#include "hci/exec.h"

#include "hci/payload.h"
#include "hci/port.h"

#include <libzippp.h>

#include <cpr/cpr.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hci {
namespace exec {

// ------------------------------------------------------------------
bool mkdirs(const std::string& path)
{
    std::error_code ec;
    fs::create_directories(fs::u8path(path), ec);
    return !ec;
}

bool copyFile(const std::string& src, const std::string& dst, std::string* error)
{
    std::error_code ec;
    fs::create_directories(fs::u8path(dst).parent_path(), ec);
    fs::copy_file(fs::u8path(src), fs::u8path(dst), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    return true;
}

bool copyTree(const std::string& srcRoot, const std::string& dstRoot,
              const std::vector<std::string>& skipPatterns,
              std::function<bool(const std::string&)> onFile,
              std::string* error)
{
    std::error_code ec;
    fs::path src = fs::u8path(srcRoot);
    if (!fs::is_directory(src, ec)) {
        if (error) *error = "copyTree: source not a directory: " + srcRoot;
        return false;
    }
    if (!fs::create_directories(fs::u8path(dstRoot), ec) && ec) {
        if (error) *error = ec.message();
        return false;
    }

    fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) { if (error) *error = ec.message(); return false; }
        fs::path rel = fs::relative(it->path(), src, ec);
        if (ec) continue;
        std::string relS = rel.generic_u8string();

        bool skipped = false;
        for (auto& pat : skipPatterns) {
            if (wildcardMatch(pat, relS)) { skipped = true; break; }
        }
        if (skipped) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }

        fs::path dst = fs::u8path(dstRoot) / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(dst, ec);
            continue;
        }
        std::error_code copyEc;
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, copyEc);
        if (copyEc) {
            if (error) *error = copyEc.message();
            return false;
        }
        if (onFile && !onFile(relS)) {
            if (error) *error = "copyTree: cancelled by callback";
            return false;
        }
    }
    return true;
}

bool cleanDir(const std::string& path, std::string* error)
{
    std::error_code ec;
    fs::path root = fs::u8path(path);
    if (!fs::exists(root, ec)) return true; // nothing to clean
    if (!fs::is_directory(root, ec)) {
        if (error) *error = "cleanDir: not a directory: " + path;
        return false;
    }
    fs::directory_iterator it(root, ec);
    if (ec) { if (error) *error = ec.message(); return false; }
    fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) { if (error) *error = ec.message(); return false; }
        fs::remove_all(it->path(), ec);
        if (ec) { if (error) *error = ec.message(); return false; }
    }
    return true;
}

// ------------------------------------------------------------------
bool extractZip(const std::string& zipPath, const std::string& destDir,
                const std::string& sevenZipExe, std::string* error)
{
    // .7z / .7z.exe archives cannot go through libzippp; use 7za when provided.
    std::string lower = zipPath;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((lower.rfind(".7z.exe") == lower.size() - 7 || lower.rfind(".7z") == lower.size() - 3)
        && !sevenZipExe.empty() && lower.size() >= 4 &&
        lower.compare(lower.size() - 3, 3, ".7z") == 0) {
        // 7za x archive -o<dest> -y
        ProcessResult r;
        std::vector<std::string> cmd = {sevenZipExe, "x", zipPath, "-o" + destDir, "-y"};
        if (!runProcess(cmd, 120000, r)) {
            if (error) *error = "7za launch failed";
            return false;
        }
        if (r.exitCode != 0) {
            if (error) *error = "7za failed: " + std::to_string(r.exitCode);
            return false;
        }
        return true;
    }

    libzippp::ZipArchive za(zipPath);
    if (!za.open(libzippp::ZipArchive::ReadOnly)) {
        if (error) *error = "cannot open zip: " + zipPath;
        return false;
    }
    std::vector<libzippp::ZipEntry> entries = za.getEntries();
    for (auto& e : entries) {
        std::string name = e.getName();
        fs::path dest = fs::u8path(destDir) / fs::u8path(name);
        if (e.isDirectory()) {
            std::error_code ec;
            fs::create_directories(dest, ec);
            continue;
        }
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write: " + name;
            za.close();
            return false;
        }
        int rc = e.readContent(out);
        out.close();
        if (rc < 0) {
            if (error) *error = "zip read failed: " + name;
            za.close();
            return false;
        }
    }
    za.close();
    return true;
}

// ------------------------------------------------------------------
bool runProcess(const std::vector<std::string>& command, int waitMs,
                ProcessResult& result)
{
#ifdef _WIN32
    // Build a command line with proper quoting.
    std::wstring cmdLine;
    auto appendArg = [&cmdLine](const std::string& a) {
        std::wstring wa;
        int n = ::MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
        if (n > 0) {
            wa.resize(static_cast<size_t>(n) - 1);
            ::MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, &wa[0], n);
        }
        bool quote = wa.find(L' ') != std::wstring::npos || wa.find(L'\t') != std::wstring::npos;
        if (!cmdLine.empty()) cmdLine += L' ';
        if (quote) {
            cmdLine += L'"';
            for (wchar_t c : wa) {
                if (c == L'"') cmdLine += L"\\\"";
                else cmdLine += c;
            }
            cmdLine += L'"';
        } else {
            cmdLine += wa;
        }
    };
    for (auto& a : command) appendArg(a);

    // Redirect output to a temp file (avoids pipe-buffer deadlock).
    std::string tmpOut = port::tempDir() + "/hci_proc_out.txt";
    std::wstring wTmp;
    int n = ::MultiByteToWideChar(CP_UTF8, 0, tmpOut.c_str(), -1, nullptr, 0);
    if (n > 0) { wTmp.resize(static_cast<size_t>(n) - 1); ::MultiByteToWideChar(CP_UTF8, 0, tmpOut.c_str(), -1, &wTmp[0], n); }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hFile = ::CreateFileW(wTmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        result.launched = false;
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hFile;
    si.hStdError = hFile;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    // Each argument is already quoted as needed (first token = exe path).
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        ::CloseHandle(hFile);
        result.launched = false;
        return false;
    }
    ::CloseHandle(hFile);

    result.launched = true;
    if (waitMs < 0) {
        // Detach: fire-and-forget (start an app, don't wait for it).
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        result.exitCode = 0;
        return true;
    }
    DWORD wait = (waitMs <= 0) ? INFINITE : static_cast<DWORD>(waitMs);
    DWORD rc = ::WaitForSingleObject(pi.hProcess, wait);
    if (rc == WAIT_TIMEOUT) {
        ::TerminateProcess(pi.hProcess, 1);
        ::WaitForSingleObject(pi.hProcess, 2000);
        result.timedOut = true;
        result.exitCode = -1;
    } else {
        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);
        result.exitCode = static_cast<int>(code);
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    // Read captured output.
    std::ifstream f(tmpOut, std::ios::binary);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        result.output = ss.str();
        f.close();
    }
    std::remove(tmpOut.c_str());
    return true;
#else
    (void)waitMs;
    std::string cmd;
    for (auto& a : command) {
        if (!cmd.empty()) cmd += ' ';
        cmd += "'" + a + "'";
    }
    cmd += " 2>&1";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) { result.launched = false; return false; }
    result.launched = true;
    char buf[4096];
    size_t rd = 0;
    while ((rd = ::fread(buf, 1, sizeof(buf), p)) > 0)
        result.output.append(buf, rd);
    result.exitCode = ::pclose(p);
    return true;
#endif
}

// ------------------------------------------------------------------
bool downloadFile(const std::string& url, const std::string& destPath,
                  DownloadProgress progress, std::string* error)
{
    std::ofstream f(fs::u8path(destPath), std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot open output file: " + destPath;
        return false;
    }
    cpr::Session session;
    session.SetUrl(cpr::Url{url});
    session.SetTimeout(cpr::Timeout{60000});
    if (progress) {
        session.SetProgressCallback(cpr::ProgressCallback(
            [progress](cpr::cpr_off_t downloadTotal, cpr::cpr_off_t downloadNow,
                       cpr::cpr_off_t, cpr::cpr_off_t, intptr_t) {
                progress(static_cast<long long>(downloadNow),
                         static_cast<long long>(downloadTotal));
                return true;
            }));
    }
    cpr::Response r = session.Download(f);
    f.close();
    if (!r.error.message.empty() || r.status_code < 200 || r.status_code >= 300) {
        if (error) *error = r.error.message.empty()
            ? "HTTP " + std::to_string(r.status_code)
            : r.error.message;
        std::error_code ec;
        fs::remove(fs::u8path(destPath), ec);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------
std::string renderTemplate(const std::string& text, const Vars& vars)
{
    return vars.interpolate(text);
}

// ------------------------------------------------------------------
#ifdef _WIN32
namespace {
std::wstring s2w(const std::string& s)
{
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n) - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

bool makeShortcutAt(const std::wstring& lnkPath, const std::wstring& target,
                    const std::wstring& workDir, const std::wstring& args,
                    std::string* error)
{
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        if (error) *error = "CoInitializeEx failed";
        return false;
    }
    IShellLinkW* sl = nullptr;
    IPersistFile* pf = nullptr;
    hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IShellLinkW, reinterpret_cast<void**>(&sl));
    if (SUCCEEDED(hr)) {
        sl->SetPath(target.c_str());
        sl->SetWorkingDirectory(workDir.c_str());
        if (!args.empty()) sl->SetArguments(args.c_str());
        hr = sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf));
        if (SUCCEEDED(hr)) {
            hr = pf->Save(lnkPath.c_str(), TRUE);
            pf->Release();
        }
        sl->Release();
    }
    if (FAILED(hr) && error) *error = "IShellLink save failed";
    return SUCCEEDED(hr);
}
} // namespace
#endif

bool createShortcut(ShortcutKind kind, const std::string& name,
                    const std::string& targetPath, const std::string& workDir,
                    const std::string& args, std::string* error)
{
#ifdef _WIN32
    wchar_t base[MAX_PATH] = {0};
    if (kind == ShortcutKind::Desktop) {
        if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, base))) {
            if (error) *error = "cannot resolve desktop folder";
            return false;
        }
    } else {
        if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, base))) {
            if (error) *error = "cannot resolve start menu folder";
            return false;
        }
    }

    // name may contain '/' subfolders (e.g. "MyApp/MyApp.lnk").
    std::wstring rel = s2w(name);
    std::wstring sub;
    size_t slash = rel.find(L'/');
    if (slash != std::wstring::npos) {
        sub = rel.substr(0, slash);
        rel = rel.substr(slash + 1);
    }
    std::wstring dir(base);
    if (!sub.empty()) {
        dir += L"\\" + sub;
        ::CreateDirectoryW(dir.c_str(), nullptr);
    }
    std::wstring lnk = dir + L"\\" + rel;
    return makeShortcutAt(lnk, s2w(targetPath), s2w(workDir), s2w(args), error);
#else
    (void)kind; (void)name; (void)targetPath; (void)workDir; (void)args;
    if (error) *error = "shortcuts not supported on this platform (v1)";
    return false;
#endif
}

// ------------------------------------------------------------------
#ifdef _WIN32
namespace {
std::wstring regKeyPath(const std::string& key)
{
    // "Software/Company/App" -> "Software\Company\App"
    std::wstring w = s2w(key);
    for (auto& c : w) if (c == L'/') c = L'\\';
    return w;
}
} // namespace
#endif

bool registryWriteString(const std::string& key, const std::string& valueName,
                         const std::string& value)
{
#ifdef _WIN32
    HKEY hk = nullptr;
    LONG r = ::RegCreateKeyExW(HKEY_CURRENT_USER, regKeyPath(key).c_str(),
                               0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr);
    if (r != ERROR_SUCCESS) return false;
    std::wstring vName = s2w(valueName);
    std::wstring v = s2w(value);
    r = ::RegSetValueExW(hk, vName.c_str(), 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(v.c_str()),
                         static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(hk);
    return r == ERROR_SUCCESS;
#else
    (void)key; (void)valueName; (void)value;
    return false;
#endif
}

bool registryDeleteKey(const std::string& key)
{
#ifdef _WIN32
    LONG r = ::RegDeleteKeyW(HKEY_CURRENT_USER, regKeyPath(key).c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
#else
    (void)key;
    return false;
#endif
}

// ------------------------------------------------------------------
bool findSystemGit(std::string* path)
{
    ProcessResult r;
    if (runProcess({"git", "--version"}, 8000, r) && r.exitCode == 0) {
        if (path) *path = "git";
        return true;
    }
    // Common install locations (Git for Windows / scoop / MSYS).
    std::vector<std::string> candidates;
    std::string pf = port::getEnv("ProgramFiles");
    std::string pf86 = port::getEnv("ProgramFiles(x86)");
    std::string la = port::getEnv("LocalAppData");
    if (!pf.empty()) candidates.push_back(pf + "\\Git\\bin\\git.exe");
    if (!pf86.empty()) candidates.push_back(pf86 + "\\Git\\bin\\git.exe");
    if (!la.empty()) candidates.push_back(la + "\\Programs\\Git\\bin\\git.exe");
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(fs::u8path(c), ec)) {
            ProcessResult vr;
            if (runProcess({c, "--version"}, 8000, vr) && vr.exitCode == 0) {
                if (path) *path = c;
                return true;
            }
        }
    }
    return false;
}

bool checkDirWritable(const std::string& dir, std::string* error)
{
    std::error_code ec;
    fs::path p = fs::u8path(dir);
    if (!fs::exists(p, ec)) {
        // Create the full chain to test.
        if (!fs::create_directories(p, ec)) {
            if (error) *error = "cannot create directory: " + dir;
            return false;
        }
    }
    fs::path probe = p / ".hci_write_probe";
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "no write permission for: " + dir;
            return false;
        }
        f << "probe";
    }
    fs::remove(probe, ec);
    return true;
}

} // namespace exec
} // namespace hci