#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hci/bus.h"
#include "hci/context.h"
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
                          const std::string& launchExe = "") = 0;
};

// ------------------------------------------------------------------
// FlowRunner: executes a FlowSpec against a context/product.
// ------------------------------------------------------------------
class FlowRunner {
public:
    FlowRunner(ProductConfig& product, InstallContext& ctx,
               IFlowUi* ui, IScriptEngine* script);

    void setEventBus(EventBus* bus) { bus_ = bus; } // optional (M2 wiring)
    // Base directory for relative paths in flow steps (flow file location).
    void setBaseDir(const std::string& dir) { baseDir_ = dir; }

    // Runs the flow; returns 0 on success, 1 on failure/cancel.
    int run(FlowSpec& flow);

private:
    bool runStep(FlowSpec& flow, FlowStep& step, size_t& index, std::string& error);
    bool evalWhen(const std::string& expr, std::string& error);
    bool handleUiStep(FlowStep& step, std::string& error);
    bool handleExecStep(FlowStep& step, std::string& error);
    std::string resolvePath(const std::string& p) const;

    ProductConfig& product_;
    InstallContext& ctx_;
    IFlowUi* ui_;
    IScriptEngine* script_;
    EventBus* bus_ = nullptr;
    std::string baseDir_;
};

} // namespace hci