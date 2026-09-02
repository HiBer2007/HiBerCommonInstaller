#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "hci/context.h"

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

    void clear();

private:
    struct ArgEntry {
        CliArgHandler handler;
        std::string help;
    };
    std::map<std::string, StepHandler> steps_;
    std::map<std::string, ArgEntry> args_;
};

} // namespace hci