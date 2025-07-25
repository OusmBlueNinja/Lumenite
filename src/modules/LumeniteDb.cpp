// lumenite_db.cpp
#include "LumeniteDB.h"
#include <sqlite3.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <filesystem>

#include "../ErrorHandler.h"

namespace fs = std::filesystem;

// -- Static state ------------------------------------------------------------

std::map<std::string, LumeniteDB::Model> LumeniteDB::models;
LumeniteDB::Session LumeniteDB::session;
LumeniteDB::DB *LumeniteDB::db_instance = nullptr;

// -- Internal helpers --------------------------------------------------------

// Throws a Lua error if exec() fails
static void run_sql_or_throw(lua_State *L, const std::string &sql)
{
    if (!LumeniteDB::db_instance) {
        luaL_error(L, "No database connection. Did you call db.open(path)?");
    }
    if (!LumeniteDB::db_instance->exec(sql)) {
        luaL_error(L, "SQL error: %s", LumeniteDB::db_instance->lastError().c_str());
    }
}

// Binds the array in __filter_args to an sqlite3_stmt
static void bind_filter_args(lua_State *L, sqlite3_stmt *stmt)
{
    lua_getfield(L, 1, "__filter_args");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    int n = (int) lua_rawlen(L, -1);
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        if (lua_isinteger(L, -1)) sqlite3_bind_int(stmt, i, (int) lua_tointeger(L, -1));
        else if (lua_isnumber(L, -1)) sqlite3_bind_double(stmt, i, lua_tonumber(L, -1));
        else if (lua_isstring(L, -1)) sqlite3_bind_text(stmt, i, lua_tostring(L, -1), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, i);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// Catches assignments to instance fields, records them for UPDATE on commit
static int instance_newindex(lua_State *L)
{
    // stack: 1=instance, 2=key, 3=new value
    const char *key = luaL_checkstring(L, 2);

    // 1) Set the field on the instance
    lua_settop(L, 3);
    lua_rawset(L, 1);

    // 2) Lookup the model name
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__model");
    const char *tablename = lua_tostring(L, -1);
    lua_pop(L, 2);

    // 3) Lookup the primary key value
    lua_getfield(L, 1, "id");
    const char *id = lua_tostring(L, -1);
    lua_pop(L, 1);

    // 4) Record update
    LumeniteDB::Update upd;
    upd.tablename = tablename;
    upd.changes["id"] = id;
    // new value:
    lua_pushvalue(L, 3);
    const char *val = lua_tostring(L, -1);
    lua_pop(L, 1);
    upd.changes[key] = val ? val : "";

    LumeniteDB::session.pending_updates.push_back(std::move(upd));
    return 0;
}

// === default query methods installation ===
static void register_default_query_methods(lua_State *L, int queryTableIndex, const std::string &tablename)
{
    // order_by, limit, filter_by, get, first, all ...
    // (same as before, omitted here for brevity)
    // … your existing register_default_query_methods code …
}

// === model_new ===
static int model_new(lua_State *L)
{
    int defIdx = 1;
    if (!lua_istable(L, 1) && lua_istable(L, 2)) defIdx = 2;
    luaL_checktype(L, defIdx, LUA_TTABLE);

    lua_newtable(L);
    int instIdx = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, defIdx)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_settable(L, instIdx);
        lua_pop(L, 1);
    }

    const char *mtName = lua_tostring(L, lua_upvalueindex(1));
    luaL_getmetatable(L, mtName);
    lua_setmetatable(L, -2);

    return 1;
}

// === create_model_table ===
static void create_model_table(lua_State *L, const LumeniteDB::Model &model)
{
    // metatable
    std::string mtName = "LumeniteDB.instance." + model.tablename;
    luaL_newmetatable(L, mtName.c_str());
    lua_pushstring(L, model.tablename.c_str());
    lua_setfield(L, -2, "__model");
    // install __newindex to track field changes
    lua_pushcfunction(L, instance_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    // model table
    lua_newtable(L);
    int modelIdx = lua_gettop(L);

    // .new constructor
    lua_pushstring(L, mtName.c_str());
    lua_pushcclosure(L, model_new, 1);
    lua_setfield(L, modelIdx, "new");

    // column helper tables: <col> with :asc()/:desc()
    for (const auto &col: model.columns) {
        lua_newtable(L);

        lua_pushstring(L, col.name.c_str());
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s ASC", c);
            return 1;
        }, 1);
        lua_setfield(L, -2, "asc");

        lua_pushstring(L, col.name.c_str());
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s DESC", c);
            return 1;
        }, 1);
        lua_setfield(L, -2, "desc");

        lua_setfield(L, modelIdx, col.name.c_str());
    }

    // .query
    lua_newtable(L);
    register_default_query_methods(L, lua_gettop(L), model.tablename);
    lua_setfield(L, modelIdx, "query");
}

// -- LumeniteDB::DB implementation -------------------------------------------

bool LumeniteDB::DB::open(const std::string &file)
{
    return sqlite3_open(file.c_str(), &handle) == SQLITE_OK;
}

bool LumeniteDB::DB::exec(const std::string &sql)
{
    char *errMsg = nullptr;
    if (sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::string LumeniteDB::DB::lastError() const
{
    return error;
}

LumeniteDB::DB::~DB()
{
    if (handle) sqlite3_close(handle);
}

LumeniteDB::DB **LumeniteDB::check(lua_State *L)
{
    return static_cast<DB **>(luaL_checkudata(L, 1, "LumeniteDB.DB"));
}

// -- Lua bindings -----------------------------------------------------------

int LumeniteDB::db_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);

    // ensure ./db exists
    fs::path dir("db");
    if (!fs::exists(dir)) fs::create_directories(dir);

    fs::path full = dir / filename;
    std::string fullPath = full.string();

    auto **ud = static_cast<DB **>(lua_newuserdata(L, sizeof(DB*)));
    *ud = new DB();
    luaL_getmetatable(L, "LumeniteDB.DB");
    lua_setmetatable(L, -2);

    if (!(*ud)->open(fullPath)) {
        lua_pushnil(L);
        lua_pushstring(L, (*ud)->lastError().c_str());
        return 2;
    }

    db_instance = *ud;
    return 1;
}

int LumeniteDB::db_gc(lua_State *L)
{
    auto **ud = check(L);
    delete *ud;
    return 0;
}

int LumeniteDB::db_column(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *type = luaL_checkstring(L, 2);
    bool primary = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "primary_key");
        primary = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    lua_newtable(L);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, type);
    lua_setfield(L, -2, "type");
    lua_pushboolean(L, primary);
    lua_setfield(L, -2, "primary_key");
    return 1;
}

int LumeniteDB::db_model(lua_State *L)
{
    int defIdx = 1;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) defIdx = 2;
    luaL_checktype(L, defIdx, LUA_TTABLE);

    lua_getfield(L, defIdx, "__tablename");
    if (!lua_isstring(L, -1)) {
        return luaL_error(L, "db.Model: missing or invalid '__tablename' field");
    }
    std::string tablename = lua_tostring(L, -1);
    lua_pop(L, 1);

    Model model;
    model.tablename = tablename;

    lua_pushnil(L);
    while (lua_next(L, defIdx)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            Column col;
            lua_getfield(L, -1, "name");
            col.name = luaL_checkstring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "type");
            col.type = luaL_checkstring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "primary_key");
            col.primary_key = lua_toboolean(L, -1);
            lua_pop(L, 1);
            model.columns.push_back(col);
        }
        lua_pop(L, 1);
    }

    models[tablename] = model;
    create_model_table(L, model);
    return 1;
}

int LumeniteDB::db_create_all(lua_State *L)
{
    for (auto it = models.rbegin(); it != models.rend(); ++it) {
        const auto &[tn, mdl] = *it;
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << tn << " (";
        for (size_t i = 0; i < mdl.columns.size(); ++i) {
            const auto &c = mdl.columns[i];
            ss << c.name << " " << c.type << (c.primary_key ? " PRIMARY KEY" : "");
            if (i + 1 < mdl.columns.size()) ss << ", ";
        }
        ss << ");";
        run_sql_or_throw(L, ss.str());
    }
    return 0;
}

int LumeniteDB::db_session_add(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    Row row;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        const char *key = lua_tostring(L, -2);
        const char *val = lua_tostring(L, -1);
        if (key) row.values[key] = val ? val : "";
        lua_pop(L, 1);
    }

    if (!lua_getmetatable(L, 1)) {
        return luaL_error(L, "Missing model metatable");
    }
    lua_getfield(L, -1, "__model");
    session.tablename = lua_tostring(L, -1);
    lua_pop(L, 1);

    session.pending_inserts.push_back(std::move(row));
    return 0;
}

int LumeniteDB::db_session_commit(lua_State *L)
{
    // INSERTs
    for (auto &row: session.pending_inserts) {
        std::stringstream keys, vals;
        keys << "(";
        vals << "(";
        bool first = true;
        for (auto &[k,v]: row.values) {
            if (!first) {
                keys << ", ";
                vals << ", ";
            }
            first = false;
            keys << k;
            vals << "'" << v << "'";
        }
        keys << ")";
        vals << ")";
        run_sql_or_throw(L,
                         "INSERT INTO " + session.tablename +
                         " " + keys.str() + " VALUES " + vals.str() + ";"
        );
    }
    session.pending_inserts.clear();

    // UPDATEs
    for (auto &upd: session.pending_updates) {
        std::stringstream ss;
        ss << "UPDATE " << upd.tablename << " SET ";
        bool first = true;
        for (auto &[col,val]: upd.changes) {
            if (col == "id") continue;
            if (!first) ss << ", ";
            first = false;
            ss << col << "='" << val << "'";
        }
        ss << " WHERE id='" << upd.changes["id"] << "';";
        run_sql_or_throw(L, ss.str());
    }
    session.pending_updates.clear();

    return 0;
}

int LumeniteDB::db_select_all(lua_State *L)
{
    const char *tn = luaL_checkstring(L, 1);
    std::string query = std::string("SELECT * FROM ") + tn + ";";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_instance->handle, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return luaL_error(L, "SQLite prepare failed: %s",
                          sqlite3_errmsg(db_instance->handle));
    }

    lua_newtable(L);
    int idx = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        lua_newtable(L);
        for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
            const char *col = sqlite3_column_name(stmt, i);
            const unsigned char *txt = sqlite3_column_text(stmt, i);
            if (txt) lua_pushstring(L, (const char *) txt);
            else lua_pushnil(L);
            lua_setfield(L, -2, col);
        }
        lua_rawseti(L, -2, idx++);
    }
    sqlite3_finalize(stmt);
    return 1;
}

extern "C" int luaopen_lumenite_db(lua_State *L)
{
    std::cout << YELLOW << "[~] Notice  : " << RESET
            << "The module " << BOLD << "'lumenite.db'" << RESET
            << " is currently in " << BOLD RED << "Alpha" << RESET << ".\n"
            << "             Use with caution - it may be incomplete or insecure.\n";

    // DB userdata metatable
    luaL_newmetatable(L, "LumeniteDB.DB");
    lua_pushcfunction(L, LumeniteDB::db_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    // module table
    lua_newtable(L);
    lua_pushcfunction(L, LumeniteDB::db_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, LumeniteDB::db_column);
    lua_setfield(L, -2, "Column");
    lua_pushcfunction(L, LumeniteDB::db_model);
    lua_setfield(L, -2, "Model");
    lua_pushcfunction(L, LumeniteDB::db_create_all);
    lua_setfield(L, -2, "create_all");
    lua_pushcfunction(L, LumeniteDB::db_session_add);
    lua_setfield(L, -2, "session_add");
    lua_pushcfunction(L, LumeniteDB::db_session_commit);
    lua_setfield(L, -2, "session_commit");
    lua_pushcfunction(L, LumeniteDB::db_select_all);
    lua_setfield(L, -2, "select_all");

    return 1;
}
