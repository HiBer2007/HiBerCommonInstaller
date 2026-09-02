// hci_git - generic Git strategy extension (id "hci.git").
//
// Reusable by any installer product that needs Git:
//   - CLI args: --use-system-git / --use-bundled-git / --install-system-git
//   - step "git_plan": decide system vs bundled vs install-system
//     (auto-detect + mode from the "git" ui step / args), write
//     gitUseSystem/gitDownload/gitInstallKind/gitVariant/gitInstallDir/
//     gitPath/gitPlanned. The bundled install location is configurable:
//       - step param "gitDir" (relative to installDir, default "tools/git")
//       - or product metadata: "extensions": {"hci.git": {"gitDir": "tools/git"}}
//   - step "git_refresh": re-detect system git (after a system install).
// Product metadata for this extension may also set "systemInstall": true to
// show the "install system Git" option by default.
//
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
        // Extension feature configuration from product metadata.
        const nlohmann::json& cfg = api.extensionConfig("hci.git");
        defaultGitDir_ = cfg.value("gitDir", "tools/git");

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
        api.registry().registerCliArg(
            "--install-system-git",
            [](const std::string&, hci::InstallContext& ctx) {
                ctx.vars().set("gitMode", "install-system");
                return true;
            },
            "download and install Git for Windows system-wide (admin)");

        api.registry().registerStep(
            "git_plan",
            [ this ](const nlohmann::json& params, hci::InstallContext& ctx,
                     std::string& error) {
                (void)error;
                hci::Vars& v = ctx.vars();
                std::string mode = v.get("gitMode");
                std::string ec = params.value("editorComponent", "editor");
                bool editor = v.getBool("components." + ec, false);
                std::string variant = editor ? "PortableGit" : "MinGit";
                if (params.contains("editorVariant") && editor)
                    variant = params.value("editorVariant", "PortableGit");

                // Bundled install location: step param > product metadata.
                std::string gitDir = params.value("gitDir", "");
                if (gitDir.empty()) gitDir = defaultGitDir_;
                if (gitDir.empty()) gitDir = "tools/git";
                v.set("gitInstallDir", "{installDir}/" + gitDir);
                v.set("gitPath", "{installDir}/" + gitDir + "/bin/git.exe");
                v.set("gitInstallKind", "");

                if (mode == "install-system") {
                    // Download the Git for Windows installer and install it
                    // system-wide (needs elevation; see git_refresh after).
                    v.setBool("gitUseSystem", true);
                    v.setBool("gitDownload", true);
                    v.set("gitInstallKind", "system");
                    v.set("gitVariant", params.value("installerVariant", "Git-"));
                    v.set("gitInstallerPath", "{tempDir}/git_installer.exe");
                    v.remove("gitPath"); // resolved by git_refresh after install
                } else if (mode == "system" || mode.empty() || mode == "auto") {
                    std::string sysPath;
                    bool found = findSystemGit(&sysPath);
                    if (mode == "system" && !found) {
                        // system requested but missing: fall back to bundled
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
                v.set("gitVariant", variant);
                v.setBool("gitPlanned", true);
                return true;
            });

        api.registry().registerStep(
            "git_refresh",
            [](const nlohmann::json&, hci::InstallContext& ctx, std::string&) {
                hci::Vars& v = ctx.vars();
                std::string sysPath;
                if (findSystemGit(&sysPath)) {
                    v.setBool("gitUseSystem", true);
                    v.set("gitSystemPath", sysPath);
                    v.set("gitPath", sysPath.empty() ? "git" : sysPath);
                }
                return true;
            });
        return true;
    }

    void shutdown() override {}

private:
    std::string defaultGitDir_;
};

} // namespace

HCI_REGISTER_EXTENSION(GitExtension);