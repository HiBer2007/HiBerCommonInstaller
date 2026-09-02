#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hci/bus.h"
#include "hci/context.h"
#include "hci/extension_registry.h"
#include "hci/log.h"
#include "hci/product.h"

namespace hci {

// Declarative capability bits of an extension.
struct HciCapabilities {
    bool providesSteps = false;      // custom step types
    bool providesPages = false;      // GUI page factories (M4)
    bool providesTuiPanels = false;  // TUI panel factories (M5)
    bool providesCliCommands = false;
    bool providesCliArgs = false;    // product-specific CLI arguments (e.g. --with-editor)
    bool providesServices = false;
};

// HostApi: the surface an extension reaches (bus / services / context /
// product / logging / registries). Extensions call api.* during init() and
// at runtime.
class HostApi {
public:
    HostApi(EventBus* bus, ServiceRegistry* services, InstallContext* ctx,
            const ProductConfig* product, ExtensionRegistry* registry)
        : bus_(bus), services_(services), ctx_(ctx), product_(product),
          registry_(registry)
    {
        // Pre-split product "extensions" metadata by extension id.
        if (product_ && product_->extensionConfig.is_object()) {
            for (auto it = product_->extensionConfig.begin();
                 it != product_->extensionConfig.end(); ++it) {
                extConfig_[it.key()] = it.value();
            }
        }
    }

    EventBus& bus() { return *bus_; }
    ServiceRegistry& services() { return *services_; }
    InstallContext& context() { return *ctx_; }
    const ProductConfig& product() const { return *product_; }

    // Extension feature configuration from the product metadata:
    // product.json "extensions": { "<this id>": { ... } }. Returns an
    // empty object when the product did not configure this extension.
    const nlohmann::json& extensionConfig(const std::string& id) const
    {
        auto it = extConfig_.find(id);
        if (it == extConfig_.end()) return emptyJson_;
        return it->second;
    }

    void log(LogLevel lv, const std::string& msg) const { Log::instance().write(lv, msg); }

    // Extension-facing registries (step types / cli args). When the host
    // injected a registry, extensions register into THAT instance (the static
    // fallback exists only for library-mode convenience).
    ExtensionRegistry& registry()
    {
        return registry_ ? *registry_ : ExtensionRegistry::instance();
    }
    bool hasRegistry() const { return registry_ != nullptr; }

private:
    EventBus* bus_;
    ServiceRegistry* services_;
    InstallContext* ctx_;
    const ProductConfig* product_;
    ExtensionRegistry* registry_;
    std::map<std::string, nlohmann::json> extConfig_;
    nlohmann::json emptyJson_;
};

class IHciExtension {
public:
    virtual ~IHciExtension() = default;

    virtual const char* id() const = 0;
    virtual const char* version() const = 0;
    virtual HciCapabilities capabilities() const = 0;

    // Called after loading; register step handlers / services / arg handlers here.
    virtual bool init(HostApi& api) = 0;
    virtual void shutdown() = 0;
};

// ------------------------------------------------------------------
// Static (link-time) extension registry.
// Extensions compiled into the binary register themselves via
// HCI_REGISTER_EXTENSION(Class); the host loads them with
// ExtensionLoader::loadStatic().
// ------------------------------------------------------------------
using ExtensionFactory = std::function<IHciExtension*()>;

class StaticExtensions {
public:
    static StaticExtensions& instance();

    void add(const char* className, ExtensionFactory factory);
    const std::vector<std::pair<std::string, ExtensionFactory>>& all() const;

private:
    StaticExtensions() = default;
    std::vector<std::pair<std::string, ExtensionFactory>> factories_;
};

#define HCI_REGISTER_EXTENSION_IMPL(cls, uid)                                   \
    static const bool hciExtRegistered_##uid =                                  \
        (hci::StaticExtensions::instance().add(                                 \
             #cls, []() -> hci::IHciExtension* { return new cls(); }),          \
         true)
#define HCI_REGISTER_EXTENSION(cls) HCI_REGISTER_EXTENSION_IMPL(cls, __COUNTER__)

// Extensions in a shared library / package must export this symbol
// (__declspec(dllexport) is REQUIRED - see plugin export lessons):
//   extern "C" __declspec(dllexport) hci::IHciExtension* HciGetExtension();
// Drop-in DLLs may ship a <name>.meta.json next to them; packages (.hci)
// embed meta.json + the dll + assets (ZIP container).

// ------------------------------------------------------------------
// ExtensionLoader: three load modes.
//   1. loadStatic()      - HCI_REGISTER_EXTENSION registrations (link-time)
//   2. loadDirectory()   - *.dll drop-ins (HciGetExtension export) + *.hci
//                          packages (ZIP: meta.json + dll + assets, sha256
//                          verified, extracted to %LOCALAPPDATA%/hci/ext-cache)
// ------------------------------------------------------------------
class ExtensionLoader {
public:
    ExtensionLoader(EventBus* bus, ServiceRegistry* services, InstallContext* ctx,
                    const ProductConfig* product, ExtensionRegistry* registry = nullptr);
    ~ExtensionLoader();

    void loadStatic();
    void loadDirectory(const std::string& dir);
    void shutdownAll();

    size_t count() const { return loaded_.size(); }
    const std::string& lastError() const { return lastError_; }

    // Loaded modules (id, version) - for help/metadata display.
    std::vector<std::pair<std::string, std::string>> modules() const;

private:
    bool loadDll(const std::string& dllPath, std::string& err);
    bool loadPackage(const std::string& pkgPath, std::string& err);

    HostApi api_;
    std::vector<std::shared_ptr<IHciExtension>> loaded_;
    std::string lastError_;
};

} // namespace hci