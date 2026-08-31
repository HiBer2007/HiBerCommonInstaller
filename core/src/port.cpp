#include "hci/port.h"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace hci {
namespace port {

namespace {
std::string w2u8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring u82w(const std::string& s)
{
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
} // namespace

// ------------------------------------------------------------------
bool hasConsole()
{
#ifdef _WIN32
    DWORD dummy = 0;
    DWORD n = ::GetConsoleProcessList(&dummy, 1);
    return n > 0; // attached to a console (inherited or owned)
#else
    return ::isatty(0) || ::isatty(1);
#endif
}

bool ownsConsole()
{
#ifdef _WIN32
    DWORD pids[2] = {0, 0};
    DWORD n = ::GetConsoleProcessList(pids, 2);
    return n <= 1; // only us -> OS created a new console for us (double-click)
#else
    return false;
#endif
}

void holdOrReleaseConsole()
{
#ifdef _WIN32
    if (!hasConsole()) return;
    if (ownsConsole()) {
        // Double-click launch: release the console we do not need.
        ::FreeConsole();
        return;
    }
    // Started from a terminal: keep it and make it UTF-8 friendly.
    setUtf8Console(true);
#else
    (void)0;
#endif
}

void setUtf8Console(bool virtualTerminal)
{
#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    if (virtualTerminal) {
        HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (::GetConsoleMode(hOut, &mode))
                ::SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    ::_setmode(::_fileno(stdout), _O_BINARY);
    ::_setmode(::_fileno(stderr), _O_BINARY);
    ::setvbuf(stdout, nullptr, _IONBF, 0);
    ::setvbuf(stderr, nullptr, _IONBF, 0);
#else
    (void)virtualTerminal;
#endif
}

// ------------------------------------------------------------------
std::string exeDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0) return currentDir();
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos) w.resize(slash);
    return w2u8(w);
#else
    char buf[PATH_MAX] = {0};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        std::string p(buf, static_cast<size_t>(n));
        size_t slash = p.find_last_of('/');
        if (slash != std::string::npos) p.resize(slash);
        return p;
    }
    return ".";
#endif
}

std::string tempDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = ::GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n > MAX_PATH) return ".";
    return w2u8(std::wstring(buf, n));
#else
    return "/tmp";
#endif
}

std::string localAppData()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)))
        return w2u8(buf);
    return tempDir();
#else
    std::string xdg = getEnv("XDG_DATA_HOME");
    if (!xdg.empty()) return xdg;
    std::string home = getEnv("HOME");
    return home.empty() ? std::string(".") : home + "/.local/share";
#endif
}

std::string currentDir()
{
    std::error_code ec;
    return fs::current_path(ec).u8string();
}

std::string joinPath(const std::string& a, const std::string& b)
{
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}

std::string joinPath(const std::vector<std::string>& parts)
{
    std::string out;
    for (auto& p : parts) out = joinPath(out, p);
    return out;
}

std::string normalizeSlashes(const std::string& p)
{
    std::string out = p;
    for (auto& c : out)
        if (c == '\\') c = '/';
    return out;
}

// ------------------------------------------------------------------
std::string getEnv(const std::string& name, const std::string& fallback)
{
#ifdef _WIN32
    std::wstring wn = u82w(name);
    DWORD n = ::GetEnvironmentVariableW(wn.c_str(), nullptr, 0);
    if (n == 0) return fallback;
    std::wstring w(n, L'\0');
    ::GetEnvironmentVariableW(wn.c_str(), &w[0], n);
    return w2u8(w);
#else
    const char* v = ::getenv(name.c_str());
    return v ? std::string(v) : fallback;
#endif
}

void setEnv(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    ::SetEnvironmentVariableW(u82w(name).c_str(), u82w(value).c_str());
#else
    ::setenv(name.c_str(), value.c_str(), 1);
#endif
}

} // namespace port
} // namespace hci