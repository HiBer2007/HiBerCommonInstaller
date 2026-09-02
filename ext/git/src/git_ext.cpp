// hci_git - generic Git strategy extension (id "hci.git").
//
// Reusable by any installer product that needs Git:
//   - CLI args: --use-system-git / --use-bundled-git
//   - step "git_plan": decide system vs bundled (auto-detect + mode from the
//     "git" ui step / args), write gitUseSystem/gitDownload/gitVariant/
//     gitPath/gitPlanned; "editorComponent" param switches the bundled
//     variant to PortableGit when that component is selected.
// Register via HCI_REGISTER_EXTENSION (static link or DLL hosting).

#include "hci/exec.h"
#include "hci/extension.h"

#include <filesystem>

namespace {

using hci::exec::findSystemGit;

class GitExtension : public hci::IHciExtension {
public:
    const char* id() const override { return "hci.git"; }
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
        api.registry().registerCliArg(
            "--use-system-git",
            [](const std::string&, hci::InstallContext& ctx) {
                ctx.vars().set("gitMode", "system");
                return true;
            },
            "force using a system-installed Git");
        api.registry().registerCliArg(
            "--use-bundled-git",
            [](const std::string&, hci::InstallContext& ctx) {
                ctx.vars().set("gitMode", "bundled");
                return true;
            },
            "force downloading and using the bundled Git");

        api.registry().registerStep(
            "git_plan",
            [](const nlohmann::json& params, hci::InstallContext& ctx,
               std::string& error) {
                (void)error;
                hci::Vars& v = ctx.vars();
                std::string mode = v.get("gitMode");
                std::string ec = params.value("editorComponent", "editor");
                bool editor = v.getBool("components." + ec, false);
                std::string variant = editor ? "PortableGit" : "MinGit";
                if (params.contains("editorVariant") && editor)
                    variant = params.value("editorVariant", "PortableGit");

                std::string sysPath;
                if (mode == "system" || (mode.empty() || mode == "auto")) {
                    bool found = findSystemGit(&sysPath);
                    if (mode == "system" && !found) {
                        // fall back to bundled (documented system semantics)
                        v.setBool("gitUseSystem", false);
                        v.setBool("gitDownload", true);
                    } else if (found) {
                        v.setBool("gitUseSystem", true);
                        v.setBool("gitDownload", false);
                        v.set("gitSystemPath", sysPath);
                        v.set("gitPath", sysPath.empty() ? "git" : sysPath);
                    } else {
                        v.setBool("gitUseSystem", false);
                        v.setBool("gitDownload", true);
                    }
                } else { // "bundled"
                    v.setBool("gitUseSystem", false);
                    v.setBool("gitDownload", true);
                }
                if (!v.has("gitPath"))
                    v.set("gitPath", "{installDir}/tools/git/bin/git.exe");
                v.set("gitVariant", variant);
                v.setBool("gitPlanned", true);
                return true;
            });
        return true;
    }

    void shutdown() override {}
};

} // namespace

HCI_REGISTER_EXTENSION(GitExtension);