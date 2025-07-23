#pragma once

#include <string>
#include <vector>
#include <map>
#include <sqlite3.h>
#include "lua.hpp"

class LumeniteDB
{
public:
    struct Column
    {
        std::string name;
        std::string type;
        bool primary_key = false;
    };

    struct Model
    {
        std::string tablename;
        std::vector<Column> columns;
    };

    struct Row
    {
        std::map<std::string, std::string> values;
    };

    struct Session
    {
        std::string tablename;
        std::vector<Row> pending_inserts;
    };

    struct DB
    {
        sqlite3 *handle = nullptr;
        std::string error;

        bool open(const std::string &file);

        bool exec(const std::string &sql);

        std::string lastError() const;

        ~DB();
    };

    // Lua bindings
    static int db_open(lua_State *L);

    static int db_column(lua_State *L);

    static int db_model(lua_State *L);

    static int db_create_all(lua_State *L);

    static int db_session_add(lua_State *L);

    static int db_session_commit(lua_State *L);

    static int db_select_all(lua_State *L);

    static int db_gc(lua_State *L);

    static DB **check(lua_State *L);

    // Global state
    static std::map<std::string, Model> models;
    static Session session;
    static DB *db_instance;
};

extern "C" int luaopen_lumenite_db(lua_State *L);
