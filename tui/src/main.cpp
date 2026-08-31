// hci_tui - C++ ANSI TUI shell (zero Qt). Requires a terminal.
//
//   hci_tui [--product <file.json>] [--flow install|uninstall|file.json]
//           [--path <dir>] [--tui] [--help|-h] [--version|-v]
//
// Exit codes: 0 success, 1 failure, 2 usage error.

#include "hci/entry.h"
#include "hci/log.h"
#include "hci/port.h"
#include "hci/product.h"

#include "tui_shell.h"

#include <iostream>
#include <string>

using namespace hci;

namespace {

struct TuiOptions {
    std::string productJson = "product.json";
    std::string flow;
    std::string installPath;
    bool help = false;
    bool version = false;
};

bool parseArgs(int argc, char** argv, TuiOptions& o, std::string& err)
{
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        if (a == "--version" || a == "-v") { o.version = true; continue; }
        if (a == "--tui") continue;
        if (a == "--path" && i + 1 < argc) { o.installPath = argv[++i]; continue; }
        if (a == "--product" && i + 1 < argc) { o.productJson = argv[++i]; continue; }
        if (a == "--flow" && i + 1 < argc) { o.flow = argv[++i]; continue; }
        err = "unknown option: " + a;
        return false;
    }
    return true;
}

std::string dirOf(const std::string& p)
{
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

std::string absolutize(const std::string& p, const std::string& base)
{
    if (p.empty()) return p;
    if (p.size() >= 2 && (p[1] == ':' || p[0] == '/' || p[0] == '\\')) return p;
    return port::joinPath(base, p);
}

} // namespace

int main(int argc, char* argv[])
{
    TuiOptions opt;
    std::string err;
    if (!parseArgs(argc, argv, opt, err)) {
        std::cerr << "Error: " << err << "\n";
        return 2;
    }
    if (opt.help) {
        std::cout << "HiBer Common Installer Module - TUI shell\n"
                     "Usage:\n"
                     "  hci_tui [--product <file.json>] [--flow install|uninstall|file.json]\n"
                     "          [--path <dir>]\n"
                     "Exit codes: 0 success, 1 failure, 2 usage error.\n";
        return 0;
    }
    if (opt.version) {
        std::cout << "HiBer Common Installer Module v" HCI_VERSION_STRING " (hci_tui)\n";
        return 0;
    }

    // TUI requires an interactive terminal (entry model 2). CreateProcessA
    // (no console) scenarios must use the CLI shell instead. The autopilot
    // test hook bypasses the check for headless verification.
    if (!port::hasConsole() && port::getEnv("HCI_TUI_AUTOPILOT").empty()) {
        std::cerr << "Error: hci_tui requires a terminal; use hci_cli for headless installs\n";
        return 2;
    }
    port::setUtf8Console(true);
    Log::instance().addSink(std::make_shared<ConsoleSink>(LogLevel::Info));

    ProductConfig product;
    std::string productPath, flowFile;
    try {
        productPath = absolutize(opt.productJson, port::currentDir());
        product = ProductConfig::loadFile(productPath);
        std::string base = dirOf(productPath);
        std::string f = opt.flow;
        if (f.empty() || f == "install") f = product.flows.install;
        else if (f == "uninstall") f = product.flows.uninstall;
        if (f.empty()) {
            std::cerr << "Error: no flow defined (--flow or product flows.install)\n";
            return 2;
        }
        flowFile = absolutize(f, base);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    // Mandatory branding banner.
    std::cout << entry::renderBanner(product.productName, product.bannerFont);

    tui::TuiShell shell(product, flowFile, opt.installPath);
    return shell.run();
}