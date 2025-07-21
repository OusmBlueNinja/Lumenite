//
// Created by spenc on 7/21/2025.
//
#include "LumeniteSafe.h"
#include <lua.hpp>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <regex>

// ---------- HTML Escaping ----------
static std::string html_escape(const std::string &input)
{
    std::ostringstream oss;
    for (char c: input) {
        switch (c) {
            case '&': oss << "&amp;";
                break;
            case '<': oss << "&lt;";
                break;
            case '>': oss << "&gt;";
                break;
            case '"': oss << "&quot;";
                break;
            case '\'': oss << "&#x27;";
                break;
            case '/': oss << "&#x2F;";
                break;
            default: oss << c;
                break;
        }
    }
    return oss.str();
}

static int l_html_escape(lua_State *L)
{
    lua_pushstring(L, html_escape(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- URL Encode / Decode ----------
static std::string url_encode(const std::string &input)
{
    std::ostringstream oss;
    for (unsigned char c: input) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::uppercase << std::setw(2)
                    << std::setfill('0') << std::hex << (int) c;
        }
    }
    return oss.str();
}

static int l_url_encode(lua_State *L)
{
    lua_pushstring(L, url_encode(luaL_checkstring(L, 1)).c_str());
    return 1;
}

static std::string url_decode(const std::string &input)
{
    std::ostringstream oss;
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%' && i + 2 < input.length()) {
            int hex;
            std::istringstream(input.substr(i + 1, 2)) >> std::hex >> hex;
            oss << static_cast<char>(hex);
            i += 2;
        } else if (input[i] == '+') {
            oss << ' ';
        } else {
            oss << input[i];
        }
    }
    return oss.str();
}

static int l_url_decode(lua_State *L)
{
    lua_pushstring(L, url_decode(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- Strip Tags ----------
static std::string strip_tags(const std::string &input)
{
    return std::regex_replace(input, std::regex("<[^>]*>"), "");
}

static int l_strip_tags(lua_State *L)
{
    lua_pushstring(L, strip_tags(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- JavaScript Escape ----------
static std::string js_escape(const std::string &input)
{
    std::ostringstream oss;
    for (char c: input) {
        switch (c) {
            case '\'': oss << "\\\'";
                break;
            case '"': oss << "\\\"";
                break;
            case '\\': oss << "\\\\";
                break;
            case '\n': oss << "\\n";
                break;
            case '\r': oss << "\\r";
                break;
            case '\t': oss << "\\t";
                break;
            default:
                if (isprint((unsigned char) c))
                    oss << c;
                else
                    oss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << ((unsigned char) c);
                break;
        }
    }
    return oss.str();
}

static int l_js_escape(lua_State *L)
{
    lua_pushstring(L, js_escape(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- Attribute Escape ----------
static std::string attr_escape(const std::string &input)
{
    std::ostringstream oss;
    for (char c: input) {
        switch (c) {
            case '&': oss << "&amp;";
                break;
            case '"': oss << "&quot;";
                break;
            case '\'': oss << "&#x27;";
                break;
            case '<': oss << "&lt;";
                break;
            case '>': oss << "&gt;";
                break;
            default: oss << c;
                break;
        }
    }
    return oss.str();
}

static int l_attr_escape(lua_State *L)
{
    lua_pushstring(L, attr_escape(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- Quote Escape ----------
static std::string quote_escape(const std::string &input)
{
    std::ostringstream oss;
    for (char c: input) {
        if (c == '"' || c == '\'') oss << '\\' << c;
        else oss << c;
    }
    return oss.str();
}

static int l_quote_escape(lua_State *L)
{
    lua_pushstring(L, quote_escape(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- CSV Escape ----------
static std::string csv_escape(const std::string &input)
{
    bool must_quote = input.find_first_of(",\"\n\r") != std::string::npos;
    std::string escaped = input;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, "\"");
        pos += 2;
    }
    if (must_quote)
        return "\"" + escaped + "\"";
    return escaped;
}

static int l_csv_escape(lua_State *L)
{
    lua_pushstring(L, csv_escape(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- ID-safe (a-zA-Z0-9 and _) ----------
static std::string id_safe(const std::string &input)
{
    std::ostringstream oss;
    for (char c: input) {
        if (isalnum((unsigned char) c) || c == '_') {
            oss << c;
        }
    }
    return oss.str();
}

static int l_id_safe(lua_State *L)
{
    lua_pushstring(L, id_safe(luaL_checkstring(L, 1)).c_str());
    return 1;
}

// ---------- Whitelist Filter ----------
static int l_whitelist(lua_State *L)
{
    const char *input = luaL_checkstring(L, 1);
    const char *pattern = luaL_optstring(L, 2, "a-zA-Z0-9_");
    std::regex rx(std::string("[^") + pattern + "]");
    std::string result = std::regex_replace(input, rx, "");
    lua_pushstring(L, result.c_str());
    return 1;
}


int LumeniteSafe::luaopen(lua_State *L)
{
    lua_newtable(L);

    lua_pushcfunction(L, l_html_escape);
    lua_setfield(L, -2, "escape");

    lua_pushcfunction(L, l_url_encode);
    lua_setfield(L, -2, "urlencode");

    lua_pushcfunction(L, l_url_decode);
    lua_setfield(L, -2, "urldecode");

    lua_pushcfunction(L, l_strip_tags);
    lua_setfield(L, -2, "strip_tags");

    lua_pushcfunction(L, l_js_escape);
    lua_setfield(L, -2, "js_escape");

    lua_pushcfunction(L, l_attr_escape);
    lua_setfield(L, -2, "attr_escape");

    lua_pushcfunction(L, l_quote_escape);
    lua_setfield(L, -2, "quote_safe");

    lua_pushcfunction(L, l_csv_escape);
    lua_setfield(L, -2, "csv_escape");

    lua_pushcfunction(L, l_id_safe);
    lua_setfield(L, -2, "id_safe");

    lua_pushcfunction(L, l_whitelist);
    lua_setfield(L, -2, "whitelist");

    return 1;
}
