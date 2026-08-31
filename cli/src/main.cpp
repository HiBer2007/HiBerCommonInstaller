// hci_cli - CLI shell (console subsystem, zero Qt).
//
// Core args (extension args arrive in M2 via extension cliArgs handlers):
//   --gui | --tui | --cli     explicit shell mode
//   --silent, -s              non-interactive: default answers
//   --path <dir>              install path override
//   --product <file.json>     product config (default: ./product.json)
//   --flow <install|uninstall|file.json>
//   --json                    JSON protocol blocks on stdout
//   --verbose                 debug logging
//   --extensions <dir>        extension directory (dll / .hci)
//   --help, -h / --version, -v
//
// Exit codes: 0 success, 1 failure, 2 usage error.

#include "hci/entry.h"
#include "hci/exec.h"
#include "hci/extension.h"
#include "hci/flow.h"
#include "hci/log.h"
#include "hci/port.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

using namespace hci;

namespace {

struct CliOptions {
    std::string mode;          // gui | tui | cli | ""
    bool silent = false;
    bool help = false;
    bool version = false;
    std::string installPath;
    std::string productJson = "product.json";
    std::string flow;          // "" -> install
    bool jsonOut = false;
    bool verbose = false;
    std::string extensionsDir;
    std::vector<std::string> extensionArgs; // unknown args -> extension handlers
};

void printUsage(std::ostream& os)
{
    os << "HiBer Common Installer Module - CLI shell\n"
          "Usage:\n"
          "  hci_cli [options]\n\n"
          "Options:\n"
          "  --gui | --tui | --cli     shell mode (default: product defaultMode)\n"
          "  --silent, -s              non-interactive (default answers)\n"
          "  --path <dir>              install path override\n"
          "  --product <file.json>     product config (default ./product.json)\n"
          "  --flow <id|file.json>     flow: install | uninstall | path\n"
          "  --json                    JSON protocol blocks on stdout\n"
          "  --verbose                 debug logging\n"
          "  --extensions <dir>        load extensions from directory\n"
          "  --help, -h                show this help\n"
          "  --version, -v             version\n\n"
          "Exit codes: 0 success, 1 failure, 2 usage error.\n";
}

bool parseArgs(int argc, char** argv, CliOptions& o, std::string& err)
{
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        if (a == "--version" || a == "-v") { o.version = true; continue; }
        if (a == "--gui") { o.mode = "gui"; continue; }
        if (a == "--tui") { o.mode = "tui"; continue; }
        if (a == "--cli") { o.mode = "cli"; continue; }
        if (a == "--silent" || a == "-s") { o.silent = true; continue; }
        if (a == "--json") { o.jsonOut = true; continue; }
        if (a == "--verbose") { o.verbose = true; continue; }
        if (a == "--path" && i + 1 < argc) { o.installPath = argv[++i]; continue; }
        if (a == "--product" && i + 1 < argc) { o.productJson = argv[++i]; continue; }
        if (a == "--flow" && i + 1 < argc) { o.flow = argv[++i]; continue; }
        if (a == "--extensions" && i + 1 < argc) { o.extensionsDir = argv[++i]; continue; }
        // Unknown args are routed to extension cliArgs handlers (M2).
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

// ------------------------------------------------------------------
// CLI UI provider
// ------------------------------------------------------------------
class CliUi : public IFlowUi {
public:
    CliUi(bool silent, bool jsonOut, std::ostream& out)
        : silent_(silent), jsonOut_(jsonOut), out_(out) {}

    void jsonEvent(const nlohmann::json& j)
    {
        if (!jsonOut_) return;
        out_ << "=====JSON-BEGIN=====\n" << j.dump() << "\n=====JSON-END=====\n";
    }

    bool onWelcome(const std::string& productName, const ProductConfig&) override
    {
        if (!jsonOut_) out_ << "Product: " << productName << "\n";
        return true;
    }

    bool onLicense(const std::string& text, bool& accepted) override
    {
        if (silent_) { accepted = true; return true; }
        out_ << "--- License ---\n" << text << "\n";
        out_ << "Accept the license? [y/N] ";
        std::string line;
        std::getline(std::cin, line);
        accepted = (line == "y" || line == "Y" || line == "yes");
        return true;
    }

    bool onPath(std::string& path, const std::string& defaultPath) override
    {
        if (silent_ || !path.empty()) return true; // use --path / default
        out_ << "Install path [" << defaultPath << "]: ";
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) path = line;
        else path = defaultPath;
        return true;
    }

    bool onComponents(const std::vector<ProductComponent>& components,
                      std::vector<bool>& checked) override
    {
        if (silent_) return true; // defaults
        for (size_t i = 0; i < components.size(); ++i) {
            out_ << (checked[i] ? "[x] " : "[ ] ") << components[i].label
                 << " (" << components[i].id << ") [y/N] ";
            std::string line;
            std::getline(std::cin, line);
            if (line == "y" || line == "Y" || line == "yes" || line == "1")
                checked[i] = true;
            else if (line == "n" || line == "N" || line == "no" || line == "0")
                checked[i] = false;
        }
        return true;
    }

    bool onOption(const std::string& prompt,
                  const std::vector<std::string>& choices, int& selected) override
    {
        if (silent_) return true;
        out_ << prompt << "\n";
        for (size_t i = 0; i < choices.size(); ++i)
            out_ << "  [" << i << "] " << choices[i] << "\n";
        out_ << "> ";
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) {
            try { selected = std::stoi(line); } catch (...) {}
        }
        return true;
    }

    bool onConfirm(const std::string& prompt, bool& yes) override
    {
        if (silent_) return true; // default yes
        out_ << prompt << " [Y/n] ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "n" || line == "N" || line == "no") yes = false;
        return true;
    }

    bool onInput(const std::string& prompt, std::string& value, bool required) override
    {
        if (silent_) {
            if (required) return false; // no default available
            return true;
        }
        out_ << prompt << ": ";
        std::getline(std::cin, value);
        return !(required && value.empty());
    }

    void onProgress(const std::string& step, int percent, const std::string& detail) override
    {
        if (jsonOut_) {
            jsonEvent({{"category", "hci"}, {"event", "progress"},
                       {"step", step}, {"percent", percent}, {"detail", detail}});
        } else if (!detail.empty()) {
            out_ << "[step " << step << "] " << percent << "% " << detail << "\n";
        }
    }

    void onMessage(const std::string& text, bool isError) override
    {
        if (jsonOut_) {
            jsonEvent({{"category", "hci"}, {"event", "message"},
                       {"error", isError}, {"text", text}});
        } else if (isError) {
            std::cerr << "[ERROR] " << text << "\n";
        } else {
            out_ << text << "\n";
        }
    }

    void onFinish(bool success, const std::string& message, const std::string& launch) override
    {
        if (jsonOut_) {
            jsonEvent({{"category", "hci"}, {"event", "finish"},
                       {"success", success}, {"message", message}, {"launch", launch}});
        } else {
            out_ << (success ? "[OK] " : "[FAIL] ") << message << "\n";
            if (success && !launch.empty()) out_ << "Launch: " << launch << "\n";
        }
    }

private:
    bool silent_;
    bool jsonOut_;
    std::ostream& out_;
};

} // namespace

int main(int argc, char* argv[])
{
    CliOptions opt;
    std::string err;
    if (!parseArgs(argc, argv, opt, err)) {
        std::cerr << "Error: " << err << "\n\n";
        printUsage(std::cerr);
        return 2;
    }

    if (opt.help) {
        port::setUtf8Console(true);
        printUsage(std::cout);
        return 0;
    }
    if (opt.version) {
        port::setUtf8Console(true);
        std::cout << "HiBer Common Installer Module v" HCI_VERSION_STRING " (hci_cli)\n";
        return 0;
    }

    if (opt.mode == "gui" || opt.mode == "tui") {
        std::cerr << "Error: '" << opt.mode
                  << "' shell is not built in this milestone (hci_cli only)\n";
        return 2;
    }

    // Logging: console when attached, file otherwise (CreateProcessA scenario).
    if (port::hasConsole()) {
        port::setUtf8Console(true);
        Log::instance().addSink(std::make_shared<ConsoleSink>(
            opt.verbose ? LogLevel::Debug : LogLevel::Info));
    } else {
        std::string logPath = port::tempDir() + "/hci_cli.log";
        Log::instance().addSink(std::make_shared<FileSink>(logPath));
    }

    // Product config.
    ProductConfig product;
    std::string productPath;
    try {
        productPath = absolutize(opt.productJson, port::currentDir());
        product = ProductConfig::loadFile(productPath);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    // Banner: product name + mandatory powered-by (stderr when --json).
    std::ostream& bannerOut = opt.jsonOut ? std::cerr : std::cout;
    bannerOut << entry::renderBanner(product.productName, product.bannerFont);

    // Resolve flow.
    std::string flowFile;
    {
        std::string base = dirOf(productPath);
        std::string f = opt.flow;
        if (f.empty() || f == "install") f = product.flows.install;
        else if (f == "uninstall") f = product.flows.uninstall;
        if (f.empty()) {
            std::cerr << "Error: no flow defined (--flow or product flows.install)\n";
            return 2;
        }
        flowFile = absolutize(f, base);
    }

    // Context + engine + extensions.
    InstallContext ctx;
    if (!opt.installPath.empty()) ctx.vars().set("installDir", opt.installPath);
    ctx.vars().setBool("silent", opt.silent);

    auto script = createLuaEngine();
    EventBus bus;
    ServiceRegistry services;
    ExtensionRegistry registry; // host-owned; shared with loader + runner

    ExtensionLoader loader(&bus, &services, &ctx, &product, &registry);
    loader.loadStatic();
    std::string extDir = !opt.extensionsDir.empty()
        ? opt.extensionsDir
        : port::joinPath(port::exeDir(), "extensions");
    if (fs::exists(fs::u8path(extDir))) loader.loadDirectory(extDir);

    // Route unknown args to extension cliArgs handlers; reject unhandled ones.
    for (auto& a : opt.extensionArgs) {
        if (registry.hasCliArg(a)) {
            std::string argErr;
            if (!registry.handleCliArg(a, ctx)) {
                std::cerr << "Error: extension rejected argument: " << a << "\n";
                return 2;
            }
        } else {
            std::cerr << "Error: unknown option: " << a << "\n\n";
            printUsage(std::cerr);
            return 2;
        }
    }

    FlowSpec flow;
    try {
        flow = FlowSpec::loadFile(flowFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    CliUi ui(opt.silent, opt.jsonOut, std::cout);
    FlowRunner runner(product, ctx, &ui, script.get());
    runner.setEventBus(&bus);
    runner.setRegistry(&registry);
    runner.setBaseDir(dirOf(flowFile));

    int rc = runner.run(flow);
    loader.shutdownAll();
    return rc;
}