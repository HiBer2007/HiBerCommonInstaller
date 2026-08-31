// hci_gui - Qt GUI shell (entry model: console subsystem + holdOrRelease).
//
//   hci_gui [--product <file.json>] [--flow install|uninstall|file.json]
//           [--path <dir>] [--gui] [--help|-h] [--version|-v]
//
// Exit codes: 0 success, 1 failure, 2 usage error.

#include "hci/entry.h"
#include "hci/log.h"
#include "hci/port.h"
#include "hci/product.h"

#include "gui_shell.h"
#include "resource_utils.h"

#include <QApplication>
#include <QDir>
#include <QFile>

#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

using namespace hci;

namespace {

struct GuiOptions {
    std::string productJson = "product.json";
    std::string flow;
    std::string installPath;
    bool silent = false;
    bool help = false;
    bool version = false;
    std::vector<std::string> extensionArgs; // unknown args -> extension handlers
};

bool parseArgs(int argc, char** argv, GuiOptions& o, std::string& err)
{
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        if (a == "--version" || a == "-v") { o.version = true; continue; }
        if (a == "--gui") continue;
        if (a == "--silent" || a == "-s") { o.silent = true; continue; }
        if (a == "--path" && i + 1 < argc) { o.installPath = argv[++i]; continue; }
        if (a == "--product" && i + 1 < argc) { o.productJson = argv[++i]; continue; }
        if (a == "--flow" && i + 1 < argc) { o.flow = argv[++i]; continue; }
        // Unknown args are routed to extension cliArgs handlers (e.g. --with-editor).
        o.extensionArgs.push_back(a);
        continue;
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
    GuiOptions opt;
    std::string err;
    if (!parseArgs(argc, argv, opt, err)) {
        std::cerr << "Error: " << err << "\n";
        return 2;
    }
    if (opt.help) {
        std::cout << "HiBer Common Installer Module - GUI shell\n"
                     "Usage:\n"
                     "  hci_gui [--product <file.json>] [--flow install|uninstall|file.json]\n"
                     "          [--path <dir>]\n"
                     "Exit codes: 0 success, 1 failure, 2 usage error.\n";
        return 0;
    }
    if (opt.version) {
        std::cout << "HiBer Common Installer Module v1.0.0 (hci_gui)\n";
        return 0;
    }

    // Console lifecycle (entry model): started from a terminal -> keep it
    // (UTF-8 + logs); double-click -> released before the GUI appears.
    if (port::hasConsole()) {
        port::setUtf8Console(true);
        Log::instance().addSink(std::make_shared<ConsoleSink>(LogLevel::Info));
    } else {
        Log::instance().addSink(
            std::make_shared<FileSink>(port::tempDir() + "/hci_gui.log"));
    }
    port::holdOrReleaseConsole();

    // QApplication must exist before loading qrc: resources.
    QApplication app(argc, argv);
#ifdef HCI_EMBED_PRODUCT
    Q_INIT_RESOURCE(hci_product);
#endif

    ProductConfig product;
    std::string productPath, flowFile;
    try {
        productPath = opt.productJson;
        bool fromQrc = productPath.rfind("qrc:", 0) == 0;
        if (!fromQrc) productPath = absolutize(productPath, port::currentDir());
        if (fromQrc) {
            std::string json;
            if (!gui::readResource(productPath, json)) {
                std::cerr << "Error: cannot read product resource: " << productPath << "\n";
                return 2;
            }
            product = ProductConfig::loadString(json);
        } else {
            product = ProductConfig::loadFile(productPath);
        }
        std::string base = fromQrc ? std::string("qrc:/") : dirOf(productPath);
        std::string f = opt.flow;
        if (f.empty() || f == "install") f = product.flows.install;
        else if (f == "uninstall") f = product.flows.uninstall;
        if (f.empty()) {
            std::cerr << "Error: no flow defined (--flow or product flows.install)\n";
            return 2;
        }
        if (f.rfind("qrc:", 0) == 0) {
            flowFile = f;
        } else if (fromQrc) {
            flowFile = base + f; // resolve relative flow names against qrc:/ 
        } else {
            flowFile = absolutize(f, base);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    if (port::hasConsole())
        std::cout << entry::renderBanner(product.productName, product.bannerFont);

    app.setApplicationName(QString::fromUtf8(product.productName.c_str()));
    app.setOrganizationName(QString::fromUtf8(product.orgName.c_str()));

    gui::GuiShell shell(product, flowFile, opt.installPath, opt.silent,
                        opt.extensionArgs);
    if (!opt.silent) shell.show(); // silent: headless run, no window
    return shell.run();
}