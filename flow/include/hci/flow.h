#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hci/bus.h"
#include "hci/context.h"
#include "hci/extension_registry.h"
#include "hci/product.h"
#include "hci/script.h"

namespace hci {

// ------------------------------------------------------------------
// Flow script: JSON declarative step list + optional Lua script blocks.
//
// {
//   "id": "install",
//   "vars": { "k": "v", ... },          // seed variables
//   "steps": [
//     { "id": "license", "ui": "license", "source": "...", "next": "..." },
//     { "id": "extract", "type": "extract", "source": "...", "target": "{installDir}",
//       "skip": ["..."], "when": "lua-expr", "onFail": "abort|ignore" },
//     { "id": "s1",     "type": "script", "script": "<lua code>" },
//     ...
//   ]
// }
// ------------------------------------------------------------------
struct FlowStep {
    std::string id;
    std::string type;   // exec type; empty when 'ui' is present (interactive)
    std::string ui;     // welcome|license|path|components|option|confirm|input|finish
    std::string next;   // explicit next step id ("__end" to finish)
    std::string when;   // Lua expression; empty = always run
    std::string onFail; // "abort" (default) | "ignore"
    nlohmann::json params; // rest of step fields (source/target/skip/url/...)
};

struct FlowSpec {
    std::string id;
    Vars seedVars;
    std::vector<FlowStep> steps;

    static FlowSpec loadFile(const std::string& jsonPath);
    static FlowSpec loadString(const std::string& jsonText);
};

// ------------------------------------------------------------------
// Shell UI provider: interactive steps delegate rendering here.
// Returning false from an interaction aborts the run (user cancel).
// ------------------------------------------------------------------
class IFlowUi {
public:
    virtual ~IFlowUi() = default;

    virtual bool onWelcome(const std::string& productName,
                           const ProductConfig& product) = 0;
    virtual bool onLicense(const std::string& text, bool& accepted) = 0;
    virtual bool onPath(std::string& path, const std::string& defaultPath) = 0;
    virtual bool onComponents(const std::vector<ProductComponent>& components,
                              std::vector<bool>& checked) = 0;
    virtual bool onOption(const std::string& prompt,
                          const std::vector<std::string>& choices,
                          int& selected) = 0;
    virtual bool onConfirm(const std::string& prompt, bool& yes) = 0;
    virtual bool onInput(const std::string& prompt, std::string& value,
                         bool required) = 0;
    virtual void onProgress(const std::string& step, int percent,
                            const std::string& detail) = 0;
    virtual void onMessage(const std::string& text, bool isError) = 0;
    virtual void onFinish(bool success, const std::string& message,
                          const std::string& launchExe = "",
                          const std::vector<std::string>& launchOptions = {}) = 0;

    // Git strategy selection ("git" ui step): 'systemAvailable' is the
    // auto-detection result; 'mode' in ("", "system", "bundled",
    // "install-system") with 'def' as the suggested default; the picker may
    // offer the system-install option when 'showInstallSystem' is true.
    // Returns false = user cancelled.
    virtual bool onGit(bool systemAvailable, std::string& mode,
                       const std::string& def, bool showInstallSystem = false)
    {
        mode = def;
        return true;
    }

    // Language selection (optional first step, "language" ui id). Default
    // implementation accepts the prescribed default; shells may show a picker.
    virtual bool onLanguage(std::string& selected, const std::string& def)
    {
        selected = def;
        return true;
    }

    // Called right before each interactive step: lets the UI adjust its
    // chrome from step params (e.g. "buttons": {"back": false, ...}) and
    // whether a previous step exists (canGoBack).
    virtual void onStepParam(const nlohmann::json& params, bool canGoBack) {}

    // When an interaction returns false, the runner checks this: true means
    // the user asked to go back to the previous step (not a cancel/abort).
    virtual bool backRequested() const { return false; }

    // Elevation request (start of flow via product elevation spec, or the
    // "elevate" ui step). Default: announce + relaunch as admin, then exit
    // this (unprivileged) process. Return false = user declined / cannot
    // elevate: the run continues WITHOUT privileges (v1).
    virtual bool onElevate(const std::string& reason, bool autoRestart);
    // (declaration lives here; default implementation in flow.cpp keeps the
    //  core-free shells working; shells may override to prompt first)
};

// ------------------------------------------------------------------
// FlowRunner: executes a FlowSpec against a context/product.
// ------------------------------------------------------------------
class FlowRunner {
public:
    FlowRunner(ProductConfig& product, InstallContext& ctx,
               IFlowUi* ui, IScriptEngine* script);

    // Optional: reads embedded resources (qrc: sources). Core stays zero-Qt;
    // shells (GUI) supply a reader. Receives the path after "qrc:".
    using ResourceReader = std::function<bool(const std::string& path,
                                              std::string& out)>;
    void setResourceReader(ResourceReader reader) { resourceReader_ = std::move(reader); }

    void setEventBus(EventBus* bus) { bus_ = bus; } // optional (M2 wiring)
    // Explicit extension registry (host-owned; extension DLLs register into the
    // SAME instance). Required for extension step types.
    void setRegistry(ExtensionRegistry* registry) { registry_ = registry; }
    // Base directory for relative paths in flow steps (flow file location).
    void setBaseDir(const std::string& dir) { baseDir_ = dir; }

    // Runs the flow; returns 0 on success, 1 on failure/cancel.
    int run(FlowSpec& flow);

private:
    bool runStep(FlowSpec& flow, FlowStep& step, size_t& index, std::string& error);
    // Returns false on condition error; 'shouldRun' = false means skip the step.
    bool evalWhen(const std::string& expr, bool& shouldRun, std::string& error);
    bool handleUiStep(FlowStep& step, std::string& error);
    bool handleExecStep(FlowStep& step, std::string& error);
    std::string resolvePath(const std::string& p) const;

    ProductConfig& product_;
    InstallContext& ctx_;
    IFlowUi* ui_;
    IScriptEngine* script_;
    EventBus* bus_ = nullptr;
    ExtensionRegistry* registry_ = nullptr;
    ResourceReader resourceReader_;
    std::string baseDir_;
    std::vector<size_t> history_; // executed step indexes (back navigation)
};

} // namespace hci