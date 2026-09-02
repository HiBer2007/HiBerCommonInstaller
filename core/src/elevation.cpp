// hci::elevation implementation (Windows).

#include "hci/elevation.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <vector>

namespace hci {
namespace elevation {

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev{};
    DWORD size = 0;
    bool ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev),
                                  &size) && elev.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

bool relaunchAsAdmin(std::string& err)
{
    // Replay the original command line so the elevated instance continues
    // the same run (--product/--flow/--silent/... all preserved).
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // lpParameters = original args only (skip the exe token: quoted or bare).
    std::wstring params;
    const wchar_t* cmd = GetCommandLineW();
    const wchar_t* p = cmd;
    if (*p == L'"') {
        p = wcschr(p + 1, L'"');
        if (p) ++p;
    } else {
        p = wcschr(p, L' ');
    }
    if (p && *p) params.assign(p); // includes the leading space

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        err = "ShellExecute(runas) failed (error " +
              std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return false;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

} // namespace elevation
} // namespace hci

#else // POSIX: no elevation concept in v1.

namespace hci {
namespace elevation {

bool isElevated() { return false; }

bool relaunchAsAdmin(std::string& err)
{
    err = "elevation is not supported on this platform";
    return false;
}

} // namespace elevation
} // namespace hci

#endif