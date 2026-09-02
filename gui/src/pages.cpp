// Built-in GUI pages (presets) + page factory registry.
// Page ids: welcome | license | path | components | option | confirm |
//          input | finish   (progress handled directly by GuiShell)

#include "gui_shell.h"
#include "hci/lang.h"

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

// GUI 欢迎页：主标题（大字，略小）+ 副标题（中等）/描述（稍大），整体
// 向右偏移，压缩空白。
QWidget* welcomeTitle(const std::string& productName)
{
    auto* label = new QLabel(QString::fromUtf8(productName.c_str()));
    QFont f = label->font();
    f.setPointSize(22);
    f.setBold(true);
    label->setFont(f);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

QWidget* welcomeSubtitle(const std::string& text)
{
    auto* label = new QLabel(QString::fromUtf8(text.c_str()));
    QFont f = label->font();
    f.setPointSize(15);
    label->setFont(f);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

// ------------------------------------------------------------------
// Welcome page layout (common):
//   left-shifted block: big title (welcomeTitle || productName),
//   medium subtitle ("Installer"), description (slightly bigger),
//   bottom-right: small grey "Powered by HiBer Common Installer Module"
QWidget* pageWelcome(const nlohmann::json&, GuiShell& shell, QVariant&)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    const hci::ProductConfig& p = shell.product();

    // Compact layout: tighten spacing, shift content to the right.
    l->setContentsMargins(48, 16, 16, 8);
    l->setSpacing(10);

    std::string big = p.welcomeTitle.empty() ? p.productName : p.welcomeTitle;
    l->addWidget(welcomeTitle(big));
    l->addWidget(welcomeSubtitle(hci::lang::tr(shell.language(), "Installer")));

    auto* desc = new QLabel(QString::fromUtf8(hci::lang::tr(shell.language(),
        "This wizard will guide you through the installation.").c_str()), w);
    QFont df = desc->font();
    df.setPointSize(13);
    desc->setFont(df);
    desc->setWordWrap(true);
    l->addWidget(desc);

    l->addStretch(1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    auto* powered = new QLabel(QString::fromUtf8(
        p.poweredBy.empty()
            ? "Powered by HiBer Common Installer Module"
            : p.poweredBy.c_str()), w);
    powered->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    bottom->addWidget(powered);
    l->addLayout(bottom);
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
    auto* accept = new QCheckBox(QString::fromUtf8(
        hci::lang::tr(shell.language(), "I accept the license").c_str()), w);
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
    auto* lbl = new QLabel(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Install directory:").c_str()), w);
    l->addWidget(lbl);
    auto* edit = new QLineEdit(QString::fromUtf8(params.value("default", "").c_str()), w);
    auto* browse = new QPushButton(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Browse...").c_str()), w);
    auto* hl = new QHBoxLayout();
    hl->addWidget(edit, 1);
    hl->addWidget(browse);
    l->addLayout(hl);
    auto* note = new QLabel(QString::fromUtf8(hci::lang::tr(shell.language(),
        "The target directory will be cleared before installation.").c_str()), w);
    note->setWordWrap(true);
    l->addWidget(note);
    l->addStretch(1);

    QObject::connect(browse, &QPushButton::clicked, &shell, [&shell, edit]() {
        QString d = QFileDialog::getExistingDirectory(&shell,
            QString::fromUtf8(hci::lang::tr(shell.language(),
                "Choose install directory").c_str()), edit->text());
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
    l->addWidget(new QLabel(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Select components:").c_str()), w));
    auto* help = new QLabel(QString::fromUtf8(hci::lang::tr(shell.language(),
        "Choose what to install. Required components cannot be unchecked.").c_str()), w);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: #777; font-size: 11px;"));
    l->addWidget(help);
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
            // CRASH FIX: do NOT capture the local 'list' by reference - the
            // page ctor returns while the nested event loop runs, so the
            // lambda would write a dangling stack slot. Update 'res' instead
            // (it stays alive while blockOnPage blocks in the caller).
            QObject::connect(cb, &QCheckBox::toggled, &shell, [&res, i](bool on) {
                QVariantList l = res.toList();
                while (static_cast<int>(l.size()) <= i) l.append(false);
                l[i] = on;
                res = l;
            });
            l->addWidget(cb);
            if (c.contains("description")) {
                const std::string desc = c.value("description", "");
                if (!desc.empty()) {
                    auto* dl = new QLabel(QString::fromUtf8(desc.c_str()), w);
                    dl->setStyleSheet(QStringLiteral("color: #777; font-size: 11px;"));
                    dl->setWordWrap(true);
                    dl->setContentsMargins(24, 0, 0, 4);
                    l->addWidget(dl);
                }
            }
            ++idx;
        }
    }
    l->addStretch(1);
    res = list;
    // Next must be enabled - the previous page's gating must not leak here.
    shell.setNextEnabled(true);
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
    auto* yes = new QCheckBox(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Yes, continue").c_str()), w);
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

QWidget* pageGit(const nlohmann::json& params, GuiShell& shell, QVariant& res)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Choose the Git strategy:").c_str()), w));

    bool sysAvail = params.value("systemAvailable", false);
    std::string sysPath = params.value("systemPath", "");
    std::string infoText = sysAvail
        ? hci::lang::tr(shell.language(), "System Git found: ") + sysPath
        : hci::lang::tr(shell.language(), "Git was not found on this system.");
    auto* info = new QLabel(QString::fromUtf8(infoText.c_str()), w);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color: #5a7; font-size: 12px;"));
    l->addWidget(info);

    std::string def = params.value("default", "system");
    bool showInstallSystem = params.value("showInstallSystem", false);
    auto* rbSys = new QRadioButton(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Use system Git").c_str()), w);
    auto* rbBun = new QRadioButton(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Use bundled Git").c_str()), w);
    rbSys->setEnabled(sysAvail);
    // "Install system Git" is offered only when no system git exists.
    auto* rbInst = (!sysAvail && showInstallSystem)
        ? new QRadioButton(QString::fromUtf8(
            hci::lang::tr(shell.language(),
                          "Download and install system Git (admin)").c_str()), w)
        : nullptr;
    if (def == "bundled") {
        rbBun->setChecked(true);
    } else if (def == "install-system" && rbInst) {
        rbInst->setChecked(true);
    } else {
        rbSys->setChecked(true);
    }
    l->addWidget(rbSys);
    l->addWidget(rbBun);
    if (rbInst) l->addWidget(rbInst);
    QObject::connect(rbSys, &QRadioButton::toggled, &shell, [&res](bool on) {
        if (on) res = QStringLiteral("system");
    });
    QObject::connect(rbBun, &QRadioButton::toggled, &shell, [&res](bool on) {
        if (on) res = QStringLiteral("bundled");
    });
    if (rbInst) {
        QObject::connect(rbInst, &QRadioButton::toggled, &shell, [&res](bool on) {
            if (on) res = QStringLiteral("install-system");
        });
    }
    res = QString::fromUtf8(def.c_str());
    l->addStretch(1);
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

    // Multiple launch options: each entry is "name=absPath" (flow resolves
    // templates); the user picks which to start. res = QVariantList of
    // selected "name=path" strings. Legacy single launch stays a bool.
    std::vector<std::string> options;
    if (params.contains("launchOptions") && params["launchOptions"].is_array()) {
        for (auto& o : params["launchOptions"])
            options.push_back(o.get<std::string>());
    }
    if (success && !options.empty()) {
        QVariantList sel;
        for (auto& o : options) {
            std::string name = o;
            size_t eq = o.find('=');
            if (eq != std::string::npos) name = o.substr(0, eq);
            auto* cb = new QCheckBox(
                QString::fromUtf8((hci::lang::tr(shell.language(), "Launch now") +
                                   " " + name).c_str()), w);
            cb->setChecked(true);
            sel.append(QString::fromUtf8(o.c_str()));
            QObject::connect(cb, &QCheckBox::toggled, &shell, [&res, o](bool on) {
                QVariantList l2 = res.toList();
                QByteArray item = QString::fromUtf8(o.c_str()).toUtf8();
                int idx = -1;
                for (int k = 0; k < l2.size(); ++k)
                    if (l2[k].toByteArray() == item) { idx = k; break; }
                if (on && idx < 0) l2.append(QString::fromUtf8(o.c_str()));
                if (!on && idx >= 0) l2.removeAt(idx);
                res = l2;
            });
            l->addWidget(cb);
        }
        res = sel;
    } else {
        std::string launch = params.value("launch", "");
        bool hasLaunch = !launch.empty() && success;
        res = false;
        if (hasLaunch) {
            auto* launch = new QCheckBox(QString::fromUtf8(
                hci::lang::tr(shell.language(), "Launch now").c_str()), w);
            launch->setChecked(true);
            l->addWidget(launch);
            QObject::connect(launch, &QCheckBox::toggled, &shell, [&res](bool on) {
                res = on;
            });
        }
    }
    l->addStretch(1);
    return w;
}

QWidget* pageFallback(const nlohmann::json& params, GuiShell& shell, QVariant&)
{
    auto* w = new QWidget(&shell);
    auto* l = new QVBoxLayout(w);
    auto* lbl = new QLabel(QString::fromUtf8(
        hci::lang::tr(shell.language(), "Unknown page: ").c_str()) +
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
        registerPage("git", pageGit);
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