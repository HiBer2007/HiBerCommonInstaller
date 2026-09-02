#pragma once

#include <memory>
#include <string>

#include <QDialog>
#include <QEventLoop>
#include <QFrame>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>

#include "hci/flow.h"
#include "hci/product.h"

namespace hci {
// Registers the "qrc" deploy source kind (implemented in qrc_source.cpp).
bool registerQtSources();

namespace gui {

class GuiShell;

// Per-page result conventions:
//   welcome:    (none)
//   license:    bool accepted
//   path:       QString path
//   components: QVariantList<bool> checked (order == product.components)
//   option:     int selected
//   confirm:    bool yes
//   input:      QString value
//   finish:     bool launch
using PageFactory = std::function<QWidget*(const nlohmann::json& params,
                                           GuiShell& shell, QVariant& result)>;

// Built-in page registry; extensions may register more via the same API.
void registerPage(const std::string& uiId, PageFactory factory);
QWidget* createPage(const std::string& uiId, const nlohmann::json& params,
                    GuiShell& shell, QVariant& result);
void unregisterAllPages();

// GuiFlowUi: IFlowUi implemented with Qt widgets; interactive steps block
// on nested event loops until the user clicks Next (or cancels).
class GuiFlowUi : public hci::IFlowUi {
public:
    GuiFlowUi(GuiShell& shell, bool silent = false);

    bool onLanguage(std::string& selected, const std::string& def) override;
    void onStepParam(const nlohmann::json& params, bool canGoBack) override;
    bool backRequested() const override;
    bool onElevate(const std::string& reason, bool autoRestart) override;
    bool onWelcome(const std::string& productName,
                   const hci::ProductConfig& product) override;
    bool onLicense(const std::string& text, bool& accepted) override;
    bool onPath(std::string& path, const std::string& defaultPath) override;
    bool onComponents(const std::vector<hci::ProductComponent>& components,
                      std::vector<bool>& checked) override;
    bool onOption(const std::string& prompt,
                  const std::vector<std::string>& choices,
                  int& selected) override;
    bool onConfirm(const std::string& prompt, bool& yes) override;
    bool onInput(const std::string& prompt, std::string& value,
                 bool required) override;
    bool onGit(bool systemAvailable, std::string& mode,
               const std::string& def, bool showInstallSystem) override;
    void onProgress(const std::string& step, int percent,
                    const std::string& detail) override;
    void onMessage(const std::string& text, bool isError) override;
    void onFinish(bool success, const std::string& message,
                  const std::string& launchExe,
                  const std::vector<std::string>& launchOptions) override;

private:
    GuiShell& shell_;
    bool autopilot_ = false;
};

class GuiShell : public QWidget {
    Q_OBJECT
public:
    GuiShell(const hci::ProductConfig& product, const std::string& flowFile,
             const std::string& installPath, bool silent = false,
             std::vector<std::string> extensionArgs = {},
             const std::string& language = "",
             QWidget* parent = nullptr);
    ~GuiShell() override;

    // Runs the flow; returns process exit code (0 success / 1 failure).
    int run();

    // Shows the main window (story: hidden until the language picker is
    // resolved so the welcome page renders only afterwards).
    void setVisibleForFlow();

    // Blocks until the user clicks Next on the given page (or cancels/back).
    // Returns false when cancelled or when back was requested (query
    // backRequested() to distinguish). The page is adopted into the stack.
    // showHeader=false renders a headerless page (welcome).
    bool blockOnPage(QWidget* page, const QString& nextText,
                     bool showHeader = true);

    void setHeaderVisible(bool on);
    void setNextEnabled(bool on);
    void cancelFlow();
    bool cancelled() const { return cancelled_; }

    // "back" navigation (previous step, FlowRunner re-runs it).
    bool backRequested() const { return backFlag_; }
    // Per-step chrome: params["buttons"] = {"back":false,"next":true,...},
    // canGoBack = a previous step exists. Missing entries keep defaults.
    void applyStepButtons(const nlohmann::json& params, bool canGoBack);

    // Current UI language ("en"/"zh"); product.defaultLanguage is the seed.
    const std::string& language() const { return lang_; }
    void setLanguage(const std::string& l) { lang_ = l; }

    void log(const QString& line);          // appends to the live log view
    void setStatus(const QString& text);
    void showProgress(const QString& stepLabel, int percent);
    void showProgressCard(const QString& title, bool cancelable);

    hci::InstallContext& context() { return ctx_; }
    hci::ProductConfig& product() { return product_; }
    const std::string& flowFile() const { return flowFile_; }
    bool silent() const { return silent_; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildProgressPage();

    hci::ProductConfig product_;
    std::string flowFile_;
    hci::InstallContext ctx_;
    bool cancelled_ = false;
    bool silent_ = false;
    std::vector<std::string> extensionArgs_;

    QLabel* titleLabel_ = nullptr;
    QFrame* headerLine_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QWidget* progressPage_ = nullptr;
    QLabel* progressStepLabel_ = nullptr;
    QLabel* progressPercentLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTextEdit* logView_ = nullptr;
    std::unique_ptr<QEventLoop> activeLoop_;
    bool gateNext_ = false;
    bool nextForced_ = false;   // flow forced next disabled (buttons.next=false)
    bool backVisible_ = false;  // back button shown (product.backEnabled && canGoBack)
    bool backEnabled_ = false;  // back clickable (flow buttons.back != false)
    bool backFlag_ = false;     // back requested by the user on last interaction
    bool showHeader_ = true;    // current page shows the title header
    std::string lang_;          // active UI language ("en"|"zh")

    friend class GuiFlowUi;
};

} // namespace gui
} // namespace hci