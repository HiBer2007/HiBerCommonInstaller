// hci_winget - "winget" download backend (Windows Package Manager).
//
// Registers a download backend usable from flow chains:
//   { "type": "download", "package": "Git.Git", "backend": "winget" }
//   { "type": "download", "package": "Git.Git",
//     "chain": ["winget", "github"] }
// The backend runs `winget install` silently (id match, agreements
// accepted). Register via HCI_REGISTER_EXTENSION (static or DLL).

#include "hci/download.h"
#include "hci/exec.h"
#include "hci/extension.h"

namespace {

class WingetBackend : public hci::download::IDownloadBackend {
public:
    const char* name() const override { return "winget"; }
    bool supports(const hci::download::DownloadRequest& req) const override
    {
#ifdef _WIN32
        return !req.package.empty();
#else
        (void)req;
        return false;
#endif
    }
    bool fetch(hci::download::DownloadRequest& req,
               hci::download::DownloadProgress,
               std::string& error) override
    {
        hci::exec::ProcessResult r;
        // winget install --exact --id <pkg> --silent
        //   --accept-package-agreements --accept-source-agreements
        if (!hci::exec::runProcess(
                {"winget", "install", "--exact", "--id", req.package,
                 "--silent", "--accept-package-agreements",
                 "--accept-source-agreements"},
                300000, r)) {
            error = "winget failed to start";
            return false;
        }
        if (r.exitCode != 0) {
            if (!r.output.empty())
                error = r.output.substr(0, r.output.size() < 300
                                            ? r.output.size() : 300);
            else
                error = "winget install failed (exit " +
                        std::to_string(r.exitCode) + ")";
            return false;
        }
        return true;
    }
};

class WingetExtension : public hci::IHciExtension {
public:
    const char* id() const override { return "hci.winget"; }
    const char* version() const override { return "1.0.0"; }
    hci::HciCapabilities capabilities() const override
    {
        hci::HciCapabilities c;
        c.providesServices = true; // registers a download backend
        return c;
    }
    bool init(hci::HostApi& api) override
    {
        api.registry().registerDownloadBackend(
            "winget", []() -> std::shared_ptr<hci::download::IDownloadBackend> {
                return std::make_shared<WingetBackend>();
            });
        return true;
    }
    void shutdown() override {}
};

} // namespace

HCI_REGISTER_EXTENSION(WingetExtension);