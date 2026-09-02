#include "hci/product.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace hci {

namespace {

std::string readTextFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

ProductComponent parseComponent(const nlohmann::json& j)
{
    ProductComponent c;
    c.id = j.value("id", "");
    c.label = j.value("label", c.id);
    c.description = j.value("description", "");
    c.exe = j.value("exe", "");
    c.shortcutName = j.value("shortcutName", "");
    c.required = j.value("required", false);
    c.defaultChecked = j.value("defaultChecked", false);
    return c;
}

ShortcutSpec parseShortcut(const nlohmann::json& j)
{
    ShortcutSpec s;
    s.kind = j.value("kind", "desktop");
    s.name = j.value("name", "");
    s.target = j.value("target", "");
    s.args = j.value("args", "");
    return s;
}

} // namespace

ProductConfig ProductConfig::loadString(const std::string& jsonText)
{
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded())
        throw std::runtime_error("product.json: invalid JSON");

    ProductConfig p;
    p.productName = j.value("productName", "");
    p.company = j.value("company", "");
    p.orgName = j.value("orgName", "");
    p.version = j.value("version", "1.0.0");
    p.icon = j.value("icon", "");
    p.defaultMode = j.value("defaultMode", "gui");
    p.defaultInstallPath = j.value("defaultInstallPath", "");
    p.bannerFont = j.value("banner", nlohmann::json::object()).value("font", "slant");
    p.defaultLanguage = j.value("defaultLanguage", "");
    p.backEnabled = j.value("backEnabled", true);
    p.welcomeTitle = j.value("welcomeTitle", "");
    p.poweredBy = j.value("branding", nlohmann::json::object())
                      .value("poweredBy", "Powered by HiBer Common Installer Module");

    if (j.contains("components") && j["components"].is_array()) {
        for (auto& c : j["components"]) p.components.push_back(parseComponent(c));
    }
    if (j.contains("shortcuts") && j["shortcuts"].is_array()) {
        for (auto& s : j["shortcuts"]) p.shortcuts.push_back(parseShortcut(s));
    }
    if (j.contains("installConf") && j["installConf"].is_object()) {
        const auto& ic = j["installConf"];
        p.installConf.fileName = ic.value("fileName", "install.conf");
        p.installConf.header = ic.value("header", "");
        if (ic.contains("template") && ic["template"].is_object()) {
            for (auto it = ic["template"].begin(); it != ic["template"].end(); ++it)
                p.installConf.template_[it.key()] = it.value().get<std::string>();
        }
    }
    if (j.contains("flows") && j["flows"].is_object()) {
        p.flows.install = j["flows"].value("install", "");
        p.flows.uninstall = j["flows"].value("uninstall", "");
        p.flows.repair = j["flows"].value("repair", "");
        p.flows.upgrade = j["flows"].value("upgrade", "");
    }
    if (j.contains("payload") && j["payload"].is_object()) {
        p.payload.source = j["payload"].value("source", "");
        if (j["payload"].contains("skip") && j["payload"]["skip"].is_array()) {
            for (auto& s : j["payload"]["skip"]) p.payload.skip.push_back(s.get<std::string>());
        }
    }
    if (j.contains("uninstall") && j["uninstall"].is_object()) {
        p.uninstall.registryKey = j["uninstall"].value("registryKey", "");
        p.uninstall.displayName = j["uninstall"].value("displayName", p.productName);
    }
    if (j.contains("elevation") && j["elevation"].is_object()) {
        p.elevation.request = j["elevation"].value("request", false);
        p.elevation.autoRestart = j["elevation"].value("autoRestart", true);
        p.elevation.reason = j["elevation"].value("reason", "");
    }
    if (j.contains("extensions") && j["extensions"].is_object()) {
        p.extensionConfig = j["extensions"];
    }
    return p;
}

ProductConfig ProductConfig::loadFile(const std::string& jsonPath)
{
    return loadString(readTextFile(jsonPath));
}

} // namespace hci