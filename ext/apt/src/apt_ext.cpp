// hci_apt - "apt" download/install backend extension (POSIX/Linux).
//
// Registers a download backend usable from flow chains:
//   { "type": "download", "package": "git", "backend": "apt" }
// Runs `apt-get install -y <pkg>` (requires root; run the installer
// elevated on Linux). Register via HCI_REGISTER_EXTENSION.

#include "hci/download.h"
#include "hci/exec.h"
#include "hci/extension.h"

namespace {

class AptBackend : public hci::download::IDownloadBackend {
public:
    const char* name() const override { return "apt"; }
    bool supports(const hci::download::DownloadRequest& req) const override
    {
#ifndef _WIN32
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
        if (!hci::exec::runProcess(
                {"apt-get", "install", "-y", req.package}, 300000, r)) {
            error = "apt-get failed to start";
            return false;
        }
        if (r.exitCode != 0) {
            if (!r.output.empty())
                error = r.output.substr(0, r.output.size() < 300
                                            ? r.output.size() : 300);
            else
                error = "apt-get install failed (exit " +
                        std::to_string(r.exitCode) + ")";
            return false;
        }
        return true;
    }
};

class AptExtension : public hci::IHciExtension {
public:
    const char* id() const override { return "hci.apt"; }
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
            "apt", []() -> std::shared_ptr<hci::download::IDownloadBackend> {
                return std::make_shared<AptBackend>();
            });
        return true;
    }
    void shutdown() override {}
};

} // namespace

HCI_REGISTER_EXTENSION(AptExtension);