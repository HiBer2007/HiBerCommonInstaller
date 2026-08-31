#pragma once

#include <memory>
#include <string>

#include <QDialog>
#include <QEventLoop>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>

#include "hci/flow.h"
#include "hci/product.h"

namespace HiBerGUI {
class ProgressCard;
}

namespace hci {
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
    GuiFlowUi(GuiShell& shell);

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
    void onProgress(const std::string& step, int percent,
                    const std::string& detail) override;
    void onMessage(const std::string& text, bool isError) override;
    void onFinish(bool success, const std::string& message,
                  const std::string& launchExe) override;

private:
    GuiShell& shell_;
    bool autopilot_ = false;
};

class GuiShell : public QWidget {
    Q_OBJECT
public:
    GuiShell(const hci::ProductConfig& product, const std::string& flowFile,
             const std::string& installPath, QWidget* parent = nullptr);
    ~GuiShell() override;

    // Runs the flow; returns process exit code (0 success / 1 failure).
    int run();

    // Blocks until the user clicks Next on the given page (or cancels).
    // Returns false when cancelled. The page is adopted into the stack.
    bool blockOnPage(QWidget* page, const QString& nextText);

    void setNextEnabled(bool on);
    void cancelFlow();
    bool cancelled() const { return cancelled_; }

    void log(const QString& line);          // appends to the live log view
    void setStatus(const QString& text);
    void showProgress(const QString& stepLabel, int percent);
    void showProgressCard(const QString& title, bool cancelable);

    hci::InstallContext& context() { return ctx_; }
    hci::ProductConfig& product() { return product_; }
    const std::string& flowFile() const { return flowFile_; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildProgressPage();

    hci::ProductConfig product_;
    std::string flowFile_;
    hci::InstallContext ctx_;
    bool cancelled_ = false;

    QLabel* titleLabel_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QWidget* progressPage_ = nullptr;
    QLabel* progressStepLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTextEdit* logView_ = nullptr;
    HiBerGUI::ProgressCard* progressCard_ = nullptr;
    std::unique_ptr<QEventLoop> activeLoop_;
    bool gateNext_ = false;

    friend class GuiFlowUi;
};

} // namespace gui
} // namespace hci