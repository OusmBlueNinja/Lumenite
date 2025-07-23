#pragma once

#include <string>
#include <sqlite3.h>
#include "lua.hpp"

class LumeniteDB
{
public:
    struct DB
    {
        sqlite3 *handle = nullptr;
        std::string error;

        bool open(const std::string &file);

        bool exec(const std::string &sql);

        std::string lastError() const;

        ~DB();
    };

    static DB **check(lua_State *L);

    static int db_open(lua_State *L);

    static int db_exec(lua_State *L);

    static int db_query(lua_State *L);

    static int db_error(lua_State *L);

    static int db_gc(lua_State *L);

    static int db_sanitize(lua_State *L);
};

extern "C" int luaopen_LumeniteDB(lua_State *L);
