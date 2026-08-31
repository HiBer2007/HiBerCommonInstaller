#pragma once

#include <memory>
#include <string>

#include "hci/vars.h"

namespace hci {

// Script engine interface (embedded interpreter; compiled in, no external
// runtime). Default implementation: Lua 5.4 (LuaEngine).
class IScriptEngine {
public:
    virtual ~IScriptEngine() = default;

    // Evaluate a single expression; result is rendered into 'out'.
    virtual bool eval(const std::string& expression, const Vars& vars,
                      std::string& out) = 0;

    // Run a script chunk (may be multiple statements); 'err' carries failure detail.
    virtual bool run(const std::string& code, const Vars& vars,
                     std::string& err) = 0;

    virtual const char* name() const = 0;
};

// Lua 5.4 implementation (static-linked).
class LuaEngine : public IScriptEngine {
public:
    LuaEngine();
    ~LuaEngine() override;

    bool eval(const std::string& expression, const Vars& vars,
              std::string& out) override;
    bool run(const std::string& code, const Vars& vars,
             std::string& err) override;
    const char* name() const override { return "lua"; }

private:
    void* state_; // lua_State*
    void pushVars(const Vars& vars);
};

// Script engine factory (single instance per run is fine; engines are stateless
// apart from their interpreter state).
std::shared_ptr<IScriptEngine> createLuaEngine();

} // namespace hci