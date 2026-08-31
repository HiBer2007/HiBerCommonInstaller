#pragma once

#include <memory>
#include <string>
#include <vector>

namespace hci {

// DeploySpec describes where the payload comes from and what to skip.
//   source: "dir:<path>" | "zip:<path>" | "qrc:/prefix" (qrc via hci_qtbridge)
//   skip:   wildcard patterns relative to payload root (* and ? supported)
struct DeploySpec {
    std::string source;
    std::vector<std::string> skip;
};

// One entry of a deploy source enumeration.
struct DeployEntry {
    std::string relPath; // relative path, '/'-separated
    bool isDir = false;
    long long size = 0;
};

class IDeploySource {
public:
    virtual ~IDeploySource() = default;

    // Open the source; returns false when the source is unavailable.
    virtual bool open(std::string& error) = 0;

    // Enumerate entries (directories first is NOT guaranteed; sort client-side).
    virtual bool enumerate(std::vector<DeployEntry>& entries, std::string& error) = 0;

    // Read one file's raw bytes (relPath from enumerate()).
    virtual bool readFile(const std::string& relPath, std::vector<char>& out,
                          std::string& error) = 0;

    const std::string& sourceKind() const { return kind_; }

protected:
    std::string kind_;
};

// Directory-backed source ("dir:").
std::shared_ptr<IDeploySource> makeDirSource(const std::string& rootPath);

// ZIP-backed source ("zip:"), via libzippp.
std::shared_ptr<IDeploySource> makeZipSource(const std::string& zipPath);

// Build a source from a DeploySpec; returns false for unsupported kinds.
std::shared_ptr<IDeploySource> makeDeploySource(const DeploySpec& spec,
                                                std::string& error);

// Wildcard match for skip patterns ('*' and '?' only).
bool wildcardMatch(const std::string& pattern, const std::string& text);

} // namespace hci