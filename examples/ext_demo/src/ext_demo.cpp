// hci_ext_demo - example extension (dual-loadable DLL, also usable as .hci
// package). Demonstrates: custom step type registration + product-specific
// CLI arg handler (--with-editor, the product-extension parameter model).

#include "hci/extension.h"

#include <string>

#ifdef _WIN32
#define HCI_EXT_EXPORT __declspec(dllexport)
#else
#define HCI_EXT_EXPORT __attribute__((visibility("default")))
#endif

namespace {

class DemoExtension : public hci::IHciExtension {
public:
    const char* id() const override { return "hci.demo"; }
    const char* version() const override { return "1.0.0"; }

    hci::HciCapabilities capabilities() const override
    {
        hci::HciCapabilities c;
        c.providesSteps = true;
        c.providesCliArgs = true;
        return c;
    }

    bool init(hci::HostApi& api) override
    {
        api.registry().registerStep(
            "demo_ping",
            [](const nlohmann::json& params, hci::InstallContext& ctx,
               std::string& err) {
                (void)err;
                ctx.vars().set("demoPing", "true");
                std::string note = params.value("note", "");
                if (!note.empty()) ctx.vars().set("demoPingNote", note);
                return true;
            });

        api.registry().registerCliArg(
            "--with-editor",
            [](const std::string&, hci::InstallContext& ctx) {
                ctx.vars().setBool("components.editor", true);
                return true;
            });
        return true;
    }

    void shutdown() override {}
};

} // namespace

extern "C" HCI_EXT_EXPORT hci::IHciExtension* HciGetExtension()
{
    return new DemoExtension();
}