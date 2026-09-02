#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "hci/payload.h"

namespace hci {

// One selectable component (core feature; selection UI in every shell).
struct ProductComponent {
    std::string id;
    std::string label;
    std::string description;  // shown under the label (GUI components page)
    std::string exe;          // exe under install root, may be empty
    std::string shortcutName; // start-menu shortcut name when selected
    bool required = false;
    bool defaultChecked = false;
};

struct ShortcutSpec {
    std::string kind;    // "desktop" | "startmenu"
    std::string name;    // e.g. "MyApp.lnk"
    std::string target;  // absolute target exe or "{installDir}/x.exe"
    std::string args;
};

struct InstallConfSpec {
    std::string fileName;              // e.g. "install.conf"
    std::string header;                // optional first line comment
    std::map<std::string, std::string> template_; // key -> "{var}" text
};

struct ProductFlows {
    std::string install;   // path or "qrc:/..." of install flow
    std::string uninstall; // path or "qrc:/..." of uninstall flow
    std::string repair;    // optional: repair/reinstall flow
    std::string upgrade;   // optional: upgrade flow
};

struct UninstallSpec {
    std::string registryKey; // e.g. "Software/Company/App"
    std::string displayName;
};

// Elevation (UAC) request policy:
//   "elevation": { "request": true, "autoRestart": true, "reason": "..." }
//   request     - ask for administrator rights up front (start of flow)
//   autoRestart - relaunch as admin automatically when requested (default true)
//   reason      - message shown to the user before restarting
struct ElevationSpec {
    bool request = false;
    bool autoRestart = true;
    std::string reason;
};

struct ProductConfig {
    std::string productName;
    std::string company;
    std::string orgName;
    std::string version = "1.0.0";
    std::string icon;
    std::string defaultMode = "gui"; // gui | tui | cli
    std::string defaultInstallPath;
    std::string bannerFont = "slant";
    std::string defaultLanguage;     // "en" | "zh" | "" ("" -> "en")
    bool backEnabled = true;         // shell "back" button available by default
    std::string welcomeTitle;        // GUI welcome big title (default productName)
    std::string poweredBy;           // branding line (default "Powered by HiBer...")
    std::vector<ProductComponent> components;
    std::vector<ShortcutSpec> shortcuts;
    InstallConfSpec installConf;
    ProductFlows flows;
    DeploySpec payload;
    UninstallSpec uninstall;
    ElevationSpec elevation;

    // Extension feature configuration (product metadata -> extensions):
    //   "extensions": { "<extensionId>": { ...custom keys... }, ... }
    // Extensions read their section via HostApi::extensionConfig(id).
    nlohmann::json extensionConfig;

    // Load from a JSON file (UTF-8). Throws std::runtime_error on failure.
    static ProductConfig loadFile(const std::string& jsonPath);

    // Load from a JSON string.
    static ProductConfig loadString(const std::string& jsonText);
};

} // namespace hci