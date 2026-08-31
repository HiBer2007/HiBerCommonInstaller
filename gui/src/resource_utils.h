// Resource helpers: direct QResource access (bypasses QFile quirks).

#pragma once

#include <string>

namespace hci {
namespace gui {

// Reads an embedded resource. 'path' may be "qrc:/..." or ":/..." or "/...".
// Returns false when missing or empty.
bool readResource(const std::string& path, std::string& out);

} // namespace gui
} // namespace hci