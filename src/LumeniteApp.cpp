#include "LumeniteApp.h"
#include "Server.h"            // for HttpRequest/HttpResponse
#include "TemplateEngine.h"
#include <json/json.h>
#include <iostream>
#include <random>
#include <chrono>
#include <sstream>

// ————— SessionManager definitions —————
std::unordered_map<std::string, std::unordered_map<std::string, std::string> >
SessionManager::store;
thread_local std::string SessionManager::currentId;
thread_local bool SessionManager::isNew = false;

static std::string make_id()
{
    auto now = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
    std::mt19937_64 rng((uint64_t) now);
    uint64_t r = rng();
    std::ostringstream ss;
    ss << std::hex << r;
    return ss.str();
}

void SessionManager::start(HttpRequest &req, HttpResponse &res)
{
    auto it = req.headers.find("Cookie");
    if (it != req.headers.end()) {
        std::string cookies = it->second;
        size_t p = cookies.find("LUMENITE_SESSION=");
        if (p != std::string::npos) {
            size_t start = p + strlen("LUMENITE_SESSION=");
            size_t end = cookies.find(';', start);
            currentId = cookies.substr(start,
                                       (end == std::string::npos ? cookies.size() : end) - start
            );
        }
    }
    if (currentId.empty() || store.find(currentId) == store.end()) {
        currentId = make_id();
        store[currentId] = {};
        res.headers["Set-Cookie"] =
                "LUMENITE_SESSION=" + currentId + "; Path=/; HttpOnly";
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
        // push a copy of the table to the top
        lua_pushvalue(L, idx);
        int tbl = lua_gettop(L);

        bool isArray = true;
        std::vector<Json::Value> array;
        Json::Value object(Json::objectValue);

        lua_pushnil(L);
        while (lua_next(L, tbl) != 0) {
            // key at -2, value at -1
            Json::Value v = lua_to_json(L, -1);

            if (lua_type(L, -2) == LUA_TNUMBER) {
                lua_Number n = lua_tonumber(L, -2);
                int i = (int) n;
                if (n == i && i >= 1) {
                    if (i > (int) array.size()) array.resize(i);
                    array[i - 1] = v;
                } else {
                    isArray = false;
                    auto key = std::to_string(n);
                    object[key] = v;
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
        lua_pop(L, 1); // pop the table copy

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

    // routing
    lua_pushcfunction(L, lua_route_get);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, lua_route_post);
    lua_setfield(L, -2, "post");
    lua_pushcfunction(L, lua_route_put);
    lua_setfield(L, -2, "put");
    lua_pushcfunction(L, lua_route_delete);
    lua_setfield(L, -2, "delete");

    // sessions
    lua_pushcfunction(L, lua_session_get);
    lua_setfield(L, -2, "session_get");
    lua_pushcfunction(L, lua_session_set);
    lua_setfield(L, -2, "session_set");

    // JSON
    lua_pushcfunction(L, lua_json);
    lua_setfield(L, -2, "json");

    // templating
    lua_pushcfunction(L, lua_render_template_string);
    lua_setfield(L, -2, "render_template_string");
    lua_pushcfunction(L, lua_render_template_file);
    lua_setfield(L, -2, "render_template");

    // listen
    lua_pushcfunction(L, lua_listen);
    lua_setfield(L, -2, "listen");

    lua_setglobal(L, "app");
}

// — routing bindings —
// (extracts path & handler from either dot- or colon-style calls)
static bool extract_route_args(lua_State *L,
                               const char *name,
                               std::string &outPath,
                               int &outHandlerIdx)
{
    int n = lua_gettop(L);
    // dot‑style: (path, handler)
    if (n == 2 && lua_isstring(L, 1) && lua_isfunction(L, 2)) {
        outPath = lua_tostring(L, 1);
        outHandlerIdx = 2;
        return true;
    }
    // colon‑style: (self, path, handler)
    if (n == 3 && lua_istable(L, 1)
        && lua_isstring(L, 2)
        && lua_isfunction(L, 3)) {
        outPath = lua_tostring(L, 2);
        outHandlerIdx = 3;
        return true;
    }
    luaL_error(L,
               "%s(path, handler) or %s:path(handler) expected",
               name, name);
    return false;
}

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

// — JSON helper (recursive) —
// LumeniteApp.cpp (only the lua_json binding)

int LumeniteApp::lua_json(lua_State *L)
{
    // Expect a Lua table as input
    if (!lua_istable(L, 1))
        return luaL_error(L, "json(table) expected");

    // Convert the Lua table at stack index 1 → Json::Value
    Json::Value root = lua_to_json(L, 1);

    // Serialize to string
    Json::StreamWriterBuilder w;
    std::string payload = Json::writeString(w, root);

    // Build a Lua table { status=200, headers={["Content-Type"]="application/json"}, body=payload }
    lua_newtable(L);

    // status = 200
    lua_pushstring(L, "status");
    lua_pushinteger(L, 200);
    lua_settable(L, -3);

    // headers = { ["Content-Type"] = "application/json" }
    lua_pushstring(L, "headers");
    lua_newtable(L);
    lua_pushstring(L, "Content-Type");
    lua_pushstring(L, "application/json");
    lua_settable(L, -3);
    lua_settable(L, -3);

    // body = <payload>
    lua_pushstring(L, "body");
    lua_pushlstring(L, payload.c_str(), payload.size());
    lua_settable(L, -3);

    // Return that single table
    return 1;
}



// — templating bindings —
int LumeniteApp::lua_render_template_string(lua_State *L)
{
    const char *tmpl = luaL_checkstring(L, 1);
    luaL_checktype(L, 2,LUA_TTABLE);
    std::unordered_map<std::string, std::string> ctx;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        ctx[lua_tostring(L, -2)] = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    std::string out = TemplateEngine::renderFromString(tmpl, ctx);
    lua_pushstring(L, out.c_str());
    return 1;
}

int LumeniteApp::lua_render_template_file(lua_State *L)
{
    const char *fn = luaL_checkstring(L, 1);
    luaL_checktype(L, 2,LUA_TTABLE);
    std::string tmpl = TemplateEngine::loadTemplate(fn);
    std::unordered_map<std::string, std::string> ctx;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        ctx[lua_tostring(L, -2)] = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
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
    else return luaL_error(L, "expected a integer port as argument");
    Server srv(port, L);
    srv.run();
    return 0;
}
