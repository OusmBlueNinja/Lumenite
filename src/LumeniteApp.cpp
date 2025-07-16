#include "LumeniteApp.h"
#include "Server.h"
#include "Router.h"
#include "TemplateEngine.h"

#include <iostream>
#include <sstream>
#include <unordered_map>

// ------------------- Template Rendering -------------------

static int lua_render_template_string(lua_State *L) {
    const char *tmpl = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    std::unordered_map<std::string, std::string> context;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        const char *key = lua_tostring(L, -2);
        const char *value = lua_tostring(L, -1);
        context[key] = value;
        lua_pop(L, 1);
    }

    std::string rendered = TemplateEngine::render(tmpl, context);
    lua_pushstring(L, rendered.c_str());
    return 1;
}

static int lua_render_template_file(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    std::string tmpl = TemplateEngine::loadTemplate(filename);

    std::unordered_map<std::string, std::string> context;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        const char *key = lua_tostring(L, -2);
        const char *value = lua_tostring(L, -1);
        context[key] = value;
        lua_pop(L, 1);
    }

    std::string rendered = TemplateEngine::render(tmpl, context);
    lua_pushstring(L, rendered.c_str());
    return 1;
}

// ------------------- JSON Encoder -------------------

static int lua_json(lua_State *L) {
    if (!lua_istable(L, 1)) {
        lua_pushstring(L, "{}");
        return 1;
    }

    std::ostringstream oss;
    oss << "{";
    lua_pushnil(L);
    bool first = true;
    while (lua_next(L, 1)) {
        if (!first) oss << ",";
        first = false;

        const char *key = lua_tostring(L, -2);
        if (!key) {
            lua_pop(L, 1);
            continue;
        }

        oss << "\"" << key << "\":";

        switch (lua_type(L, -1)) {
            case LUA_TSTRING:  oss << "\"" << lua_tostring(L, -1) << "\""; break;
            case LUA_TNUMBER:  oss << lua_tonumber(L, -1); break;
            case LUA_TBOOLEAN: oss << (lua_toboolean(L, -1) ? "true" : "false"); break;
            default:           oss << "null"; break;
        }

        lua_pop(L, 1);
    }
    oss << "}";
    lua_pushstring(L, oss.str().c_str());
    return 1;
}

// ------------------- LumeniteApp -------------------

LumeniteApp::LumeniteApp() {
    L = luaL_newstate();
    luaL_openlibs(L);
    exposeBindings();
}

LumeniteApp::~LumeniteApp() {
    lua_close(L);
}

void LumeniteApp::loadScript(const std::string &path) {
    if (luaL_dofile(L, path.c_str())) {
        std::cerr << "[Lua Error] " << lua_tostring(L, -1) << "\n";
    }
}

void LumeniteApp::exposeBindings() {
    lua_newtable(L); // app

    lua_pushcfunction(L, lua_get); lua_setfield(L, -2, "get");
    lua_pushcfunction(L, lua_listen); lua_setfield(L, -2, "listen");
    lua_pushcfunction(L, lua_json); lua_setfield(L, -2, "json");
    lua_pushcfunction(L, lua_render_template_string); lua_setfield(L, -2, "render_template_string");
    lua_pushcfunction(L, lua_render_template_file); lua_setfield(L, -2, "render_template");

    lua_setglobal(L, "app");
}

int LumeniteApp::lua_get(lua_State *L) {
    const char *route = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) return luaL_error(L, "Second argument must be a function");

    lua_pushvalue(L, 2); // copy handler
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Router::add(route, ref);
    return 0;
}

int LumeniteApp::lua_listen(lua_State *L) {
    int port = luaL_checkinteger(L, 1);
    Server server(port, L);
    server.run();
    return 0;
}
