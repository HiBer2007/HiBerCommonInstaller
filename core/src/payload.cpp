#include "hci/payload.h"

#include <libzippp.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace hci {

// ------------------------------------------------------------------
// Wildcard match ('*' matches any run, '?' matches one char)
// ------------------------------------------------------------------
bool wildcardMatch(const std::string& pattern, const std::string& text)
{
    size_t p = 0, t = 0;
    size_t starP = std::string::npos, starT = 0;
    while (t < text.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starT = t;
        } else if (starP != std::string::npos) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// ------------------------------------------------------------------
// Directory source
// ------------------------------------------------------------------
class DirSource : public IDeploySource {
public:
    explicit DirSource(const std::string& root) : root_(root) { kind_ = "dir"; }

    bool open(std::string& error) override
    {
        std::error_code ec;
        if (!fs::is_directory(fs::u8path(root_), ec)) {
            error = "directory not found: " + root_;
            return false;
        }
        return true;
    }

    bool enumerate(std::vector<DeployEntry>& entries, std::string& error) override
    {
        std::error_code ec;
        fs::recursive_directory_iterator it(fs::u8path(root_), fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) { error = ec.message(); return false; }
            if (!it->is_regular_file(ec) && !it->is_directory(ec)) continue;
            fs::path rel = fs::relative(it->path(), fs::u8path(root_), ec);
            if (ec) continue;
            DeployEntry de;
            de.relPath = rel.generic_u8string();
            de.isDir = it->is_directory(ec);
            if (!de.isDir) de.size = static_cast<long long>(it->file_size(ec));
            entries.push_back(std::move(de));
        }
        return true;
    }

    bool readFile(const std::string& relPath, std::vector<char>& out,
                  std::string& error) override
    {
        fs::path full = fs::u8path(root_) / fs::u8path(relPath);
        std::ifstream f(full, std::ios::binary);
        if (!f) { error = "cannot open: " + relPath; return false; }
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return true;
    }

private:
    std::string root_;
};

// ------------------------------------------------------------------
// ZIP source (libzippp 7.x API)
// ------------------------------------------------------------------
class ZipSource : public IDeploySource {
public:
    explicit ZipSource(const std::string& zipPath) : zipPath_(zipPath) { kind_ = "zip"; }

    bool open(std::string& error) override
    {
        std::error_code ec;
        if (!fs::is_regular_file(fs::u8path(zipPath_), ec)) {
            error = "zip file not found: " + zipPath_;
            return false;
        }
        libzippp::ZipArchive za(zipPath_);
        if (!za.open(libzippp::ZipArchive::ReadOnly)) {
            error = "cannot open zip: " + zipPath_;
            return false;
        }
        za.close();
        return true;
    }

    bool enumerate(std::vector<DeployEntry>& entries, std::string& error) override
    {
        libzippp::ZipArchive za(zipPath_);
        if (!za.open(libzippp::ZipArchive::ReadOnly)) {
            error = "cannot open zip: " + zipPath_;
            return false;
        }
        std::vector<libzippp::ZipEntry> zipEntries = za.getEntries();
        entries.reserve(zipEntries.size());
        for (auto& e : zipEntries) {
            DeployEntry de;
            de.relPath = e.getName();
            de.isDir = e.isDirectory();
            de.size = static_cast<long long>(e.getSize());
            entries.push_back(std::move(de));
        }
        za.close();
        return true;
    }

    bool readFile(const std::string& relPath, std::vector<char>& out,
                  std::string& error) override
    {
        libzippp::ZipArchive za(zipPath_);
        if (!za.open(libzippp::ZipArchive::ReadOnly)) {
            error = "cannot open zip: " + zipPath_;
            return false;
        }
        libzippp::ZipEntry e = za.getEntry(relPath);
        if (e.isNull() || e.isDirectory()) {
            error = "entry not found in zip: " + relPath;
            za.close();
            return false;
        }
        std::ostringstream os;
        int rc = e.readContent(os);
        if (rc < 0) {
            error = "read failed (zip): " + relPath;
            za.close();
            return false;
        }
        std::string s = os.str();
        out.assign(s.begin(), s.end());
        za.close();
        return true;
    }

private:
    std::string zipPath_;
};

// ------------------------------------------------------------------
namespace {
std::map<std::string, DeploySourceFactory>& deployFactories()
{
    static std::map<std::string, DeploySourceFactory> f;
    return f;
}
} // namespace

void registerDeploySourceFactory(const std::string& kind, DeploySourceFactory factory)
{
    deployFactories()[kind] = std::move(factory);
}

std::shared_ptr<IDeploySource> makeDirSource(const std::string& rootPath)
{
    return std::make_shared<DirSource>(rootPath);
}

std::shared_ptr<IDeploySource> makeZipSource(const std::string& zipPath)
{
    return std::make_shared<ZipSource>(zipPath);
}

std::shared_ptr<IDeploySource> makeDeploySource(const DeploySpec& spec,
                                                std::string& error)
{
    const std::string& s = spec.source;
    if (s.rfind("dir:", 0) == 0)
        return makeDirSource(s.substr(4));
    if (s.rfind("zip:", 0) == 0)
        return makeZipSource(s.substr(4));
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        std::string kind = s.substr(0, colon);
        auto it = deployFactories().find(kind);
        if (it != deployFactories().end())
            return it->second(s.substr(colon + 1), error);
    }
    error = "unsupported payload source: " + s;
    return nullptr;
}

} // namespace hci