#include "gui_shell.h"

#include "hci/entry.h"
#include "hci/exec.h"
#include "hci/extension.h"
#include "hci/log.h"
#include "hci/port.h"
#include "hci/script.h"
#include "hci/flow.h"

#include "resource_utils.h"

#include <progress_card.h>

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace hci {
namespace gui {

// ------------------------------------------------------------------
// Page registry + built-in pages (implemented in pages.cpp)
// ------------------------------------------------------------------
void registerPage(const std::string& uiId, PageFactory factory);
QWidget* createPage(const std::string& uiId, const nlohmann::json& params,
                    GuiShell& shell, QVariant& result);
void unregisterAllPages();

namespace {
std::string dirOf(const std::string& p)
{
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}
} // namespace

// ------------------------------------------------------------------
GuiShell::GuiShell(const hci::ProductConfig& product, const std::string& flowFile,
                   const std::string& installPath, bool silent,
                   std::vector<std::string> extensionArgs, QWidget* parent)
    : QWidget(parent), product_(product), flowFile_(flowFile), silent_(silent),
      extensionArgs_(std::move(extensionArgs))
{
    if (!installPath.empty()) ctx_.vars().set("installDir", installPath);

    setWindowTitle(QString::fromUtf8(product.productName.c_str()) +
                   QStringLiteral(" Installer"));
    setMinimumSize(640, 460);

    auto* root = new QVBoxLayout(this);

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: bold; padding: 4px;"));
    root->addWidget(titleLabel_);

    stack_ = new QStackedWidget(this);
    root->addWidget(stack_, 1);

    auto* footer = new QHBoxLayout();
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("color: #888;"));
    footer->addWidget(statusLabel_, 1);
    backBtn_ = new QPushButton(QStringLiteral("Back"), this);
    backBtn_->setVisible(false);
    nextBtn_ = new QPushButton(QStringLiteral("Next"), this);
    cancelBtn_ = new QPushButton(QStringLiteral("Cancel"), this);
    footer->addWidget(backBtn_);
    footer->addWidget(nextBtn_);
    footer->addWidget(cancelBtn_);
    root->addLayout(footer);

    connect(nextBtn_, &QPushButton::clicked, this, [this]() {
        nextBtn_->setEnabled(false);
        if (activeLoop_) activeLoop_->quit();
    });
    connect(backBtn_, &QPushButton::clicked, this, [this]() {
        if (activeLoop_) activeLoop_->quit();
    });
    connect(cancelBtn_, &QPushButton::clicked, this, &GuiShell::cancelFlow);

    buildProgressPage();
}

GuiShell::~GuiShell() = default;

QWidget* GuiShell::buildProgressPage()
{
    progressPage_ = new QWidget(this);
    auto* l = new QVBoxLayout(progressPage_);

    progressCard_ = new HiBerGUI::ProgressCard(progressPage_);
    l->addWidget(progressCard_);

    progressStepLabel_ = new QLabel(QStringLiteral("Preparing..."), progressPage_);
    l->addWidget(progressStepLabel_);

    progressBar_ = new QProgressBar(progressPage_);
    progressBar_->setRange(0, 100);
    l->addWidget(progressBar_);

    logView_ = new QTextEdit(progressPage_);
    logView_->setReadOnly(true);
    logView_->setMaximumHeight(200);
    l->addWidget(logView_);

    l->addStretch(1);
    stack_->addWidget(progressPage_);
    return progressPage_;
}

int GuiShell::run()
{
    hci::FlowSpec flow;
    try {
        if (flowFile_.rfind("qrc:", 0) == 0) {
            std::string json;
            if (!hci::gui::readResource(flowFile_, json)) {
                QMessageBox::critical(this, QStringLiteral("Error"),
                                      QStringLiteral("Cannot read flow resource: ") +
                                          QString::fromUtf8(flowFile_.c_str()));
                return 1;
            }
            flow = hci::FlowSpec::loadString(json);
        } else {
            flow = hci::FlowSpec::loadFile(flowFile_);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QStringLiteral("Error"),
                              QString::fromUtf8(e.what()));
        return 1;
    }

    auto script = hci::createLuaEngine();
    hci::EventBus bus;
    hci::ServiceRegistry services;
    hci::ExtensionRegistry registry;
    hci::ExtensionLoader loader(&bus, &services, &ctx_, &product_, &registry);
    loader.loadStatic();
    std::string extDir = port::joinPath(port::exeDir(), "extensions");
    if (fs::exists(fs::u8path(extDir))) loader.loadDirectory(extDir);

    // Route unknown args to extension cliArgs handlers; reject unhandled ones.
    for (auto& a : extensionArgs_) {
        if (registry.hasCliArg(a)) {
            if (!registry.handleCliArg(a, ctx_)) {
                std::cerr << "Error: extension rejected argument: " << a << "\n";
                loader.shutdownAll();
                return 1;
            }
        } else {
            std::cerr << "Error: unknown option: " << a << "\n";
            loader.shutdownAll();
            return 1;
        }
    }

    hci::registerQtSources();

    hci::gui::GuiFlowUi ui(*this, silent_);
    hci::FlowRunner runner(product_, ctx_, &ui, script.get());
    runner.setEventBus(&bus);
    runner.setRegistry(&registry);
    runner.setBaseDir(dirOf(flowFile_));
    runner.setResourceReader([](const std::string& path, std::string& out) -> bool {
        return hci::gui::readResource("qrc:" + path, out);
    });

    titleLabel_->setText(QString::fromUtf8(product_.productName.c_str()) +
                         QStringLiteral(" - ") +
                         QStringLiteral("Powered by HiBer Common Installer Module"));
    int rc = runner.run(flow);
    if (cancelled_) rc = 1;
    loader.shutdownAll();
    close();
    return rc;
}

bool GuiShell::blockOnPage(QWidget* page, const QString& nextText)
{
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
    nextBtn_->setText(nextText);
    if (!gateNext_) nextBtn_->setEnabled(true);
    gateNext_ = false;
    cancelBtn_->setEnabled(true);

    QEventLoop loop;
    activeLoop_.reset(&loop);
    loop.exec();
    activeLoop_.release();

    stack_->removeWidget(page);
    page->deleteLater();
    return !cancelled_;
}

void GuiShell::setNextEnabled(bool on)
{
    gateNext_ = true;
    nextBtn_->setEnabled(on);
}

void GuiShell::cancelFlow()
{
    cancelled_ = true;
    ctx_.cancel();
    if (activeLoop_) activeLoop_->quit();
}

void GuiShell::closeEvent(QCloseEvent* e)
{
    cancelFlow();
    QWidget::closeEvent(e);
}

void GuiShell::log(const QString& line)
{
    if (logView_) logView_->append(line);
}

void GuiShell::setStatus(const QString& text)
{
    if (statusLabel_) statusLabel_->setText(text);
}

void GuiShell::showProgress(const QString& stepLabel, int percent)
{
    if (progressCard_) {
        if (!progressCard_->isActive())
            progressCard_->showCard(QStringLiteral("Installing..."), false);
        progressCard_->setProgress(percent, stepLabel);
    }
    if (progressStepLabel_)
        progressStepLabel_->setText(stepLabel.isEmpty()
            ? QStringLiteral("Working...") : stepLabel);
    if (progressBar_ && percent >= 0) progressBar_->setValue(percent);
}

void GuiShell::showProgressCard(const QString& title, bool cancelable)
{
    if (progressCard_) progressCard_->showCard(title, cancelable);
}

// ------------------------------------------------------------------
GuiFlowUi::GuiFlowUi(GuiShell& shell, bool silent) : shell_(shell)
{
    autopilot_ = silent || !port::getEnv("HCI_GUI_AUTOPILOT").empty();
}

bool GuiFlowUi::onWelcome(const std::string& productName,
                          const hci::ProductConfig&)
{
    if (autopilot_) return true;
    shell_.setStatus(QString::fromUtf8(productName.c_str()));
    QVariant res;
    QWidget* page = createPage("welcome", nlohmann::json::object(), shell_, res);
    return shell_.blockOnPage(page, QStringLiteral("Next"));
}

bool GuiFlowUi::onLicense(const std::string& text, bool& accepted)
{
    if (autopilot_) { accepted = true; return true; }
    QVariant res;
    QWidget* page = createPage("license", nlohmann::json{{"text", text}}, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Accept"))) return false;
    accepted = res.toBool();
    return true;
}

bool GuiFlowUi::onPath(std::string& path, const std::string& defaultPath)
{
    if (autopilot_) { if (path.empty()) path = defaultPath; return true; }
    QVariant res;
    std::string p = path.empty() ? defaultPath : path;
    QWidget* page = createPage("path", nlohmann::json{{"default", p}}, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Install"))) return false;
    path = res.toString().toUtf8().toStdString();
    if (!path.empty()) {
        QDir d(QString::fromUtf8(path.c_str()));
        if (d.exists() && !d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
            int r = QMessageBox::warning(&shell_, QStringLiteral("Directory not empty"),
                QStringLiteral("The target directory already contains files.\n\n"
                               "It will be cleared before installation.\n\n"
                               "Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes) return false;
        }
    }
    return true;
}

bool GuiFlowUi::onComponents(const std::vector<hci::ProductComponent>& comps,
                             std::vector<bool>& checked)
{
    if (autopilot_) return true; // defaults preserved
    nlohmann::json params = nlohmann::json::object();
    for (size_t i = 0; i < comps.size(); ++i) {
        nlohmann::json c;
        c["id"] = comps[i].id;
        c["label"] = comps[i].label;
        c["required"] = comps[i].required;
        c["checked"] = checked[i];
        params["components"].push_back(c);
    }
    QVariant res;
    QWidget* page = createPage("components", params, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Next"))) return false;
    QVariantList list = res.toList();
    for (size_t i = 0; i < comps.size() && i < static_cast<size_t>(list.size()); ++i)
        checked[i] = list[static_cast<int>(i)].toBool();
    return true;
}

bool GuiFlowUi::onOption(const std::string& prompt,
                         const std::vector<std::string>& choices, int& selected)
{
    if (autopilot_) return true;
    nlohmann::json params = nlohmann::json::object();
    params["prompt"] = prompt;
    params["default"] = selected;
    for (auto& c : choices) params["choices"].push_back(c);
    QVariant res;
    QWidget* page = createPage("option", params, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Next"))) return false;
    selected = res.toInt();
    return true;
}

bool GuiFlowUi::onConfirm(const std::string& prompt, bool& yes)
{
    if (autopilot_) return true;
    QVariant res;
    QWidget* page = createPage("confirm", nlohmann::json{{"prompt", prompt},
                                                         {"defaultYes", yes}},
                               shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Continue"))) return false;
    yes = res.toBool();
    return true;
}

bool GuiFlowUi::onInput(const std::string& prompt, std::string& value, bool required)
{
    if (autopilot_) return !required;
    QVariant res;
    QWidget* page = createPage("input",
        nlohmann::json{{"prompt", prompt}, {"required", required}}, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Next"))) return false;
    value = res.toString().toUtf8().toStdString();
    return true;
}

void GuiFlowUi::onProgress(const std::string& step, int percent, const std::string& detail)
{
    QString label = QString::fromUtf8(step.c_str());
    if (!detail.empty()) label += QStringLiteral(" - ") + QString::fromUtf8(detail.c_str());
    shell_.stack_->setCurrentWidget(shell_.progressPage_);
    shell_.showProgress(label, percent);
}

void GuiFlowUi::onMessage(const std::string& text, bool isError)
{
    if (shell_.silent()) {
        // Headless mode: surface messages on stderr.
        if (isError) std::cerr << "[ERROR] " << text << "\n";
        else std::cerr << "[INFO] " << text << "\n";
        return;
    }
    if (isError) {
        shell_.log(QStringLiteral("[ERROR] ") + QString::fromUtf8(text.c_str()));
        shell_.setStatus(QStringLiteral("Error"));
    } else {
        shell_.log(QString::fromUtf8(text.c_str()));
    }
}

void GuiFlowUi::onFinish(bool success, const std::string& message,
                         const std::string& launchExe)
{
    if (autopilot_) {
        if (success && !launchExe.empty()) {
            exec::ProcessResult r;
            exec::runProcess({launchExe}, 0, r);
        }
        return;
    }
    nlohmann::json params;
    params["success"] = success;
    params["message"] = message;
    params["launch"] = launchExe;
    QVariant res;
    QWidget* page = createPage("finish", params, shell_, res);
    if (!shell_.blockOnPage(page, QStringLiteral("Finish"))) return;

    if (success && res.toBool() && !launchExe.empty()) {
        exec::ProcessResult r;
        exec::runProcess({launchExe}, 0, r);
    }
}

} // namespace gui
} // namespace hci