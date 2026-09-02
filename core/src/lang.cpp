// hci::lang implementation (shell chrome i18n).

#include "hci/lang.h"

#include <map>

namespace hci {
namespace lang {

namespace {

const std::map<std::string, std::string>& zhTable()
{
    static const std::map<std::string, std::string> t = {
        // Footer buttons
        {"Back", "\u4e0a\u4e00\u6b65"},
        {"Next", "\u4e0b\u4e00\u6b65"},
        {"Cancel", "\u53d6\u6d88"},
        {"Accept", "\u63a5\u53d7"},
        {"Install", "\u5b89\u88c5"},
        {"Continue", "\u7ee7\u7eed"},
        {"Finish", "\u5b8c\u6210"},
        // Welcome page
        {"Installer", "\u5b89\u88c5\u7a0b\u5e8f"},
        {"This wizard will guide you through the installation.",
         "\u6b64\u5411\u5bfc\u5c06\u5f15\u5bfc\u60a8\u5b8c\u6210\u5b89\u88c5\u3002"},
        // License page
        {"I accept the license", "\u6211\u63a5\u53d7\u8bb8\u53ef\u534f\u8bae"},
        // Path page
        {"Install directory:", "\u5b89\u88c5\u76ee\u5f55\uff1a"},
        {"Choose install directory", "\u9009\u62e9\u5b89\u88c5\u76ee\u5f55"},
        {"Browse...", "\u6d4f\u89c8..."},
        {"The target directory will be cleared before installation.",
         "\u5b89\u88c5\u524d\u5c06\u6e05\u7a7a\u76ee\u6807\u76ee\u5f55\u3002"},
        {"Directory not empty", "\u76ee\u5f55\u4e0d\u4e3a\u7a7a"},
        {"The target directory already contains files.\n\n"
         "It will be cleared before installation.\n\nContinue?",
         "\u76ee\u6807\u76ee\u5f55\u5df2\u5305\u542b\u6587\u4ef6\u3002\n\n"
         "\u5b89\u88c5\u524d\u5c06\u4f1a\u6e05\u7a7a\u3002\n\n\u7ee7\u7eed\uff1f"},
        // Components page
        {"Select components:", "\u9009\u62e9\u7ec4\u4ef6\uff1a"},
        // Confirm page
        {"Yes, continue", "\u662f\uff0c\u7ee7\u7eed"},
        // Finish page
        {"Launch now", "\u7acb\u5373\u542f\u52a8"},
        // Progress page
        {"Preparing...", "\u6b63\u5728\u51c6\u5907..."},
        {"Working...", "\u6b63\u5728\u5de5\u4f5c..."},
        {"Installing...", "\u6b63\u5728\u5b89\u88c5..."},
        {"Error", "\u9519\u8bef"},
        {"Notice", "\u63d0\u793a"},
        {"Language", "\u8bed\u8a00"},
        {"Select language:", "\u9009\u62e9\u8bed\u8a00\uff1a"},
        {"Unknown page: ", "\u672a\u77e5\u9875\u9762\uff1a "},
        // Elevation
        {"Administrator privileges required",
         "\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650"},
        {"This action needs administrator privileges.\n\nRestart as administrator and continue?",
         "\u6b64\u64cd\u4f5c\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650\u3002\n\n"
         "\u662f\u5426\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u91cd\u542f\u5e76\u7ee7\u7eed\uff1f"},
        // TUI chrome
        {"Select an option:", "\u9009\u62e9\u4e00\u9879\uff1a"},
        {"Enter value:", "\u8f93\u5165\u503c\uff1a"},
        {"Toggle items by id, blank = done:", "\u7528 id \u5207\u6362\u9879\uff0c\u56de\u8f66\u786e\u8ba4\uff1a"},
        {"Press Enter to continue...", "\u6309\u56de\u8f66\u7ee7\u7eed..."},
        {"Installation complete", "\u5b89\u88c5\u5b8c\u6210"},
        {"Installation failed", "\u5b89\u88c5\u5931\u8d25"},
        {"Please read and accept the license.", "\u8bf7\u9605\u8bfb\u5e76\u63a5\u53d7\u8bb8\u53ef\u534f\u8bae"},
        {"Accept the license?", "\u63a5\u53d7\u8bb8\u53ef\u534f\u8bae\uff1f"},
        {"License", "\u8bb8\u53ef\u534f\u8bae"},
        {"A value is required", "\u5fc5\u987b\u8f93\u5165\u503c"},
        {"Install directory", "\u5b89\u88c5\u76ee\u5f55"}, 
        {"Install directory:", "\u5b89\u88c5\u76ee\u5f55\uff1a"},
        // Language picker entries (self-describing)
        {"English", "English"},
        {"\u7b80\u4f53\u4e2d\u6587", "\u7b80\u4f53\u4e2d\u6587"},
    };
    return t;
}

} // namespace

std::string tr(const std::string& code, const std::string& en)
{
    if (code == "zh") {
        const auto& t = zhTable();
        auto it = t.find(en);
        if (it != t.end()) return it->second;
    }
    return en;
}

std::vector<std::pair<std::string, std::string>> availableLanguages()
{
    return {
        {"en", "English"},
        {"zh", "\u7b80\u4f53\u4e2d\u6587"},
    };
}

} // namespace lang
} // namespace hci