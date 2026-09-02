#pragma once

#include <string>
#include <vector>

#include "hci/product.h"

namespace hci {
namespace entry {

// ------------------------------------------------------------------
// Command-line entry options (shell-agnostic core part).
// ------------------------------------------------------------------
struct EntryOptions {
    std::string mode;          // "gui" | "tui" | "cli" | "" (default from product)
    std::string productJson;   // path to product.json
    std::string flow;          // "install" | "uninstall" | specific file path
    bool silent = false;       // CLI-style, no interactive UI
    bool jsonOut = false;      // JSON protocol blocks on stdout
    std::string installPath;   // --path override
    std::string language;      // --lang code ("en"/"zh"); presets vars.language
    std::vector<std::string> extensionArgs; // consumed by extension arg handlers

    // Resolve mode: explicit flag wins, otherwise product default.
    std::string resolveMode(const ProductConfig& product) const;
};

// ------------------------------------------------------------------
// Banner (branding): product name ASCII art + mandatory powered-by line.
// ------------------------------------------------------------------
// Returns multi-line UTF-8 string:
//   <productName>  (big letters, font: slant|standard)
//   Powered by HiBer Common Installer Module
// 'font' falls back to "slant" when unknown. Overlong names use
// a smaller font automatically (standard).
std::string renderBanner(const std::string& productName, const std::string& font = "slant");

// ------------------------------------------------------------------
// Library-mode API (entry 1: core as static/dynamic library).
// ------------------------------------------------------------------
// Runs an installation flow to completion. Returns process exit code
// (0 success, 1 failure, 2 usage error).
int runInstall(const ProductConfig& product, const std::string& flowPath,
               const EntryOptions& options);

} // namespace entry
} // namespace hci