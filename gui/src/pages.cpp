// Built-in GUI pages (presets) + page factory registry.
// Page ids: welcome | license | path | components | option | confirm |
//          input | finish   (progress handled directly by GuiShell)

#include "gui_shell.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

#include <map>

namespace hci {
namespace gui {

namespace {

std::map<std::string, PageFactory>& registry()
{
    static std::map<std::string, PageFactory> r;
    return r;
}

// GUI 欢迎页标题：直接使用大字（非 CLI/TUI 的 ASCII 拼接字——那是终端的
// 无奈之举；GUI 内用真实字体渲染）。
QWidget* welcomeTitle(const std::string& productName)
{
    auto* label = new QLabel(QString::fromUtf8(productName.c_str()));
    QFont f = label->font();
    f.setPointSize(26);
    f.setBold(true);
    label->setFont(f);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

// ------------------------------------------------------------------
QWidget* pageWelcome(const nlohmann::json&, GuiShell& shell, QVariant&)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    const hci::ProductConfig& p = shell.product();
    l->addWidget(welcomeTitle(p.productName));
    auto* info = new QLabel(QStringLiteral(
        "This wizard will guide you through the installation.\n\n"
        "Powered by HiBer Common Installer Module"), w);
    info->setWordWrap(true);
    l->addWidget(info);
    l->addStretch(1);
    return w;
}

QWidget* pageLicense(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    auto* text = new QTextEdit(w);
    text->setReadOnly(true);
    text->setPlainText(QString::fromUtf8(params.value("text", "").c_str()));
    l->addWidget(text, 1);
    auto* accept = new QCheckBox(QStringLiteral("I accept the license"), w);
    l->addWidget(accept);
    res = false;
    shell.setNextEnabled(false);
    QObject::connect(accept, &QCheckBox::toggled, &shell, [&shell, &res](bool on) {
        res = on;
        shell.setNextEnabled(on);
    });
    return w;
}

QWidget* pagePath(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    auto* lbl = new QLabel(QStringLiteral("Install directory:"), w);
    l->addWidget(lbl);
    auto* edit = new QLineEdit(QString::fromUtf8(params.value("default", "").c_str()), w);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), w);
    auto* hl = new QHBoxLayout();
    hl->addWidget(edit, 1);
    hl->addWidget(browse);
    l->addLayout(hl);
    auto* note = new QLabel(QStringLiteral(
        "The target directory will be cleared before installation."), w);
    note->setWordWrap(true);
    l->addWidget(note);
    l->addStretch(1);

    QObject::connect(browse, &QPushButton::clicked, &shell, [&shell, edit]() {
        QString d = QFileDialog::getExistingDirectory(&shell,
            QStringLiteral("Choose install directory"), edit->text());
        if (!d.isEmpty()) edit->setText(d);
    });
    QObject::connect(edit, &QLineEdit::textChanged, &shell, [&shell, &res](const QString& t) {
        res = t;
        shell.setNextEnabled(!t.trimmed().isEmpty());
    });
    shell.setNextEnabled(!edit->text().trimmed().isEmpty());
    res = edit->text();
    return w;
}

QWidget* pageComponents(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QStringLiteral("Select components:"), w));
    QVariantList list;
    if (params.contains("components") && params["components"].is_array()) {
        int idx = 0;
        for (auto& c : params["components"]) {
            auto* cb = new QCheckBox(QString::fromUtf8(c.value("label", "").c_str()), w);
            bool required = c.value("required", false);
            cb->setChecked(c.value("checked", false));
            if (required) {
                cb->setChecked(true);
                cb->setEnabled(false);
            }
            list.append(cb->isChecked());
            int i = idx;
            QObject::connect(cb, &QCheckBox::toggled, &shell,
                             [&list, i](bool on) { list[i] = on; });
            l->addWidget(cb);
            ++idx;
        }
    }
    l->addStretch(1);
    res = list;
    return w;
}

QWidget* pageOption(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QString::fromUtf8(params.value("prompt", "").c_str()), w));
    int def = params.value("default", 0);
    int idx = 0;
    res = def;
    if (params.contains("choices") && params["choices"].is_array()) {
        for (auto& c : params["choices"]) {
            auto* rb = new QRadioButton(QString::fromUtf8(c.get<std::string>().c_str()), w);
            rb->setChecked(idx == def);
            int v = idx;
            QObject::connect(rb, &QRadioButton::toggled, &shell, [&res, v](bool on) {
                if (on) res = v;
            });
            l->addWidget(rb);
            ++idx;
        }
    }
    l->addStretch(1);
    return w;
}

QWidget* pageConfirm(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    auto* lbl = new QLabel(QString::fromUtf8(params.value("prompt", "").c_str()), w);
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    auto* yes = new QCheckBox(QStringLiteral("Yes, continue"), w);
    yes->setChecked(params.value("defaultYes", true));
    l->addWidget(yes);
    l->addStretch(1);
    res = yes->isChecked();
    QObject::connect(yes, &QCheckBox::toggled, &shell, [&shell, &res](bool on) {
        res = on;
        shell.setNextEnabled(true);
    });
    return w;
}

QWidget* pageInput(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QString::fromUtf8(params.value("prompt", "").c_str()), w));
    auto* edit = new QLineEdit(w);
    l->addWidget(edit);
    l->addStretch(1);
    bool required = params.value("required", true);
    if (required) shell.setNextEnabled(false);
    QObject::connect(edit, &QLineEdit::textChanged, &shell,
                      [&shell, &res, required](const QString& t) {
        res = t;
        shell.setNextEnabled(!required || !t.trimmed().isEmpty());
    });
    res = edit->text();
    return w;
}

QWidget* pageFinish(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    bool success = params.value("success", false);
    auto* lbl = new QLabel(QString::fromUtf8(params.value("message", "").c_str()), w);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(success
        ? QStringLiteral("font-size: 16px; font-weight: bold; color: green;")
        : QStringLiteral("font-size: 16px; font-weight: bold; color: red;"));
    l->addWidget(lbl);
    std::string launch = params.value("launch", "");
    bool hasLaunch = !launch.empty() && success;
    res = false;
    if (hasLaunch) {
        auto* launch = new QCheckBox(QStringLiteral("Launch now"), w);
        launch->setChecked(true);
        l->addWidget(launch);
        QObject::connect(launch, &QCheckBox::toggled, &shell, [&res](bool on) {
            res = on;
        });
    }
    l->addStretch(1);
    return w;
}

QWidget* pageFallback(const nlohmann::json& params, GuiShell& shell, QVariant&)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    auto* lbl = new QLabel(QStringLiteral("Unknown page: ") +
                           QString::fromUtf8(params.value("id", "").c_str()), w);
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    return w;
}

struct BuiltinPageRegistrar {
    BuiltinPageRegistrar()
    {
        registerPage("welcome", pageWelcome);
        registerPage("license", pageLicense);
        registerPage("path", pagePath);
        registerPage("components", pageComponents);
        registerPage("option", pageOption);
        registerPage("confirm", pageConfirm);
        registerPage("input", pageInput);
        registerPage("finish", pageFinish);
    }
};
BuiltinPageRegistrar g_builtinPages;

} // namespace

// ------------------------------------------------------------------
void registerPage(const std::string& uiId, PageFactory factory)
{
    registry()[uiId] = std::move(factory);
}

QWidget* createPage(const std::string& uiId, const nlohmann::json& params,
                    GuiShell& shell, QVariant& result)
{
    auto it = registry().find(uiId);
    if (it == registry().end()) {
        nlohmann::json fb = params;
        fb["id"] = uiId;
        return pageFallback(fb, shell, result);
    }
    return it->second(params, shell, result);
}

void unregisterAllPages()
{
    registry().clear();
}

} // namespace gui
} // namespace hci