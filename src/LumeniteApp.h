#pragma once
#include <string>
#include <unordered_map>
#include "Router.h"
#include "SessionManager.h"
#include "json/value.h"


extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}


/// The core app: loads Lua, registers bindings, runs your script
class LumeniteApp
{
public:
    LumeniteApp();

    ~LumeniteApp();

    void loadScript(const std::string &path);

private:
    lua_State *L;

    void exposeBindings();

    static int lua_route_get(lua_State *L);

    static int lua_route_post(lua_State *L);

    static int lua_route_put(lua_State *L);

    static int lua_route_delete(lua_State *L);

    static int lua_session_get(lua_State *L);

    static int lua_session_set(lua_State *L);

    static int lua_json(lua_State *L);

    static int lua_jsonify(lua_State *L);
    static int lua_from_json(lua_State *L);

    static int lua_render_template_string(lua_State *L);

    static int lua_render_template_file(lua_State *L);

    static int lua_register_template_filter(lua_State *L);

    static int lua_listen(lua_State *L);
};
