#pragma once

#include <string>
#include <vector>

namespace hci {
namespace port {

// ------------------------------------------------------------------
// Console / terminal detection (entry model)
// ------------------------------------------------------------------
// True when the process is attached to an interactive console
// (Windows: GetConsoleProcessList > 1 or isatty(stdin/stdout)).
bool hasConsole();

// Windows console subsystem handling (main program pattern):
//   - started from a terminal   -> keep console (UTF-8 codepage + VT enabled)
//   - double-click (owns console)-> FreeConsole()
// POSIX: no-op.
void holdOrReleaseConsole();

// UTF-8 console setup: SetConsoleOutputCP(CP_UTF8) + VT processing +
// binary-mode stdout/stderr with unbuffered writes.
void setUtf8Console(bool virtualTerminal = true);

// ------------------------------------------------------------------
// Paths
// ------------------------------------------------------------------
std::string exeDir();
std::string tempDir();
std::string localAppData();
std::string currentDir();

// Join with native separators.
std::string joinPath(const std::string& a, const std::string& b);
std::string joinPath(const std::vector<std::string>& parts);

// Normalize separators to '/' (for JSON/display).
std::string normalizeSlashes(const std::string& p);

// ------------------------------------------------------------------
// Environment
// ------------------------------------------------------------------
std::string getEnv(const std::string& name, const std::string& fallback = "");
void setEnv(const std::string& name, const std::string& value);

// Expand %NAME% environment placeholders in a path/text
// (e.g. "%APPDATA%\\NSUM" -> "C:\\Users\\x\\AppData\\Roaming\\NSUM").
// Unknown/ill-formed placeholders are left unchanged.
std::string expandEnv(const std::string& text);

} // namespace port
} // namespace hci