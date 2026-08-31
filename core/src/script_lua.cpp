#include "hci/script.h"

#include <lua.hpp>

#include <stdexcept>

namespace hci {

LuaEngine::LuaEngine() : state_(nullptr)
{
    lua_State* L = luaL_newstate();
    if (!L) throw std::runtime_error("lua: cannot create state");
    luaL_openlibs(L);
    state_ = L;
}

LuaEngine::~LuaEngine()
{
    if (state_) lua_close(static_cast<lua_State*>(state_));
}

void LuaEngine::pushVars(const Vars& vars)
{
    lua_State* L = static_cast<lua_State*>(state_);
    lua_newtable(L);
    for (auto& kv : vars.all()) {
        lua_pushstring(L, kv.first.c_str());
        lua_pushstring(L, kv.second.c_str());
        lua_settable(L, -3);
    }
    lua_setglobal(L, "vars");
}

bool LuaEngine::eval(const std::string& expression, const Vars& vars,
                     std::string& out)
{
    lua_State* L = static_cast<lua_State*>(state_);
    pushVars(vars);

    std::string chunk = "return (" + expression + ")";
    if (luaL_loadstring(L, chunk.c_str()) != LUA_OK) {
        out = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        out = lua_tostring(L, -1) ? lua_tostring(L, -1) : "eval error";
        lua_pop(L, 1);
        return false;
    }

    int type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        out.clear();
    } else if (type == LUA_TBOOLEAN) {
        out = lua_toboolean(L, -1) ? "true" : "false";
    } else if (type == LUA_TNUMBER) {
        lua_Number n = lua_tonumber(L, -1);
        if (n == static_cast<lua_Integer>(n))
            out = std::to_string(static_cast<lua_Integer>(n));
        else
            out = std::to_string(n);
    } else {
        const char* s = lua_tostring(L, -1);
        out = s ? s : "";
    }
    lua_pop(L, 1);
    return true;
}

bool LuaEngine::run(const std::string& code, const Vars& vars, std::string& err)
{
    lua_State* L = static_cast<lua_State*>(state_);
    pushVars(vars);

    if (luaL_loadstring(L, code.c_str()) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "run error";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

std::shared_ptr<IScriptEngine> createLuaEngine()
{
    return std::make_shared<LuaEngine>();
}

} // namespace hci