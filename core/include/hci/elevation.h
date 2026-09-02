// hci::elevation - Windows UAC elevation helpers (POSIX stubs).
// Elevation model: run-time requests (start of flow via product elevation
// spec, midway via the "elevate" ui step). Relaunch re-runs THIS executable
// with the original command line; the elevated process then passes
// isElevated() checks naturally (no extra marker args needed).

#pragma once

#include <string>
#include <vector>

namespace hci {
namespace elevation {

// True when the current process runs with administrator privileges
// (Windows: token elevation check).
bool isElevated();

// Relaunch the current executable with the SAME command line via UAC
// (ShellExecuteW "runas"). Returns true when the relaunch request was
// accepted (the caller should terminate this process: the new instance
// continues the run). On failure fills 'err' and returns false.
bool relaunchAsAdmin(std::string& err);

} // namespace elevation
} // namespace hci