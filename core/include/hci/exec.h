#pragma once

#include <functional>
#include <string>
#include <vector>

#include "hci/vars.h"

namespace hci {
namespace exec {

// ------------------------------------------------------------------
// Filesystem operations
// ------------------------------------------------------------------
bool mkdirs(const std::string& path);
bool copyFile(const std::string& src, const std::string& dst, std::string* error = nullptr);
bool copyTree(const std::string& srcRoot, const std::string& dstRoot,
              const std::vector<std::string>& skipPatterns = {},
              std::function<bool(const std::string& rel)> onFile = nullptr,
              std::string* error = nullptr);
bool cleanDir(const std::string& path, std::string* error = nullptr);

// Extract a ZIP archive; backend: libzippp (primary), external 7za/tar as fallback.
// sevenZipExe: path to 7za.exe when available (used for .7z/.7z.exe archives).
bool extractZip(const std::string& zipPath, const std::string& destDir,
                const std::string& sevenZipExe = "", std::string* error = nullptr);

// ------------------------------------------------------------------
// Process / download
// ------------------------------------------------------------------
struct ProcessResult {
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;  // combined stdout+stderr (UTF-8 best effort)
};

bool runProcess(const std::vector<std::string>& command, int waitMs,
                ProcessResult& result);

using DownloadProgress = std::function<void(long long received, long long total)>;

// HTTP(S) download via cpr (libcurl). Returns true on success.
bool downloadFile(const std::string& url, const std::string& destPath,
                  DownloadProgress progress = nullptr, std::string* error = nullptr);

// ------------------------------------------------------------------
// Git helper (generic; used by the git extension and flows)
// ------------------------------------------------------------------
// Locates a usable system git: PATH "git" first, then common install
// paths (ProgramFiles / ProgramFiles(x86) / LocalAppData). Each candidate
// is verified with `git --version`. Returns true and fills 'path' ("git"
// for the PATH hit, absolute path otherwise).
bool findSystemGit(std::string* path = nullptr);

// ------------------------------------------------------------------
// Write permission probe
// ------------------------------------------------------------------
// True when the directory exists and a probe file can be created+removed
// (or the directory itself can be created). Used before install to
// detect "needs elevation" situations.
bool checkDirWritable(const std::string& dir, std::string* error = nullptr);

// ------------------------------------------------------------------
// Text template
// ------------------------------------------------------------------
// Render {name} placeholders from vars.
std::string renderTemplate(const std::string& text, const Vars& vars);

// ------------------------------------------------------------------
// Windows-specific (POSIX: shortcuts are no-ops returning false in v1)
// ------------------------------------------------------------------
enum class ShortcutKind { Desktop, StartMenu };

bool createShortcut(ShortcutKind kind, const std::string& name,
                    const std::string& targetPath, const std::string& workDir,
                    const std::string& args = "", std::string* error = nullptr);

// Registry helpers (WIN32 only; POSIX returns false).
bool registryWriteString(const std::string& key, const std::string& valueName,
                         const std::string& value);
bool registryDeleteKey(const std::string& key);

} // namespace exec
} // namespace hci