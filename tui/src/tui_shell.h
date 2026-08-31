#pragma once

#include <map>
#include <string>
#include <vector>

#include "hci/flow.h"
#include "hci/product.h"

namespace hci {
namespace tui {

// ------------------------------------------------------------------
// Terminal helpers (UTF-8 + wcwidth).
// ------------------------------------------------------------------
// Display width of a UTF-8 string (CJK/fullwidth = 2 cells, combining = 0).
size_t displayWidth(const std::string& utf8);

// Word-wrap text to a target display width.
std::vector<std::string> wrapText(const std::string& text, size_t width);

// ANSI helpers.
std::string ansi(const char* code, const std::string& text); // wrap with color

// ------------------------------------------------------------------
// TuiShell: console UI host. Flow-driven; interactive steps render here.
// ------------------------------------------------------------------
class TuiShell {
public:
    TuiShell(const hci::ProductConfig& product, const std::string& flowFile,
             const std::string& installPath);

    int run();

    // Rendering
    void clear();
    void print(const std::string& line);
    void printTitle(const std::string& line);
    void printError(const std::string& line);
    void printProgress(const std::string& label, int percent);

    // Input
    std::string prompt(const std::string& question, const std::string& def = "");
    bool confirm(const std::string& question, bool defaultYes = true);
    int select(const std::string& prompt, const std::vector<std::string>& choices,
               int defaultIndex = 0);
    bool toggleList(const std::string& title,
                    const std::vector<std::string>& labels,
                    std::vector<bool>& checked);

    hci::ProductConfig& product() { return product_; }
    hci::InstallContext& context() { return ctx_; }
    const std::string& flowFile() const { return flowFile_; }
    bool autopilot() const { return autopilot_; }
    size_t width() const { return width_; }

private:
    hci::ProductConfig product_;
    std::string flowFile_;
    hci::InstallContext ctx_;
    bool autopilot_ = false;
    size_t width_ = 80;
};

// TuiFlowUi: IFlowUi rendered by TuiShell.
class TuiFlowUi : public hci::IFlowUi {
public:
    explicit TuiFlowUi(TuiShell& shell);

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
    TuiShell& shell_;
    bool autopilot_ = false;
};

} // namespace tui
} // namespace hci