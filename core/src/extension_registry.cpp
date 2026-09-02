#include "hci/extension_registry.h"

namespace hci {

ExtensionRegistry& ExtensionRegistry::instance()
{
    static ExtensionRegistry reg;
    return reg;
}

void ExtensionRegistry::registerStep(const std::string& type, StepHandler handler)
{
    steps_[type] = std::move(handler);
}

bool ExtensionRegistry::runStep(const std::string& type, const nlohmann::json& params,
                                InstallContext& ctx, std::string& error) const
{
    auto it = steps_.find(type);
    if (it == steps_.end()) return false; // not a registered type
    return it->second(params, ctx, error);
}

void ExtensionRegistry::registerCliArg(const std::string& arg, CliArgHandler handler,
                                       const std::string& help)
{
    args_[arg] = {std::move(handler), help};
}

bool ExtensionRegistry::handleCliArg(const std::string& arg, InstallContext& ctx) const
{
    auto it = args_.find(arg);
    if (it == args_.end()) return false;
    return it->second.handler(arg, ctx);
}

bool ExtensionRegistry::hasCliArg(const std::string& arg) const
{
    return args_.find(arg) != args_.end();
}

std::vector<std::string> ExtensionRegistry::cliArgs() const
{
    std::vector<std::string> out;
    out.reserve(args_.size());
    for (auto& kv : args_) out.push_back(kv.first);
    return out;
}

std::string ExtensionRegistry::cliArgHelp(const std::string& arg) const
{
    auto it = args_.find(arg);
    if (it == args_.end()) return "";
    return it->second.help;
}

void ExtensionRegistry::registerDownloadBackend(const std::string& name,
                                                DownloadBackendFactory factory)
{
    downloadBackends_[name] = std::move(factory);
}

std::vector<std::string> ExtensionRegistry::downloadBackends() const
{
    std::vector<std::string> out;
    out.reserve(downloadBackends_.size());
    for (auto& kv : downloadBackends_) out.push_back(kv.first);
    return out;
}

std::shared_ptr<hci::download::IDownloadBackend>
ExtensionRegistry::createDownloadBackend(const std::string& name) const
{
    auto it = downloadBackends_.find(name);
    if (it == downloadBackends_.end()) return nullptr;
    return it->second();
}

void ExtensionRegistry::clear()
{
    steps_.clear();
    args_.clear();
    downloadBackends_.clear();
}

} // namespace hci