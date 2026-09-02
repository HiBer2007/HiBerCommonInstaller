// hci::download - pluggable download backends + fallback chains.
//
// Backends (built-in: "direct" URL fetch, "github" release assets; host or
// extension backends e.g. "winget"/"apt" register via the flow extension
// registry) are tried in chain order until one supports and succeeds.
//
// A chain is a list of backend names (first match wins): default
// {"github", "direct"}. The flow download step exposes:
//   { "type": "download", "url": "...", "chain": ["direct","github"] }
//   { "type": "download", "asset": "...", "variant": "...", "chain": ["github"] }
//   { "type": "download", "package": "git", "backend": "winget" }

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace hci {
namespace download {

struct DownloadRequest {
    std::string url;      // direct URL
    std::string asset;    // GitHub repo "owner/name" (variant matches asset)
    std::string variant;  // asset name pattern
    bool allowExe = false; // match installer .exe assets too
    std::string package;  // package id (winget / apt backends)
    std::string dest;     // output file/directory (optional for installers)
};

struct DownloadResult {
    bool ok = false;
    std::string error;
    std::string backend; // backend that handled the request
};

using DownloadProgress = std::function<void(long long received, long long total)>;

class IDownloadBackend {
public:
    virtual ~IDownloadBackend() = default;
    virtual const char* name() const = 0;
    // True when this backend can handle the request (package/url/asset...).
    virtual bool supports(const DownloadRequest& req) const = 0;
    // Perform the download/install. Fills 'error' on failure.
    virtual bool fetch(DownloadRequest& req, DownloadProgress progress,
                       std::string& error) = 0;
};

// Built-in backends.
std::shared_ptr<IDownloadBackend> makeDirectBackend();
std::shared_ptr<IDownloadBackend> makeGitHubBackend();

// Run a chain of backends (first supporting+successful one wins).
// 'extra' = additional backends (host/extension supplied) consulted after
// the built-ins when their names match.
DownloadResult runChain(const DownloadRequest& req,
                        const std::vector<std::string>& chain,
                        const std::vector<std::shared_ptr<IDownloadBackend>>& extra,
                        DownloadProgress progress = nullptr);

} // namespace download
} // namespace hci