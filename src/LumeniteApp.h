#pragma once
#include <string>
#include <unordered_map>
#include "Router.h"
#include "SessionManager.h"
#include "json/value.h"
#define PKG_MNGR_NAME "LPM"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

static void raise_http_abort(lua_State *L, int status);

class LumeniteApp
{
public:
    LumeniteApp();

    ~LumeniteApp();

    int loadScript(const std::string &path) const;

    static std::vector<int> before_request_refs;
    static std::vector<int> after_request_refs;
    static std::unordered_map<int, int> on_abort_refs;


    static bool listening;

private:
    lua_State *L;

    void exposeBindings();

    void injectBuiltins();


    static int lua_route_get(lua_State *L);

    static int lua_route_post(lua_State *L);

    static int lua_route_put(lua_State *L);

    static int lua_route_delete(lua_State *L);

    static int lua_session_get(lua_State *L);

    static int lua_session_set(lua_State *L);

    static int lua_json(lua_State *L);

    static int lua_send_file(lua_State *L);

    static int lua_jsonify(lua_State *L);

    static int lua_from_json(lua_State *L);

    static int lua_render_template_string(lua_State *L);

    static int lua_render_template_file(lua_State *L);

    static int lua_register_template_filter(lua_State *L);

    static int lua_before_request(lua_State *L);

    static int lua_after_request(lua_State *L);

    static int lua_abort(lua_State *L);

    static int lua_listen(lua_State *L);
};
