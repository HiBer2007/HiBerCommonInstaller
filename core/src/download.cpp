// hci::download implementation (built-in backends + chain runner).

#include "hci/download.h"

#include "hci/exec.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>

namespace hci {
namespace download {

namespace fs = std::filesystem;

// ------------------------------------------------------------------
// Direct URL backend (cpr downloadFile).
// ------------------------------------------------------------------
class DirectBackend : public IDownloadBackend {
public:
    const char* name() const override { return "direct"; }
    bool supports(const DownloadRequest& req) const override
    {
        return !req.url.empty();
    }
    bool fetch(DownloadRequest& req, DownloadProgress progress,
               std::string& error) override
    {
        return exec::downloadFile(req.url, req.dest, progress, &error);
    }
};

// ------------------------------------------------------------------
// GitHub release asset backend (API + asset download).
// ------------------------------------------------------------------
class GitHubBackend : public IDownloadBackend {
public:
    const char* name() const override { return "github"; }
    bool supports(const DownloadRequest& req) const override
    {
        return !req.asset.empty() && !req.variant.empty();
    }
    bool fetch(DownloadRequest& req, DownloadProgress progress,
               std::string& error) override
    {
        std::string url;
        try {
            cpr::Response r = cpr::Get(
                cpr::Url{"https://api.github.com/repos/" + req.asset +
                         "/releases/latest"},
                cpr::Header{{"Accept", "application/vnd.github+json"},
                            {"User-Agent", "hci/1.0"},
                            {"X-GitHub-Api-Version", "2022-11-28"}},
                cpr::Timeout{15000});
            if (r.status_code != 200) {
                error = "GitHub API HTTP " + std::to_string(r.status_code);
                return false;
            }
            nlohmann::json j = nlohmann::json::parse(r.text, nullptr, false);
            if (j.is_discarded() || !j.contains("assets")) {
                error = "GitHub API: unexpected payload";
                return false;
            }
            std::string upPattern = req.variant;
            for (auto& c : upPattern)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            for (auto& a : j["assets"]) {
                std::string name = a.value("name", "");
                std::string upName = name;
                for (auto& c : upName)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (upName.find(upPattern) == std::string::npos) continue;
                if (upName.find("64-BIT") == std::string::npos) continue;
                bool zipMatch = upName.rfind(".ZIP") == upName.size() - 4 ||
                                upName.rfind(".7Z.EXE") == upName.size() - 7;
                bool exeMatch = req.allowExe &&
                                upName.rfind(".EXE") == upName.size() - 4 &&
                                upName.rfind(".7Z.EXE") != upName.size() - 7;
                if (zipMatch || exeMatch) {
                    url = a.value("browser_download_url", "");
                    break;
                }
            }
            if (url.empty()) {
                error = "GitHub API: no matching asset for '" + req.variant + "'";
                return false;
            }
        } catch (const std::exception& e) {
            error = std::string("GitHub API request failed: ") + e.what();
            return false;
        }
        return exec::downloadFile(url, req.dest, progress, &error);
    }
};

std::shared_ptr<IDownloadBackend> makeDirectBackend()
{
    return std::make_shared<DirectBackend>();
}

std::shared_ptr<IDownloadBackend> makeGitHubBackend()
{
    return std::make_shared<GitHubBackend>();
}

// ------------------------------------------------------------------
// Chain runner: built-in backends + extra (host/extension) backends.
// ------------------------------------------------------------------
DownloadResult runChain(const DownloadRequest& req,
                        const std::vector<std::string>& chain,
                        const std::vector<std::shared_ptr<IDownloadBackend>>& extra,
                        DownloadProgress progress)
{
    std::vector<std::pair<std::string, std::shared_ptr<IDownloadBackend>>> pool;
    pool.emplace_back("direct", makeDirectBackend());
    pool.emplace_back("github", makeGitHubBackend());
    for (auto& b : extra) {
        if (b) pool.emplace_back(b->name(), b);
    }

    std::string lastErr;
    for (const auto& name : chain) {
        const std::shared_ptr<IDownloadBackend>* found = nullptr;
        for (auto& p : pool) {
            if (p.first == name) { found = &p.second; break; }
        }
        if (!found) {
            lastErr = "unknown download backend: " + name;
            continue;
        }
        if (!(*found)->supports(req)) continue; // not applicable, try next
        DownloadResult res;
        res.backend = name;
        if ((*found)->fetch(const_cast<DownloadRequest&>(req), progress, res.error)) {
            res.ok = true;
            return res;
        }
        lastErr = res.error;
    }
    DownloadResult fail;
    fail.error = lastErr.empty() ? "no download backend matched the request"
                                 : lastErr;
    return fail;
}

} // namespace download
} // namespace hci