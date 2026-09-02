#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hci/context.h"
#include "hci/download.h"

namespace hci {

// ------------------------------------------------------------------
// Extension-facing registries (per-process, owned by the core).
// Extensions register custom step types and product-specific CLI args
// from their init(); the flow runner and shells consult these.
// ------------------------------------------------------------------
using StepHandler = std::function<bool(const nlohmann::json& params,
                                       InstallContext& ctx,
                                       std::string& error)>;
using CliArgHandler = std::function<bool(const std::string& arg,
                                         InstallContext& ctx)>;

class ExtensionRegistry {
public:
    static ExtensionRegistry& instance();

    void registerStep(const std::string& type, StepHandler handler);
    bool runStep(const std::string& type, const nlohmann::json& params,
                 InstallContext& ctx, std::string& error) const;

    // help text surfaces in the shells' --help output (extension metadata).
    void registerCliArg(const std::string& arg, CliArgHandler handler,
                        const std::string& help = "");
    bool handleCliArg(const std::string& arg, InstallContext& ctx) const;
    bool hasCliArg(const std::string& arg) const;
    std::vector<std::string> cliArgs() const;
    std::string cliArgHelp(const std::string& arg) const;

    // Download backend factories (winget/apt/custom): extensions register their
// backend here during init(); the flow download step consults the injected
// registry the same way as step types/cli args (cross-DLL safe).
using DownloadBackendFactory =
    std::function<std::shared_ptr<hci::download::IDownloadBackend>()>;
void registerDownloadBackend(const std::string& name,
                             DownloadBackendFactory factory);
std::vector<std::string> downloadBackends() const;
std::shared_ptr<hci::download::IDownloadBackend>
    createDownloadBackend(const std::string& name) const;

void clear();

private:
    struct ArgEntry {
        CliArgHandler handler;
        std::string help;
    };
    std::map<std::string, StepHandler> steps_;
    std::map<std::string, ArgEntry> args_;
    std::map<std::string, DownloadBackendFactory> downloadBackends_;
};

} // namespace hci