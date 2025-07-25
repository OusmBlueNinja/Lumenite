#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include <json/json.h>
#include <curl/curl.h>

#include "ErrorHandler.h"
#include "LumeniteApp.h"
#include "Server.h"
#include "TemplateEngine.h"

#include "modules/LumeniteCrypto.h"
#include "modules/LumeniteDb.h"
#include "modules/LumeniteSafe.h"
#include "modules/ModuleBase.h"

#include "utils/MimeDetector.h"


bool running = false;

int LumeniteApp::before_request_ref = LUA_NOREF;
int LumeniteApp::after_request_ref = LUA_NOREF;
int LumeniteApp::on_abort_ref = LUA_NOREF;


#ifdef _WIN32
#include <windows.h>
#endif

void enableAnsiColors()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
#endif
}


static size_t WriteCallback(void *contents, const size_t size, size_t nmemb, std::string *output)
{
    const size_t totalSize = size * nmemb;
    output->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

// Create the abort call to the C++ backend to trigger the server raise
void raise_http_abort(lua_State *L, int status, const std::string &message = "")
{
    lua_newtable(L);

    lua_pushinteger(L, status);
    lua_setfield(L, -2, "status");

    if (!message.empty()) {
        lua_pushlstring(L, message.c_str(), message.size());
        lua_setfield(L, -2, "message");
    }

    lua_pushliteral(L, "__LUMENITE_ABORT__");
    lua_setfield(L, -2, "__kind");

    lua_error(L);
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
                object[""] = v;
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

// ————— Recursive JSON→Lua converter —————
static void json_to_lua(lua_State *L, const Json::Value &val)
{
    switch (val.type()) {
        case Json::nullValue:
            lua_pushnil(L);
            break;
        case Json::intValue:
        case Json::uintValue:
            lua_pushinteger(L, val.asInt64());
            break;
        case Json::realValue:
            lua_pushnumber(L, val.asDouble());
            break;
        case Json::stringValue:
            lua_pushstring(L, val.asCString());
            break;
        case Json::booleanValue:
            lua_pushboolean(L, val.asBool());
            break;
        case Json::arrayValue:
        {
            lua_newtable(L);
            for (Json::ArrayIndex i = 0; i < val.size(); ++i) {
                json_to_lua(L, val[i]);
                lua_rawseti(L, -2, i + 1);
            }
            break;
        }
        case Json::objectValue:
        {
            lua_newtable(L);
            for (const auto &key: val.getMemberNames()) {
                lua_pushstring(L, key.c_str());
                json_to_lua(L, val[key]);
                lua_settable(L, -3);
            }
            break;
        }
    }
}

// ————— Constructor / Destructor —————
LumeniteApp::LumeniteApp()
{
    enableAnsiColors();
    L = luaL_newstate();
    luaL_openlibs(L);
    exposeBindings();
    injectBuiltins();
}

LumeniteApp::~LumeniteApp()
{
    lua_close(L);
}


int LumeniteApp::loadScript(const std::string &path) const
{
    namespace fs = std::filesystem;

    if (!fs::exists(path)) {
        ErrorHandler::fileMissing(path);
        return 1;
    }

    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    int tracebackIndex = lua_gettop(L);

    int loadStatus = luaL_loadfile(L, path.c_str());
    if (loadStatus != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        ErrorHandler::invalidScript(err);
        return 2;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, tracebackIndex) != LUA_OK) {
        ErrorHandler::invalidScript(lua_tostring(L, -1));
        lua_pop(L, 1);
        return 2;
    }

    lua_remove(L, tracebackIndex);

    if (!running) {
        ErrorHandler::serverNotRunning();
        return 3;
    }

    return 0;
}


static int lua_http_get(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);

    CURL *curl = curl_easy_init();
    std::string response;
    long http_status = 0;
    CURLcode res;

    lua_newtable(L); // prepare return table

    if (!curl) {
        lua_pushstring(L, "status");
        lua_pushinteger(L, 0);
        lua_settable(L, -3);

        lua_pushstring(L, "body");
        lua_pushstring(L, "");
        lua_settable(L, -3);

        lua_pushstring(L, "error");
        lua_pushstring(L, "CURL initialization failed");
        lua_settable(L, -3);

        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);

        lua_pushstring(L, "status");
        lua_pushinteger(L, 0);
        lua_settable(L, -3);

        lua_pushstring(L, "body");
        lua_pushstring(L, "");
        lua_settable(L, -3);

        lua_pushstring(L, "error");
        lua_pushstring(L, curl_easy_strerror(res));
        lua_settable(L, -3);

        return 1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);

    lua_pushstring(L, "status");
    lua_pushinteger(L, http_status);
    lua_settable(L, -3);

    lua_pushstring(L, "body");
    lua_pushlstring(L, response.data(), response.size());
    lua_settable(L, -3);

    return 1;
}

int LumeniteApp::lua_before_request(lua_State *L)
{
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "app.before_request expects exactly 1 argument (a function)");
    }

    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "app.before_request expected a function like: app.before_request(function(req) ... end)");
    }

    if (before_request_ref != LUA_NOREF) {
        return luaL_error(L, "app.before_request has already been set. Only one before_request handler is allowed.");
    }

    lua_pushvalue(L, 1);
    before_request_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

int LumeniteApp::lua_after_request(lua_State *L)
{
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "app.after_request expects exactly 1 argument (a function)");
    }

    if (!lua_isfunction(L, 1)) {
        return luaL_error(
            L, "app.after_request expected a function like: app.after_request(function(req, res) ... end)");
    }

    if (after_request_ref != LUA_NOREF) {
        return luaL_error(L, "app.after_request has already been set. Only one after_request handler is allowed.");
    }

    lua_pushvalue(L, 1);
    after_request_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}


// ————— Lua API Binding Exposure —————
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

    lua_pushcfunction(L, lua_http_get);
    lua_setfield(L, -2, "http_get");

    lua_pushcfunction(L, lua_send_file);
    lua_setfield(L, -2, "send_file");


    lua_pushcfunction(L, lua_json);
    lua_setfield(L, -2, "json");
    lua_pushcfunction(L, lua_jsonify);
    lua_setfield(L, -2, "jsonify");
    lua_pushcfunction(L, lua_from_json);
    lua_setfield(L, -2, "from_json");

    lua_pushcfunction(L, lua_render_template_string);
    lua_setfield(L, -2, "render_template_string");
    lua_pushcfunction(L, lua_render_template_file);
    lua_setfield(L, -2, "render_template");
    lua_pushcfunction(L, lua_register_template_filter);
    lua_setfield(L, -2, "template_filter");


    lua_pushcfunction(L, lua_before_request);
    lua_setfield(L, -2, "before_request");

    lua_pushcfunction(L, lua_after_request);
    lua_setfield(L, -2, "after_request");

    lua_pushcfunction(L, lua_abort);
    lua_setfield(L, -2, "abort");


    lua_pushcfunction(L, lua_listen);
    lua_setfield(L, -2, "listen");

    lua_setglobal(L, "app");
}


static int builtin_module_loader(lua_State *L)
{
    const char *mod = luaL_checkstring(L, 1);
    std::string from;

    // Native C modules
    if (strcmp(mod, "lumenite.db") == 0) {
        from = "builtin";
        lua_pushcfunction(L, luaopen_lumenite_db);
    } else if (strcmp(mod, "lumenite.crypto") == 0) {
        from = "builtin";
        lua_pushcfunction(L, LumeniteCrypto::luaopen);
    } else if (strcmp(mod, "lumenite.safe") == 0) {
        from = "builtin";
        lua_pushcfunction(L, LumeniteSafe::luaopen);
    } else if (LumeniteModule::load(mod, L)) {
        from = "builtin";
    }

    if (!from.empty()) {
        printf(GREEN "[" PKG_MNGR_NAME "]" RESET " [%-22s] -> %s\n", from.c_str(), mod);
        return 1;
    }

    // Fallback: search for Lua script
    std::string relPath = std::string(mod);
    std::replace(relPath.begin(), relPath.end(), '.', '/'); // test.file → test/file

    std::vector<std::string> searchPaths = {
        relPath + ".lua",
        "plugins/" + relPath + ".lua"
    };

    for (const auto &path: searchPaths) {
        std::ifstream file(path);
        if (file.good()) {
            if (luaL_loadfile(L, path.c_str()) != LUA_OK) {
                lua_pushnil(L);
                lua_insert(L, -2);
                return 2;
            }

            printf(GREEN "[" PKG_MNGR_NAME "]" RESET " [%-22s] -> %s\n", path.c_str(), mod);
            return 1;
        }
    }

    // Not found
    lua_pushnil(L);
    lua_pushfstring(L, "[" PKG_MNGR_NAME "] No Lua module found for '%s'", mod);
    return 2;
}


void LumeniteApp::injectBuiltins()
{
    lua_getglobal(L, "package");
    lua_newtable(L); // New searchers table

    // Add our own single custom loader
    lua_pushcfunction(L, builtin_module_loader);
    lua_rawseti(L, -2, 1); // package.searchers[1] = builtin_module_loader

    lua_setfield(L, -2, "searchers"); // package.searchers = {...}
    lua_pop(L, 1); // pop package
}


// ————— Route Arg Helper —————
static bool extract_route_args(lua_State *L, const char *name, std::string &outPath, int &outHandlerIdx)
{
    const int n = lua_gettop(L);
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
    luaL_error(L, "%s(path, handler) expected", name);
    return false;
}


int LumeniteApp::lua_abort(lua_State *L)
{
    const lua_Integer status = luaL_checkinteger(L, 1);

    if (status < 100 || status > 599) {
        return luaL_error(L, "abort(status): status code must be between 100 and 599");
    }

    std::string message;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        size_t len;
        const char *msg = lua_tolstring(L, 2, &len);
        message.assign(msg, len);
    }

    raise_http_abort(L, static_cast<int>(status), message);
    return 0;
}


// ————— Route Handlers —————
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

// ————— Session Access —————
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

int LumeniteApp::lua_json(lua_State *L)
{
    const char *jsonStr = luaL_checkstring(L, 1);

    Json::CharReaderBuilder r;
    std::string errs;
    Json::Value root;
    std::istringstream ss(jsonStr);
    if (!Json::parseFromStream(r, ss, &root, &errs)) {
        return luaL_error(L, "Invalid JSON: %s", errs.c_str());
    }

    json_to_lua(L, root);
    return 1;
}


int LumeniteApp::lua_send_file(lua_State *L)
{
    const std::string path = luaL_checkstring(L, 1);

    bool as_attachment = false;
    std::string download_name;
    std::string content_type;
    std::unordered_map<std::string, std::string> extra_headers;
    int status = 200;

    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "as_attachment");
        if (lua_isboolean(L, -1)) as_attachment = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "download_name");
        if (lua_isstring(L, -1)) download_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "content_type");
        if (lua_isstring(L, -1)) content_type = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "status");
        if (lua_isinteger(L, -1)) status = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 2, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                    extra_headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1); // pop headers
    }

    // Read a file
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        raise_http_abort(L, 404, "File not found: " + path);
        luaL_error(L, "send_file: File not found: %s", path.c_str());
        return 0;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string body = buffer.str();

    if (content_type.empty()) {
        content_type = MimeDetector::toString(
            MimeDetector::detect(reinterpret_cast<const uint8_t *>(body.data()), body.size(), path)
        );
    }

    std::string disposition = as_attachment ? "attachment" : "inline";
    if (!download_name.empty()) {
        disposition += "; filename=\"" + download_name + "\"";
    }

    // Build response
    lua_newtable(L);

    lua_pushinteger(L, status);
    lua_setfield(L, -2, "status");

    lua_pushstring(L, body.c_str());
    lua_setfield(L, -2, "body");

    lua_newtable(L);
    lua_pushstring(L, content_type.c_str());
    lua_setfield(L, -2, "Content-Type");

    lua_pushstring(L, disposition.c_str());
    lua_setfield(L, -2, "Content-Disposition");

    // Merge custom headers
    for (const auto &[key, val]: extra_headers) {
        lua_pushstring(L, val.c_str());
        lua_setfield(L, -2, key.c_str());
    }

    lua_setfield(L, -2, "headers");

    return 1;
}


// ————— JSON Conversion —————
int LumeniteApp::lua_from_json(lua_State *L)
{
    const char *jsonStr = luaL_checkstring(L, 1);

    Json::CharReaderBuilder r;
    std::string errs;
    Json::Value root;
    std::istringstream ss(jsonStr);
    if (!Json::parseFromStream(r, ss, &root, &errs)) {
        return luaL_error(L, "Invalid JSON: %s", errs.c_str());
    }

    json_to_lua(L, root);
    return 1;
}

int LumeniteApp::lua_jsonify(lua_State *L)
{
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "jsonify(table) expected");
    }

    Json::Value root = lua_to_json(L, 1);
    Json::StreamWriterBuilder w;
    w["indentation"] = "  ";
    std::string jsonStr = Json::writeString(w, root);

    lua_newtable(L); // response

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
    lua_pushlstring(L, jsonStr.c_str(), jsonStr.size());
    lua_settable(L, -3);

    return 1; // return table
}


int LumeniteApp::lua_render_template_string(lua_State *L)
{
    const char *tmpl = luaL_checkstring(L, 1);

    TemplateValue root;
    if (lua_istable(L, 2)) {
        root = TemplateEngine::luaToTemplateValue(L, 2);
    } else {
        root = TemplateValue{TemplateMap{}};
    }

    auto [ok, result] = TemplateEngine::safeRenderFromString(tmpl, root);
    if (!ok) {
        return luaL_error(L, "[TemplateError.Render] %s", result.c_str());
    }

    lua_pushstring(L, result.c_str());
    return 1;
}

int LumeniteApp::lua_render_template_file(lua_State *L)
{
    const char *fn = luaL_checkstring(L, 1);

    TemplateValue root;
    if (lua_istable(L, 2)) {
        root = TemplateEngine::luaToTemplateValue(L, 2);
    } else {
        root = TemplateValue{TemplateMap{}};
    }

    try {
        std::string tmpl = TemplateEngine::loadTemplate(fn);
        auto [ok, result] = TemplateEngine::safeRenderFromString(tmpl, root);
        if (!ok) {
            return luaL_error(L, "%s", result.c_str());
        }

        lua_pushstring(L, result.c_str());
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "[TemplateError.TemplateNotFound] %s", e.what());
    }
}


int LumeniteApp::lua_register_template_filter(lua_State *L)
{
    int nargs = lua_gettop(L);
    const char *name = nullptr;
    int funcIndex = 0;

    if (nargs == 2 && lua_isstring(L, 1) && lua_isfunction(L, 2)) {
        name = lua_tostring(L, 1);
        funcIndex = 2;
    } else if (nargs == 2 && lua_istable(L, 1) && lua_isfunction(L, 2)) {
        return luaL_error(L, "[TemplateError.Usage] Expected app:template_filter(name, function)");
    } else if (nargs == 3 && lua_istable(L, 1) && lua_isstring(L, 2) && lua_isfunction(L, 3)) {
        name = lua_tostring(L, 2);
        funcIndex = 3;
    } else {
        return luaL_error(
            L,
            "[TemplateError.Usage] Usage: app.template_filter(name, function(input)) or app:template_filter(name, function(input))");
    }

    if (!name) {
        return luaL_error(L, "[TemplateError.MissingName] Filter name is missing");
    }

    TemplateEngine::registerLuaFilter(name, L, funcIndex);
    return 0;
}


// ————— Start HTTP Server —————
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
