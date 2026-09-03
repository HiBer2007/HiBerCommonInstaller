#include "gui_shell.h"

#include "hci/entry.h"
#include "hci/elevation.h"
#include "hci/exec.h"
#include "hci/extension.h"
#include "hci/log.h"
#include "hci/port.h"
#include "hci/script.h"
#include "hci/flow.h"

#include <cstdlib>

#include "resource_utils.h"
#include "hci/lang.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractAnimation>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QScreen>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPropertyAnimation>
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

// Mirrors the framework log into the GUI live log view (detailed run log).
class GuiLogSink : public hci::ILogSink {
public:
    explicit GuiLogSink(GuiShell& s) : shell_(s) {}
    void write(hci::LogLevel lv, const std::string& m) override
    {
        const char* tag = "";
        switch (lv) {
            case hci::LogLevel::Trace: tag = "[T]"; break;
            case hci::LogLevel::Debug: tag = "[D]"; break;
            case hci::LogLevel::Info:  tag = "[I]"; break;
            case hci::LogLevel::Warn:  tag = "[W]"; break;
            case hci::LogLevel::Error: tag = "[E]"; break;
        }
        shell_.log(QString::fromUtf8((std::string(tag) + " " + m).c_str()));
    }
    const char* sinkName() const override { return "gui"; }
private:
    GuiShell& shell_;
};
} // namespace

// ------------------------------------------------------------------
GuiShell::GuiShell(const hci::ProductConfig& product, const std::string& flowFile,
                   const std::string& installPath, bool silent,
                   std::vector<std::string> extensionArgs,
                   const std::string& language, QWidget* parent)
    : QWidget(parent), product_(product), flowFile_(flowFile), silent_(silent),
      extensionArgs_(std::move(extensionArgs)),
      lang_(language.empty()
            ? (product.defaultLanguage.empty() ? "en" : product.defaultLanguage)
            : language)
{
    if (!installPath.empty()) ctx_.vars().set("installDir", installPath);
    if (!language.empty()) ctx_.vars().set("language", language);

    setWindowTitle(QString::fromUtf8(product.productName.c_str()) +
                   QStringLiteral(" Installer"));
    // 无最小尺寸限制：窗口完全由页面内容驱动动态调节（切页动画随内容
    // 放大/缩小，而不是被固定下限卡住）。下限交给每页计算内部的宽松
    // 保底值（见 blockOnPage）。

    auto* root = new QVBoxLayout(this);

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: bold; padding: 4px;"));
    root->addWidget(titleLabel_);
    // Header divider line under the title (hidden together on welcome).
    headerLine_ = new QFrame(this);
    headerLine_->setFrameShape(QFrame::HLine);
    headerLine_->setStyleSheet(QStringLiteral("color: #bbb;"));
    root->addWidget(headerLine_);
    setHeaderVisible(false); // welcome renders without the header

    stack_ = new QStackedWidget(this);
    root->addWidget(stack_, 1);

    auto* footer = new QHBoxLayout();
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("color: #888;"));
    footer->addWidget(statusLabel_, 1);
    backBtn_ = new QPushButton(QString::fromUtf8(
        hci::lang::tr(lang_, "Back").c_str()), this);
    backBtn_->setVisible(false);
    nextBtn_ = new QPushButton(QString::fromUtf8(
        hci::lang::tr(lang_, "Next").c_str()), this);
    cancelBtn_ = new QPushButton(QString::fromUtf8(
        hci::lang::tr(lang_, "Cancel").c_str()), this);
    footer->addWidget(backBtn_);
    footer->addWidget(nextBtn_);
    footer->addWidget(cancelBtn_);
    root->addLayout(footer);

    connect(nextBtn_, &QPushButton::clicked, this, [this]() {
        nextBtn_->setEnabled(false);
        if (activeLoop_) activeLoop_->quit();
    });
    connect(backBtn_, &QPushButton::clicked, this, [this]() {
        backFlag_ = true;
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

    // Fixed layout: small step caption on the left, percent on the right,
    // progress bar pinned right under the header divider, log fills below.
    auto* row = new QHBoxLayout();
    progressStepLabel_ = new QLabel(QString::fromUtf8(
        hci::lang::tr(lang_, "Preparing...").c_str()), progressPage_);
    row->addWidget(progressStepLabel_, 1);
    progressPercentLabel_ = new QLabel(QStringLiteral("0%"), progressPage_);
    progressPercentLabel_->setStyleSheet(QStringLiteral("color: #555;"));
    row->addWidget(progressPercentLabel_);
    l->addLayout(row);

    progressBar_ = new QProgressBar(progressPage_);
    progressBar_->setRange(0, 100);
    l->addWidget(progressBar_);

    logView_ = new QTextEdit(progressPage_);
    logView_->setReadOnly(true);
    l->addWidget(logView_, 1);

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
                if (silent_) {
                    std::cerr << "Error: cannot read flow resource: "
                              << flowFile_ << "\n";
                    return 1;
                }
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
        Log::Error(std::string("flow load failed: ") + e.what());
        if (silent_) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
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

    // Detailed logging into the GUI log view (level Debug = every step).
    Log::instance().addSink(std::make_shared<GuiLogSink>(*this));
    Log::instance().setLevel(LogLevel::Debug);

    hci::gui::GuiFlowUi ui(*this, silent_);
    hci::FlowRunner runner(product_, ctx_, &ui, script.get());
    runner.setEventBus(&bus);
    runner.setRegistry(&registry);
    runner.setBaseDir(dirOf(flowFile_));
    runner.setResourceReader([](const std::string& path, std::string& out) -> bool {
        return hci::gui::readResource("qrc:" + path, out);
    });

    titleLabel_->setText(QString::fromUtf8(product_.productName.c_str()));
    int rc = runner.run(flow);
    if (cancelled_) rc = 1;
    loader.shutdownAll();
    close();
    return rc;
}

void GuiShell::setHeaderVisible(bool on)
{
    titleLabel_->setVisible(on);
    if (headerLine_) headerLine_->setVisible(on);
}

void GuiShell::setVisibleForFlow()
{
    if (!silent_ && !isVisible()) show();
}

bool GuiShell::blockOnPage(QWidget* page, const QString& nextText,
                           bool showHeader)
{
    showHeader_ = showHeader;
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);

    // Content-driven window sizing with an animated transition (same effect
    // as the main program wizard: geometry animation, 200ms OutCubic).
    {
        QSize hint = page->sizeHint();
        // 宽松保底下限（窗口无最小尺寸限制，小页可以缩得很小）
        int w = qMax(hint.width(), 440);
        if (page->layout()) {
            int hfw = page->layout()->heightForWidth(w);
            if (hfw > 0 && hfw < hint.height() * 3) hint.setHeight(hfw);
        }
        int h = qMax(hint.height(), 260);
        // + header divider + footer buttons overhead
        h += (headerLine_ && headerLine_->isVisible() ? 26 : 0) + 60;
        if (screen()) {
            QRect avail = screen()->availableGeometry();
            w = qMin(w, avail.width() - 60);
            h = qMin(h, avail.height() - 120);
        }
        const QRect cur = geometry();
        const QRect target(cur.x(), cur.y(), w, h);
        if (cur != target) {
            auto* anim = new QPropertyAnimation(this, "geometry", this);
            anim->setDuration(200);
            QEasingCurve easing(QEasingCurve::OutCubic);
            anim->setEasingCurve(easing);
            anim->setStartValue(cur);
            anim->setEndValue(target);
            QObject::connect(anim, &QPropertyAnimation::finished, this,
                             [this, target]() { setGeometry(target); });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            setGeometry(target);
        }
    }

    // Page transition fade-in (same effect the main program wizard uses:
    // opacity 0 -> 1 over 160ms, the effect is removed when finished).
    auto* effect = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", page);
    anim->setDuration(160);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    QObject::connect(anim, &QPropertyAnimation::finished, page, [page]() {
        page->setGraphicsEffect(nullptr);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    nextBtn_->setText(nextText);
    if (showHeader_) {
        // Header: "<product> 安装程序" (i18n) + divider.
        titleLabel_->setText(QString::fromUtf8(product_.productName.c_str()) +
                             QStringLiteral(" ") +
                             QString::fromUtf8(hci::lang::tr(lang_, "Installer").c_str()));
        setHeaderVisible(true);
    } else {
        setHeaderVisible(false);
    }
    backBtn_->setText(QString::fromUtf8(hci::lang::tr(lang_, "Back").c_str()));
    cancelBtn_->setText(QString::fromUtf8(hci::lang::tr(lang_, "Cancel").c_str()));
    backBtn_->setVisible(backVisible_);
    backBtn_->setEnabled(backEnabled_);
    if (!gateNext_) nextBtn_->setEnabled(!nextForced_);
    gateNext_ = false;
    nextForced_ = false;
    backFlag_ = false;
    if (cancelBtn_) cancelBtn_->setEnabled(true);

    QEventLoop loop;
    activeLoop_.reset(&loop);
    loop.exec();
    activeLoop_.release();

    stack_->removeWidget(page);
    page->deleteLater();
    return !cancelled_ && !backFlag_;
}

void GuiShell::applyStepButtons(const nlohmann::json& params, bool canGoBack)
{
    bool useBack = product_.backEnabled && canGoBack;
    bool useNext = true;
    bool useCancel = true;
    if (params.contains("buttons") && params["buttons"].is_object()) {
        const auto& b = params["buttons"];
        if (b.contains("back") && b["back"].is_boolean())
            useBack = product_.backEnabled && b["back"].get<bool>();
        if (b.contains("next") && b["next"].is_boolean())
            useNext = b["next"].get<bool>();
        if (b.contains("cancel") && b["cancel"].is_boolean())
            useCancel = b["cancel"].get<bool>();
    }
    backVisible_ = useBack;
    backEnabled_ = useBack;
    nextForced_ = !useNext;
    cancelBtn_->setVisible(useCancel); // "close" back: hide; disable instead by flow
    cancelBtn_->setEnabled(useCancel);
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
    if (progressStepLabel_)
        progressStepLabel_->setText(stepLabel.isEmpty()
            ? QString::fromUtf8(hci::lang::tr(lang_, "Working...").c_str())
            : stepLabel);
    if (progressPercentLabel_ && percent >= 0)
        progressPercentLabel_->setText(QString::number(percent) + QStringLiteral("%"));
    if (progressBar_ && percent >= 0) progressBar_->setValue(percent);
}

void GuiShell::showProgressCard(const QString& title, bool cancelable)
{
    (void)title;
    (void)cancelable; // legacy: replaced by the pinned fixed progress row
}

// ------------------------------------------------------------------
GuiFlowUi::GuiFlowUi(GuiShell& shell, bool silent) : shell_(shell)
{
    autopilot_ = silent || !port::getEnv("HCI_GUI_AUTOPILOT").empty();
}

bool GuiFlowUi::onLanguage(std::string& selected, const std::string& def)
{
    shell_.setLanguage(def); // defaults apply before any picker interaction
    if (autopilot_) { selected = def; return true; }

    // The main window stays hidden (welcome page not rendered) until the
    // language picker is resolved.
    shell_.setVisible(false);

    // Small picker window right before the welcome page.
    QDialog dlg(&shell_);
    dlg.setWindowTitle(QString::fromUtf8(
        hci::lang::tr(def, "Language").c_str()));
    auto* l = new QVBoxLayout(&dlg);
    auto* lbl = new QLabel(QString::fromUtf8(
        hci::lang::tr(def, "Select language:").c_str()), &dlg);
    l->addWidget(lbl);
    auto* list = new QListWidget(&dlg);
    auto langs = hci::lang::availableLanguages();
    int pre = 0;
    for (size_t i = 0; i < langs.size(); ++i) {
        list->addItem(QString::fromUtf8(langs[i].second.c_str()));
        if (langs[i].first == def) pre = static_cast<int>(i);
    }
    list->setCurrentRow(pre);
    l->addWidget(list);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok |
                                    QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(bb);
    dlg.setMinimumWidth(280);

    if (dlg.exec() != QDialog::Accepted) return false; // user backed out
    selected = langs[static_cast<size_t>(list->currentRow())].first;
    shell_.setLanguage(selected);
    return true;
}

bool GuiFlowUi::onElevate(const std::string& reason, bool autoRestart)
{
    shell_.setVisibleForFlow(); // the main window exists for the prompt
    if (autopilot_) {
        // Headless: relaunch automatically (UAC dialog still appears).
        std::string err;
        if (hci::elevation::relaunchAsAdmin(err)) std::exit(0);
        std::cerr << "Elevation failed: " << err << "\n";
        return false;
    }
    QString text = QString::fromUtf8(reason.c_str());
    text += QStringLiteral("\n\n");
    text += QString::fromUtf8(hci::lang::tr(shell_.language(),
        "This action needs administrator privileges.\n\n"
        "Restart as administrator and continue?").c_str());
    int r = QMessageBox::question(&shell_,
        QString::fromUtf8(hci::lang::tr(shell_.language(),
            "Administrator privileges required").c_str()),
        text, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (r != QMessageBox::Yes) return false;
    (void)autoRestart;
    std::string err;
    if (!hci::elevation::relaunchAsAdmin(err)) {
        QMessageBox::critical(&shell_,
            QString::fromUtf8(hci::lang::tr(shell_.language(), "Error").c_str()),
            QString::fromUtf8(err.c_str()));
        return false;
    }
    std::exit(0); // elevated instance takes over
}

void GuiFlowUi::onStepParam(const nlohmann::json& params, bool canGoBack)
{
    shell_.applyStepButtons(params, canGoBack);
}

bool GuiFlowUi::backRequested() const
{
    return shell_.backRequested();
}

bool GuiFlowUi::onWelcome(const std::string& productName,
                          const hci::ProductConfig&)
{
    if (autopilot_) return true;
    shell_.setVisibleForFlow(); // no language step: show before the welcome
    shell_.setStatus(QString::fromUtf8(productName.c_str()));
    QVariant res;
    QWidget* page = createPage("welcome", nlohmann::json::object(), shell_, res);
    return shell_.blockOnPage(page, QString::fromUtf8(
        hci::lang::tr(shell_.language(), "Next").c_str()), false);
}

bool GuiFlowUi::onLicense(const std::string& text, bool& accepted)
{
    if (autopilot_) { accepted = true; return true; }
    QVariant res;
    QWidget* page = createPage("license", nlohmann::json{{"text", text}}, shell_, res);
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Next").c_str()))) return false;
    accepted = res.toBool();
    return true;
}

bool GuiFlowUi::onPath(std::string& path, const std::string& defaultPath)
{
    if (autopilot_) { if (path.empty()) path = defaultPath; return true; }
    QVariant res;
    std::string p = path.empty() ? defaultPath : path;
    QWidget* page = createPage("path", nlohmann::json{{"default", p}}, shell_, res);
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Install").c_str()))) return false;
    path = res.toString().toUtf8().toStdString();
    if (!path.empty()) {
        QDir d(QString::fromUtf8(path.c_str()));
        if (d.exists() && !d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
            int r = QMessageBox::warning(&shell_,
                QString::fromUtf8(hci::lang::tr(shell_.language(),
                    "Directory not empty").c_str()),
                QString::fromUtf8(hci::lang::tr(shell_.language(),
                    "The target directory already contains files.\n\n"
                    "It will be cleared before installation.\n\nContinue?").c_str()),
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
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Next").c_str()))) return false;
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
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Next").c_str()))) return false;
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
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Continue").c_str()))) return false;
    yes = res.toBool();
    return true;
}

bool GuiFlowUi::onInput(const std::string& prompt, std::string& value, bool required)
{
    if (autopilot_) return !required;
    QVariant res;
    QWidget* page = createPage("input",
        nlohmann::json{{"prompt", prompt}, {"required", required}}, shell_, res);
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Next").c_str()))) return false;
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

bool GuiFlowUi::onGit(bool systemAvailable, std::string& mode,
                      const std::string& def, bool showInstallSystem)
{
    if (autopilot_) { mode = def; return true; }
    nlohmann::json params;
    params["systemAvailable"] = systemAvailable;
    params["systemPath"] = shell_.context().vars().get("gitSystemPath");
    params["default"] = def;
    params["showInstallSystem"] = showInstallSystem;
    QVariant res;
    QWidget* page = createPage("git", params, shell_, res);
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Next").c_str()))) return false;
    mode = res.toString().toUtf8().toStdString();
    return true;
}

void GuiFlowUi::onFinish(bool success, const std::string& message,
                         const std::string& launchExe,
                         const std::vector<std::string>& launchOptions)
{
    if (autopilot_) {
        // Headless/CI: do NOT launch any program (it would block the run).
        return;
    }
    nlohmann::json params;
    params["success"] = success;
    params["message"] = message;
    params["launch"] = launchExe;
    if (!launchOptions.empty()) {
        for (auto& o : launchOptions) params["launchOptions"].push_back(o);
    }
    QVariant res;
    QWidget* page = createPage("finish", params, shell_, res);
    // finish is the terminal step: no "back" (nothing to return to).
    shell_.applyStepButtons(nlohmann::json{{"buttons",
        nlohmann::json{{"back", false}}}}, false);
    if (!shell_.blockOnPage(page, QString::fromUtf8(
            hci::lang::tr(shell_.language(), "Finish").c_str()))) return;

    if (!success) return;
    if (!launchOptions.empty()) {
        // Launch the user-selected options (each "name=absPath"), detached.
        QVariantList sel = res.toList();
        for (auto& s : sel) {
            std::string item = s.toString().toUtf8().toStdString();
            size_t eq = item.find('=');
            std::string path = eq == std::string::npos ? item : item.substr(eq + 1);
            if (!path.empty()) {
                exec::ProcessResult r;
                exec::runProcess({path}, -1, r); // detach: do not wait
            }
        }
    } else if (res.toBool() && !launchExe.empty()) {
        exec::ProcessResult r;
        exec::runProcess({launchExe}, -1, r); // detach
    }
}

} // namespace gui
} // namespace hci