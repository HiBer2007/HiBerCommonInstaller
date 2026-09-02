#include "hci/flow.h"

#include "hci/elevation.h"
#include "hci/entry.h"
#include "hci/exec.h"
#include "hci/extension_registry.h"
#include "hci/log.h"
#include "hci/payload.h"
#include "hci/port.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace hci {

// ------------------------------------------------------------------
// FlowSpec parsing
// ------------------------------------------------------------------
FlowSpec FlowSpec::loadString(const std::string& jsonText)
{
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error("flow: invalid JSON");

    FlowSpec spec;
    spec.id = j.value("id", "flow");

    if (j.contains("vars") && j["vars"].is_object()) {
        for (auto it = j["vars"].begin(); it != j["vars"].end(); ++it)
            spec.seedVars.set(it.key(), it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump());
    }
    if (!j.contains("steps") || !j["steps"].is_array())
        throw std::runtime_error("flow: missing steps array");

    for (auto& raw : j["steps"]) {
        FlowStep s;
        s.id = raw.value("id", "");
        s.type = raw.value("type", "");
        s.ui = raw.value("ui", "");
        s.next = raw.value("next", "");
        s.when = raw.value("when", "");
        s.onFail = raw.value("onFail", "abort");
        s.params = raw;
        s.params.erase("id");
        s.params.erase("type");
        s.params.erase("ui");
        s.params.erase("next");
        s.params.erase("when");
        s.params.erase("onFail");
        if (s.id.empty()) throw std::runtime_error("flow: step without id");
        spec.steps.push_back(std::move(s));
    }
    return spec;
}

FlowSpec FlowSpec::loadFile(const std::string& jsonPath)
{
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open flow file: " + jsonPath);
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadString(ss.str());
}

// ------------------------------------------------------------------
// FlowRunner
// ------------------------------------------------------------------
FlowRunner::FlowRunner(ProductConfig& product, InstallContext& ctx,
                       IFlowUi* ui, IScriptEngine* script)
    : product_(product), ctx_(ctx), ui_(ui), script_(script)
{
}

// ------------------------------------------------------------------
// Default elevation handling (shells may override to prompt first).
// ------------------------------------------------------------------
bool IFlowUi::onElevate(const std::string& reason, bool autoRestart)
{
    (void)autoRestart;
    std::cerr << "Administrator privileges required";
    if (!reason.empty()) std::cerr << ": " << reason;
    std::cerr << "\n";
    std::string err;
    if (hci::elevation::relaunchAsAdmin(err)) {
        // The elevated instance takes over; this process is obsolete.
        std::exit(0);
    }
    std::cerr << "Elevation failed: " << err << " (continuing unprivileged)\n";
    return false;
}

namespace {

std::string resolveFlowPath(const std::string& p)
{
    if (p.rfind("qrc:", 0) == 0)
        throw std::runtime_error("qrc flow requires the Qt bridge (hci_qtbridge)");
    return p;
}

bool truthy(const std::string& s)
{
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return !s.empty(); // non-empty other values count as true
}

std::string readText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string fetchGitHubAssetUrl(const std::string& repo,
                                const std::string& namePattern,
                                std::string* error)
{
    cpr::Response r = cpr::Get(cpr::Url{"https://api.github.com/repos/" + repo + "/releases/latest"},
                               cpr::Header{{"Accept", "application/vnd.github+json"},
                                           {"User-Agent", "hci/1.0"},
                                           {"X-GitHub-Api-Version", "2022-11-28"}},
                               cpr::Timeout{15000});
    if (r.status_code != 200) {
        if (error) *error = "GitHub API HTTP " + std::to_string(r.status_code);
        return {};
    }
    nlohmann::json j = nlohmann::json::parse(r.text, nullptr, false);
    if (j.is_discarded() || !j.contains("assets")) {
        if (error) *error = "GitHub API: unexpected payload";
        return {};
    }
    for (auto& a : j["assets"]) {
        std::string name = a.value("name", "");
        std::string upName = name, upPattern = namePattern;
        for (auto& c : upName) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (auto& c : upPattern) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upName.find(upPattern) != std::string::npos &&
            upName.find("64-BIT") != std::string::npos &&
            (upName.rfind(".ZIP") == upName.size() - 4 ||
             upName.rfind(".7Z.EXE") == upName.size() - 7)) {
            return a.value("browser_download_url", "");
        }
    }
    if (error) *error = "GitHub API: no matching asset for '" + namePattern + "'";
    return {};
}

} // namespace

int FlowRunner::run(FlowSpec& flow)
{
    // Seed variables (do not override values set by the host/CLI).
    for (auto& kv : flow.seedVars.all()) {
        if (!ctx_.vars().has(kv.first)) ctx_.vars().set(kv.first, kv.second);
    }
    if (!ctx_.vars().has("productName")) ctx_.vars().set("productName", product_.productName);
    if (!ctx_.vars().has("company")) ctx_.vars().set("company", product_.company);
    if (!ctx_.vars().has("productVersion")) ctx_.vars().set("productVersion", product_.version);
    if (!ctx_.vars().has("installDir"))
        ctx_.vars().set("installDir", port::expandEnv(product_.defaultInstallPath));
    if (!ctx_.vars().has("tempDir")) ctx_.vars().set("tempDir", port::tempDir());
    if (!ctx_.vars().has("exeDir")) ctx_.vars().set("exeDir", port::exeDir());

    // Up-front elevation request (product policy; per-flow controllable by
    // NOT setting it and using an "elevate" step instead).
    if (product_.elevation.request && !hci::elevation::isElevated()) {
        if (ui_) {
            if (ui_->onElevate(product_.elevation.reason,
                               product_.elevation.autoRestart)) {
                return 0; // relaunch issued; this process is about to exit
            }
            // declined/unavailable: continue unprivileged
        }
    }

    size_t i = 0;
    history_.clear();
    while (i < flow.steps.size()) {
        if (ctx_.cancelled()) {
            if (ui_) ui_->onMessage("install cancelled", true);
            return 1;
        }
        FlowStep& step = flow.steps[i];
        // UI chrome per step: buttons state etc. (back available when we have
        // already entered at least one step).
        if (ui_) ui_->onStepParam(step.params, !history_.empty());
        history_.push_back(i);

        // Step logging (visible on terminal/console for every shell).
        Log::Info(fmt("step '{}' ({}) starting", step.id,
                      step.ui.empty() ? step.type : std::string("ui:") + step.ui));
        // Overall progress computed by the controller: completed steps share
        // 100%; executors additionally report intra-step detail via onProgress.
        if (ui_ && step.ui.empty()) {
            int overall = static_cast<int>(
                static_cast<long long>(history_.size() - 1) * 100 /
                std::max<size_t>(1, flow.steps.size()));
            ui_->onProgress(step.id, overall, "");
        }

        std::string error;
        if (!runStep(flow, step, i, error)) {
            if (ui_ && ui_->backRequested() && history_.size() >= 2) {
                // User asked "previous step": re-run the last step.
                history_.pop_back();      // drop the failed current step
                i = history_.back();      // back to the previous step
                history_.pop_back();      // will be re-pushed on entry
                continue;                 // do NOT advance i
            }
            if (step.onFail == "ignore") {
                Log::Warn("step '" + step.id + "' failed (ignored): " + error);
                if (ui_) ui_->onMessage("step '" + step.id + "' failed (ignored): " + error, true);
                ++i;                      // advance past the failed step
                continue;
            }
            if (ui_) ui_->onMessage("step '" + step.id + "' failed: " + error, true);
            return 1;
        }
        if (bus_) bus_->publish("hci/step", {{"id", step.id}, {"state", "done"}});
        Log::Info(fmt("step '{}' done", step.id));
        // Advance: runStep either left i at a jump target-1 (next/id) or
        // untouched (sequential step) - increment reaches the right step.
        ++i;
    }
    return 0;
}

std::string FlowRunner::resolvePath(const std::string& p) const
{
    if (p.empty()) return p;
    if (p.rfind("qrc:", 0) == 0 || p.rfind("http://", 0) == 0 ||
        p.rfind("https://", 0) == 0)
        return p;
    if (p.size() >= 2 && (p[1] == ':' || p[0] == '/' || p[0] == '\\'))
        return p; // absolute (drive letter, root-relative)
    if (!baseDir_.empty())
        return port::joinPath(baseDir_, p);
    return p;
}

bool FlowRunner::evalWhen(const std::string& expr, bool& shouldRun, std::string& error)
{
    if (expr.empty()) { shouldRun = true; return true; }
    if (!script_) {
        error = "condition requires a script engine (when)";
        return false;
    }
    std::string out;
    if (!script_->eval(expr, ctx_.vars(), out)) {
        error = "condition eval failed: " + out;
        return false;
    }
    shouldRun = truthy(out);
    return true;
}

bool FlowRunner::runStep(FlowSpec& flow, FlowStep& step, size_t& index,
                         std::string& error)
{
    bool shouldRun = true;
    if (!evalWhen(step.when, shouldRun, error)) return false;
    if (!shouldRun) {
        // Condition false: skip this step (not an error).
        if (bus_) bus_->publish("hci/step", {{"id", step.id}, {"state", "skipped"}});
        if (ui_) ui_->onProgress(step.id + " (skipped)", -1, "");
        if (!step.next.empty()) {
            if (step.next == "__end") { index = flow.steps.size(); return true; }
            for (size_t j = 0; j < flow.steps.size(); ++j) {
                if (flow.steps[j].id == step.next) { index = j - 1; break; }
            }
        }
        return true;
    }
    if (bus_) bus_->publish("hci/step", {{"id", step.id}, {"state", "start"}});

    bool ok = false;
    if (!step.ui.empty())
        ok = handleUiStep(step, error);
    else if (!step.type.empty())
        ok = handleExecStep(step, error);
    else {
        error = "step '" + step.id + "' has neither ui nor type";
        return false;
    }

    if (ok && !step.next.empty()) {
        if (step.next == "__end") {
            index = flow.steps.size(); // terminate the loop
            return true;
        }
        for (size_t j = 0; j < flow.steps.size(); ++j) {
            if (flow.steps[j].id == step.next) { index = j - 1; break; }
        }
    }
    return ok;
}

bool FlowRunner::handleUiStep(FlowStep& step, std::string& error)
{
    if (!ui_) { error = "interactive step '" + step.id + "' requires a shell UI"; return false; }

    if (step.ui == "language") {
        // Optional language selection (welcome precedes it in the flow);
        // precedence: --lang preset (vars) > params.default > product
        // defaultLanguage > "en". The picker only opens when no preset exists.
        std::string sel = ctx_.vars().get("language");
        if (sel.empty()) {
            std::string def = step.params.value("default", "");
            if (def.empty()) def = product_.defaultLanguage;
            if (def.empty()) def = "en";
            if (!ui_->onLanguage(sel, def)) { error = "cancelled by user"; return false; }
            if (sel.empty()) sel = def;
        }
        ctx_.vars().set("language", sel);
        return true;
    }

    if (step.ui == "elevate") {
        // Midway elevation request (any mode). The elevated relaunch passes
        // this step trivially (isElevated()).
        if (hci::elevation::isElevated()) return true;
        std::string reason = step.params.value("reason", "");
        bool autoRestart = step.params.value("autoRestart", true);
        if (!ui_->onElevate(reason, autoRestart)) {
            error = "elevation declined";
            return false;
        }
        return true; // relaunch issued; process exits in the default handler
    }

    if (step.ui == "welcome") {
        if (!ui_->onWelcome(product_.productName, product_)) { error = "cancelled by user"; return false; }
        return true;
    }
    if (step.ui == "license") {
        std::string src = step.params.value("source", "");
        std::string text;
        if (src.rfind("qrc:", 0) == 0) {
            if (resourceReader_) {
                if (!resourceReader_(src.substr(4), text)) { error = "cannot read license resource"; return false; }
            } else {
                error = "qrc license requires a resource reader (GUI shell)";
                return false;
            }
        } else {
            text = readText(resolvePath(src));
        }
        bool accepted = false;
        if (!ui_->onLicense(text, accepted)) { error = "cancelled by user"; return false; }
        if (!accepted) { error = "license not accepted"; return false; }
        return true;
    }
    if (step.ui == "path") {
        std::string p = ctx_.vars().get("installDir");
        if (!ui_->onPath(p, p)) { error = "cancelled by user"; return false; }
        ctx_.vars().set("installDir", p);
        // Write-permission probe: no access -> ask for elevation so the
        // install cannot fail half-way (elevated relaunch re-runs the flow).
        std::string werr;
        if (!p.empty() && !hci::exec::checkDirWritable(p, &werr)) {
            Log::Warn("install dir not writable: " + werr);
            std::string reason = "The target directory needs administrator rights:\n" + p;
            if (ui_ && ui_->onElevate(reason, true))
                return true; // relaunch issued; this process exits
            // declined: continue anyway (may fail later, per mode)
        } else {
            Log::Info("install dir writable: " + p);
        }
        return true;
    }
    if (step.ui == "git") {
        // Git strategy page: auto-detect system git, let the user choose
        // bundled vs system; decision lands in vars.gitMode (and the
        // detection result in vars.gitSystemAvailable).
        std::string sysPath;
        bool available = hci::exec::findSystemGit(&sysPath);
        ctx_.vars().setBool("gitSystemAvailable", available);
        if (!sysPath.empty()) ctx_.vars().set("gitSystemPath", sysPath);
        Log::Info(std::string("system git ") + (available ? "found: " + sysPath : "not found"));
        std::string mode;
        std::string def = step.params.value("default", "");
        if (def.empty()) def = available ? "system" : "bundled";
        if (!ui_->onGit(available, mode, def)) { error = "cancelled by user"; return false; }
        if (mode.empty()) mode = def;
        ctx_.vars().set("gitMode", mode);
        ctx_.vars().setBool("gitUseSystem", mode == "system");
        ctx_.vars().setBool("gitDownload", mode == "bundled");
        return true;
    }
    if (step.ui == "components") {
        std::vector<bool> checked(product_.components.size(), false);
        for (size_t i = 0; i < product_.components.size(); ++i) {
            // Pre-set values (CLI --with-* or extension arg handlers) win over
            // product defaults; interactive UI choice always wins afterwards.
            std::string varName = "components." + product_.components[i].id;
            if (ctx_.vars().has(varName))
                checked[i] = ctx_.vars().getBool(varName);
            else
                checked[i] = product_.components[i].defaultChecked;
        }
        if (!ui_->onComponents(product_.components, checked)) { error = "cancelled by user"; return false; }
        for (size_t i = 0; i < product_.components.size(); ++i) {
            ctx_.vars().setBool("components." + product_.components[i].id, checked[i]);
        }
        return true;
    }
    if (step.ui == "option") {
        std::vector<std::string> choices;
        if (step.params.contains("choices") && step.params["choices"].is_array()) {
            for (auto& c : step.params["choices"]) choices.push_back(c.get<std::string>());
        }
        int selected = step.params.value("default", 0);
        std::string prompt = step.params.value("prompt", step.id);
        if (!ui_->onOption(prompt, choices, selected)) { error = "cancelled by user"; return false; }
        ctx_.vars().set(step.params.value("name", step.id), std::to_string(selected));
        return true;
    }
    if (step.ui == "confirm") {
        bool yes = step.params.value("defaultYes", true);
        if (!ui_->onConfirm(step.params.value("prompt", step.id), yes)) { error = "cancelled by user"; return false; }
        ctx_.vars().setBool(step.params.value("name", step.id), yes);
        if (!yes && step.params.value("abortIfNo", false)) { error = "aborted by user choice"; return false; }
        return true;
    }
    if (step.ui == "input") {
        std::string value;
        bool required = step.params.value("required", true);
        std::string name = step.params.value("name", step.id);
        if (!ui_->onInput(step.params.value("prompt", name), value, required)) { error = "cancelled by user"; return false; }
        ctx_.vars().set(name, value);
        return true;
    }
    if (step.ui == "finish") {
        ctx_.vars().setBool("launchNow", step.params.value("launchDefault", true));
        if (ui_) {
            std::string launch = step.params.value("launch", "");
            std::vector<std::string> opts;
            if (step.params.contains("launchOptions") &&
                step.params["launchOptions"].is_array()) {
                for (auto& o : step.params["launchOptions"]) {
                    std::string item = o.is_string()
                        ? o.get<std::string>()
                        : o.value("path", "");
                    std::string name = o.value("name", item);
                    item = ctx_.vars().interpolate(item);
                    if (!item.empty()) {
                        std::string abs = item;
                        if (abs.size() < 2 || (abs[1] != ':' && abs[0] != '/')) {
                            // forward slash preferred in JSON; use installDir base
                            std::string base = ctx_.vars().get("installDir");
                            if (!base.empty()) abs = port::joinPath(base, abs);
                        }
                        opts.push_back(name + "=" + abs);
                    }
                }
            }
            ui_->onFinish(true, step.params.value("message", "installation complete"),
                          launch.empty() ? "" : port::joinPath(ctx_.vars().get("installDir"), launch),
                          opts);
        }
        return true;
    }
    error = "unknown ui step: " + step.ui;
    return false;
}

bool FlowRunner::handleExecStep(FlowStep& step, std::string& error)
{
    const Vars& v = ctx_.vars();

    if (step.type == "script") {
        std::string code = step.params.value("script", "");
        if (code.empty()) { error = "script step: missing 'script'"; return false; }
        if (!script_) { error = "script step: no script engine"; return false; }
        std::string err;
        if (!script_->run(code, v, err)) { error = "lua: " + err; return false; }
        return true;
    }

    if (step.type == "clean") {
        std::string target = v.interpolate(step.params.value("target", ""));
        if (target.empty()) { error = "clean: missing target"; return false; }
        return exec::cleanDir(target, &error);
    }

    if (step.type == "copy") {
        std::string src = resolvePath(v.interpolate(step.params.value("source", "")));
        std::string dst = v.interpolate(step.params.value("target", ""));
        std::vector<std::string> skip;
        if (step.params.contains("skip") && step.params["skip"].is_array()) {
            for (auto& s : step.params["skip"]) skip.push_back(s.get<std::string>());
        }
        if (src.empty() || dst.empty()) { error = "copy: missing source/target"; return false; }
        return exec::copyTree(src, dst, skip, nullptr, &error);
    }

    if (step.type == "extract") {
        std::string source = step.params.value("source", "");
        std::string target = v.interpolate(step.params.value("target", ""));
        std::vector<std::string> skip;
        if (step.params.contains("skip") && step.params["skip"].is_array()) {
            for (auto& s : step.params["skip"]) skip.push_back(s.get<std::string>());
        }
        if (source.empty() || target.empty()) { error = "extract: missing source/target"; return false; }

        if (source.rfind("zip:", 0) == 0) {
            return exec::extractZip(resolvePath(source.substr(4)), target,
                                    step.params.value("7za", ""), &error);
        }
        std::string srcErr;
        std::string plainSource;
        std::string kind = "dir";
        if (source.rfind("dir:", 0) == 0) {
            plainSource = resolvePath(source.substr(4));
        } else if (source.rfind("qrc:", 0) == 0) {
            plainSource = source.substr(4); // handled by the registered qt bridge
            kind = "qrc";
        } else {
            plainSource = resolvePath(source);
        }
        auto src = makeDeploySource(DeploySpec{kind + ":" + plainSource, skip}, srcErr);
        if (!src) { error = srcErr; return false; }
        std::string openErr;
        if (!src->open(openErr)) { error = openErr; return false; }
        std::vector<DeployEntry> entries;
        std::string enumErr;
        if (!src->enumerate(entries, enumErr)) { error = enumErr; return false; }

        // Directories first, then files in stable order.
        std::sort(entries.begin(), entries.end(),
                  [](const DeployEntry& a, const DeployEntry& b) {
                      if (a.isDir != b.isDir) return a.isDir;
                      return a.relPath < b.relPath;
                  });
        if (!exec::mkdirs(target)) { error = "extract: cannot create target"; return false; }

        long long total = 0, done = 0;
        for (auto& e : entries) {
            bool skipped = false;
            for (auto& pat : skip)
                if (wildcardMatch(pat, e.relPath)) { skipped = true; break; }
            if (!skipped && !e.isDir) ++total;
        }
        for (auto& e : entries) {
            if (ctx_.cancelled()) { error = "cancelled"; return false; }
            bool skipped = false;
            for (auto& pat : skip)
                if (wildcardMatch(pat, e.relPath)) { skipped = true; break; }
            if (skipped) continue;

            std::string dest = port::joinPath(target, e.relPath);
            if (e.isDir) {
                exec::mkdirs(dest);
                continue;
            }
            std::vector<char> buf;
            std::string readErr;
            if (!src->readFile(e.relPath, buf, readErr)) { error = readErr; return false; }
            fs::path relFs = fs::u8path(e.relPath);
            std::string parent = relFs.parent_path().empty()
                ? target : port::joinPath(target, relFs.parent_path().generic_u8string());
            exec::mkdirs(parent);
            std::ofstream out(fs::u8path(dest), std::ios::binary | std::ios::trunc);
            if (!out) { error = "extract: cannot write " + e.relPath; return false; }
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            out.close();
            ++done;
            if (ui_ && total > 0)
                ui_->onProgress(step.id, static_cast<int>(done * 100 / total), e.relPath);
        }
        return true;
    }

    if (step.type == "download") {
        std::string dest = resolvePath(v.interpolate(step.params.value("dest", "")));
        std::string url;
        if (step.params.contains("url")) {
            url = v.interpolate(step.params["url"].get<std::string>());
        } else if (step.params.contains("asset")) {
            std::string repo = step.params["asset"].get<std::string>();
            std::string variant = step.params.value("variant", "");
            if (variant.empty()) { error = "download(asset): missing variant"; return false; }
            url = fetchGitHubAssetUrl(repo, variant, &error);
            if (url.empty()) return false;
        } else {
            error = "download: missing url or asset";
            return false;
        }
        if (dest.empty()) { error = "download: missing dest"; return false; }
        return exec::downloadFile(url, dest, nullptr, &error);
    }

    if (step.type == "run") {
        std::vector<std::string> cmd;
        if (step.params.contains("program"))
            cmd.push_back(v.interpolate(step.params["program"].get<std::string>()));
        if (step.params.contains("args") && step.params["args"].is_array()) {
            for (auto& a : step.params["args"])
                cmd.push_back(v.interpolate(a.get<std::string>()));
        }
        if (cmd.empty()) { error = "run: missing program"; return false; }
        exec::ProcessResult r;
        if (!exec::runProcess(cmd, step.params.value("waitMs", 60000), r)) {
            error = "run: launch failed";
            return false;
        }
        if (r.timedOut) { error = "run: timed out (" + cmd[0] + ")"; return false; }
        if (r.exitCode != 0) { error = "run: exit " + std::to_string(r.exitCode) + " (" + cmd[0] + ")"; return false; }
        ctx_.vars().set("run.output", r.output);
        return true;
    }

    if (step.type == "shortcut") {
        // "enabled": false lets the flow skip shortcut creation entirely
        // (test/quiet installs); which shortcuts exist is the step list.
        if (!step.params.value("enabled", true)) {
            if (bus_) bus_->publish("hci/step", {{"id", step.id}, {"state", "skipped"}});
            return true;
        }
        exec::ShortcutKind kind = step.params.value("kind", "desktop") == "startmenu"
            ? exec::ShortcutKind::StartMenu : exec::ShortcutKind::Desktop;
        std::string name = step.params.value("name", "");
        std::string target = v.interpolate(step.params.value("target", ""));
        std::string workDir = v.interpolate(step.params.value("workDir", ""));
        std::string args = step.params.value("args", "");
        if (name.empty() || target.empty()) { error = "shortcut: missing name/target"; return false; }
        return exec::createShortcut(kind, name, target, workDir, args, &error);
    }

    if (step.type == "template") {
        std::string file = step.params.value("file", "install.conf");
        std::string dest = v.interpolate(step.params.value("target", "{installDir}"));
        std::string path = port::joinPath(dest, file);
        std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
        if (!out) { error = "template: cannot write " + path; return false; }
        std::string header = step.params.value("header", "");
        if (!header.empty()) out << header << "\n";
        if (step.params.contains("template") && step.params["template"].is_object()) {
            for (auto it = step.params["template"].begin(); it != step.params["template"].end(); ++it) {
                std::string val = it.value().is_string()
                    ? v.interpolate(it.value().get<std::string>())
                    : it.value().dump();
                out << it.key() << "=" << val << "\n";
            }
        }
        out.close();
        return true;
    }

    if (step.type == "registry") {
        std::string key = v.interpolate(step.params.value("key", ""));
        if (step.params.value("action", "write") == "delete")
            return exec::registryDeleteKey(key);
        std::string name = step.params.value("name", "");
        std::string value = v.interpolate(step.params.value("value", ""));
        if (key.empty()) { error = "registry: missing key"; return false; }
        return exec::registryWriteString(key, name, value);
    }

    // Extension-provided step types (M2 wiring).
    if (registry_) {
        std::string extErr;
        if (registry_->runStep(step.type, step.params, ctx_, extErr)) {
            return true;
        }
        if (!extErr.empty()) {
            error = "step '" + step.type + "': " + extErr;
            return false;
        }
    }
    error = "unknown step type: " + step.type;
    return false;
}

// ------------------------------------------------------------------
// entry::runInstall (library mode) - implemented here (hci_flow links core)
// ------------------------------------------------------------------
namespace entry {

namespace {
class NullUi : public IFlowUi {
public:
    bool onWelcome(const std::string&, const ProductConfig&) override { return true; }
    bool onLicense(const std::string&, bool& accepted) override { accepted = true; return true; }
    bool onPath(std::string&, const std::string&) override { return false; }
    bool onComponents(const std::vector<ProductComponent>&, std::vector<bool>&) override { return false; }
    bool onOption(const std::string&, const std::vector<std::string>&, int&) override { return false; }
    bool onConfirm(const std::string&, bool&) override { return false; }
    bool onInput(const std::string&, std::string&, bool) override { return false; }
    void onProgress(const std::string& step, int percent, const std::string&) override
    { Log::Info(fmt("[flow] {} {}%", step, percent)); }
    void onMessage(const std::string& text, bool isError) override
    { if (isError) Log::Error(text); else Log::Info(text); }
    void onFinish(bool success, const std::string& message, const std::string&,
                  const std::vector<std::string>&) override
    { Log::Info(fmt("[flow] finish success={} {}", success ? "yes" : "no", message)); }
};
} // namespace

int runInstall(const ProductConfig& product, const std::string& flowPath,
               const EntryOptions& options)
{
    try {
        InstallContext ctx;
        if (!options.installPath.empty())
            ctx.vars().set("installDir", options.installPath);
        if (!options.language.empty())
            ctx.vars().set("language", options.language);
        ctx.vars().setBool("silent", options.silent);

        FlowSpec flow = FlowSpec::loadFile(resolveFlowPath(flowPath));
        auto script = createLuaEngine();
        NullUi ui;
        ExtensionRegistry registry;
        FlowRunner runner(const_cast<ProductConfig&>(product), ctx, &ui, script.get());
        runner.setRegistry(&registry);
        return runner.run(flow);
    } catch (const std::exception& e) {
        Log::Error(std::string("runInstall: ") + e.what());
        return 1;
    }
}

} // namespace entry

} // namespace hci