#pragma once
#include <string>
#include <unordered_map>
#include "Router.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// forward‑declare the HTTP structs
struct HttpRequest;
struct HttpResponse;

/// Simple in‑memory session store exposed to Lua
class SessionManager
{
public:
    static void start(HttpRequest &req, HttpResponse &res);

    static std::string get(const std::string &key);

    static void set(const std::string &key, const std::string &val);

private:
    static std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::string>
    > store;
    static thread_local std::string currentId;
    static thread_local bool isNew;
};

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

    // routing
    static int lua_route_get(lua_State *L);

    static int lua_route_post(lua_State *L);

    static int lua_route_put(lua_State *L);

    static int lua_route_delete(lua_State *L);

    // session
    static int lua_session_get(lua_State *L);

    static int lua_session_set(lua_State *L);

    // JSON helper
    static int lua_json(lua_State *L);

    // templating
    static int lua_render_template_string(lua_State *L);

    static int lua_render_template_file(lua_State *L);

    static int lua_listen(lua_State* L);

};
