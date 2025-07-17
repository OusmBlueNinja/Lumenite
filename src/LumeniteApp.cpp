#include "LumeniteApp.h"
#include "Server.h"
#include "TemplateEngine.h"
#include <json/json.h>
#include <iostream>
#include <random>
#include <chrono>
#include <sstream>

// ————— SessionManager definitions —————
std::unordered_map<std::string, std::unordered_map<std::string, std::string> > SessionManager::store;
thread_local std::string SessionManager::currentId;
thread_local bool SessionManager::isNew = false;

static std::string make_id()
{
    uint64_t r = std::mt19937_64(
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())
    )();

    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(r));
    return std::string(buffer);
}

void SessionManager::start(HttpRequest &req, HttpResponse &res)
{
    auto it = req.headers.find("Cookie");
    if (it != req.headers.end()) {
        const std::string &cookies = it->second;
        size_t p = cookies.find("LUMENITE_SESSION=");
        if (p != std::string::npos) {
            size_t start = p + strlen("LUMENITE_SESSION=");
            size_t end = cookies.find(';', start);
            currentId = cookies.substr(start, (end == std::string::npos ? cookies.size() : end) - start);
        }
    }

    auto sessionIt = store.find(currentId);
    if (currentId.empty() || sessionIt == store.end()) {
        currentId = make_id();
        store[currentId] = {};
        res.headers["Set-Cookie"] = "LUMENITE_SESSION=" + currentId + "; Path=/; HttpOnly";
    }
}

std::string SessionManager::get(const std::string &key)
{
    auto &session = store[currentId];
    auto it = session.find(key);
    return it == session.end() ? "" : it->second;
}

void SessionManager::set(const std::string &key, const std::string &val)
{
    store[currentId][key] = val;
}

// ————— Recursive Lua→JSON converter —————
static Json::Value lua_to_json(lua_State *L, int idx)
{
    if (lua_istable(L, idx)) {
        lua_pushvalue(L, idx); // copy
        int tbl = lua_gettop(L);
        bool isArray = true;
        std::vector<Json::Value> array;
        Json::Value object(Json::objectValue);

        lua_pushnil(L);
        while (lua_next(L, tbl) != 0) {
            Json::Value v = lua_to_json(L, -1);

            if (lua_type(L, -2) == LUA_TNUMBER) {
                lua_Number n = lua_tonumber(L, -2);
                int i = static_cast<int>(n);
                if (n == i && i >= 1) {
                    if (i > static_cast<int>(array.size())) array.resize(i);
                    array[i - 1] = v;
                } else {
                    isArray = false;
                    object[std::to_string(n)] = v;
                }
            } else if (lua_type(L, -2) == LUA_TSTRING) {
                isArray = false;
                const char *s = lua_tostring(L, -2);
                object[s] = v;
            } else {
                isArray = false;
                const char *s = lua_tostring(L, -2);
                object[s ? s : ""] = v;
            }

            lua_pop(L, 1);
        }

        lua_pop(L, 1); // pop table copy

        if (isArray) {
            Json::Value arr(Json::arrayValue);
            for (auto &el: array) arr.append(el);
            return arr;
        }

        return object;
    } else if (lua_isboolean(L, idx)) {
        return Json::Value((bool) lua_toboolean(L, idx));
    } else if (lua_isinteger(L, idx)) {
        return Json::Value((Json::Int64) lua_tointeger(L, idx));
    } else if (lua_isnumber(L, idx)) {
        return Json::Value((double) lua_tonumber(L, idx));
    } else if (lua_isstring(L, idx)) {
        return Json::Value(lua_tostring(L, idx));
    }

    return Json::Value(); // null
}

// ————— LumeniteApp implementation —————
LumeniteApp::LumeniteApp()
{
    L = luaL_newstate();
    luaL_openlibs(L);
    exposeBindings();
}

LumeniteApp::~LumeniteApp()
{
    lua_close(L);
}

void LumeniteApp::loadScript(const std::string &path)
{
    if (luaL_dofile(L, path.c_str())) {
        std::cerr << "[Lua Error] " << lua_tostring(L, -1) << "\n";
    }
}

void LumeniteApp::exposeBindings()
{
    lua_newtable(L); // app

    lua_pushcfunction(L, lua_route_get);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, lua_route_post);
    lua_setfield(L, -2, "post");
    lua_pushcfunction(L, lua_route_put);
    lua_setfield(L, -2, "put");
    lua_pushcfunction(L, lua_route_delete);
    lua_setfield(L, -2, "delete");

    lua_pushcfunction(L, lua_session_get);
    lua_setfield(L, -2, "session_get");
    lua_pushcfunction(L, lua_session_set);
    lua_setfield(L, -2, "session_set");

    lua_pushcfunction(L, lua_json);
    lua_setfield(L, -2, "json");

    lua_pushcfunction(L, lua_render_template_string);
    lua_setfield(L, -2, "render_template_string");
    lua_pushcfunction(L, lua_render_template_file);
    lua_setfield(L, -2, "render_template");

    lua_pushcfunction(L, lua_listen);
    lua_setfield(L, -2, "listen");

    lua_setglobal(L, "app");
}

// — routing helper —
static bool extract_route_args(lua_State *L, const char *name, std::string &outPath, int &outHandlerIdx)
{
    int n = lua_gettop(L);
    if (n == 2 && lua_isstring(L, 1) && lua_isfunction(L, 2)) {
        outPath = lua_tostring(L, 1);
        outHandlerIdx = 2;
        return true;
    }
    if (n == 3 && lua_istable(L, 1) && lua_isstring(L, 2) && lua_isfunction(L, 3)) {
        outPath = lua_tostring(L, 2);
        outHandlerIdx = 3;
        return true;
    }
    luaL_error(L, "%s(path, handler) or %s:path(handler) expected", name, name);
    return false;
}

// — routing bindings —
int LumeniteApp::lua_route_get(lua_State *L)
{
    std::string path;
    int hidx;
    extract_route_args(L, "get", path, hidx);
    lua_pushvalue(L, hidx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Router::add("GET", path, ref);
    return 0;
}

int LumeniteApp::lua_route_post(lua_State *L)
{
    std::string path;
    int hidx;
    extract_route_args(L, "post", path, hidx);
    lua_pushvalue(L, hidx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Router::add("POST", path, ref);
    return 0;
}

int LumeniteApp::lua_route_put(lua_State *L)
{
    std::string path;
    int hidx;
    extract_route_args(L, "put", path, hidx);
    lua_pushvalue(L, hidx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Router::add("PUT", path, ref);
    return 0;
}

int LumeniteApp::lua_route_delete(lua_State *L)
{
    std::string path;
    int hidx;
    extract_route_args(L, "delete", path, hidx);
    lua_pushvalue(L, hidx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Router::add("DELETE", path, ref);
    return 0;
}

// — session bindings —
int LumeniteApp::lua_session_get(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    lua_pushstring(L, SessionManager::get(k).c_str());
    return 1;
}

int LumeniteApp::lua_session_set(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    const char *v = luaL_checkstring(L, 2);
    SessionManager::set(k, v);
    return 0;
}

// — JSON response builder —
int LumeniteApp::lua_json(lua_State *L)
{
    if (!lua_istable(L, 1))
        return luaL_error(L, "json(table) expected");

    Json::Value root = lua_to_json(L, 1);

    Json::StreamWriterBuilder w;
    w["indentation"] = ""; // no pretty print
    std::string payload = Json::writeString(w, root);

    lua_newtable(L);

    lua_pushstring(L, "status");
    lua_pushinteger(L, 200);
    lua_settable(L, -3);

    lua_pushstring(L, "headers");
    lua_newtable(L);
    lua_pushstring(L, "Content-Type");
    lua_pushstring(L, "application/json");
    lua_settable(L, -3);
    lua_settable(L, -3);

    lua_pushstring(L, "body");
    lua_pushlstring(L, payload.c_str(), payload.size());
    lua_settable(L, -3);

    return 1;
}


int LumeniteApp::lua_render_template_string(lua_State *L)
{
    const char *tmpl = luaL_checkstring(L, 1);
    std::unordered_map<std::string, std::string> ctx;

    // Second argument is optional
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        lua_pushnil(L); // First key for lua_next
        while (lua_next(L, 2)) {
            // Ensure key is a string
            if (!lua_isstring(L, -2)) {
                lua_pop(L, 1); // Skip this entry
                continue;
            }

            const char *k = lua_tostring(L, -2);
            std::string value;

            if (lua_isstring(L, -1)) {
                value = lua_tostring(L, -1);
            } else if (lua_isnumber(L, -1)) {
                value = std::to_string(lua_tonumber(L, -1));
            } else if (lua_isboolean(L, -1)) {
                value = lua_toboolean(L, -1) ? "true" : "false";
            } else if (lua_isnil(L, -1)) {
                value = "nil";
            } else {
                value = "[object]";
            }

            if (k) ctx.emplace(k, value);
            lua_pop(L, 1); // Remove value, keep key for next iteration
        }
    }

    std::string out = TemplateEngine::renderFromString(tmpl, ctx);
    lua_pushstring(L, out.c_str());
    return 1;
}


int LumeniteApp::lua_render_template_file(lua_State *L)
{
    const char *fn = luaL_checkstring(L, 1);
    std::unordered_map<std::string, std::string> ctx;

    // Second argument is optional
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        lua_pushnil(L); // First key for lua_next
        while (lua_next(L, 2)) {
            // Ensure key is a string
            if (!lua_isstring(L, -2)) {
                lua_pop(L, 1); // Skip this entry
                continue;
            }

            const char *k = lua_tostring(L, -2);
            std::string value;

            if (lua_isstring(L, -1)) {
                value = lua_tostring(L, -1);
            } else if (lua_isnumber(L, -1)) {
                value = std::to_string(lua_tonumber(L, -1));
            } else if (lua_isboolean(L, -1)) {
                value = lua_toboolean(L, -1) ? "true" : "false";
            } else if (lua_isnil(L, -1)) {
                value = "nil";
            } else {
                value = "[object]";
            }

            if (k) ctx.emplace(k, value);
            lua_pop(L, 1); // Remove value, keep key for next iteration
        }
    }

    std::string tmpl = TemplateEngine::loadTemplate(fn);
    std::string out = TemplateEngine::renderFromString(tmpl, ctx);
    lua_pushstring(L, out.c_str());
    return 1;
}



// — listen binding —
int LumeniteApp::lua_listen(lua_State *L)
{
    int nargs = lua_gettop(L), port;
    if (nargs == 1 && lua_isinteger(L, 1)) port = lua_tointeger(L, 1);
    else if (nargs >= 2 && lua_isinteger(L, 2)) port = lua_tointeger(L, 2);
    else return luaL_error(L, "expected an integer port as argument");

    Server srv(port, L);
    srv.run();
    return 0;
}
