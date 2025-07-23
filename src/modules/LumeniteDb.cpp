#include "LumeniteDB.h"
#include <sstream>
#include <iostream>

// Static variables
std::map<std::string, LumeniteDB::Model> LumeniteDB::models;
LumeniteDB::Session LumeniteDB::session;
LumeniteDB::DB *LumeniteDB::db_instance = nullptr;

// DB methods
bool LumeniteDB::DB::open(const std::string &file)
{
    return sqlite3_open(file.c_str(), &handle) == SQLITE_OK;
}

bool LumeniteDB::DB::exec(const std::string &sql)
{
    char *errMsg = nullptr;
    int rc = sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
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

// Lua bindings

LumeniteDB::DB **LumeniteDB::check(lua_State *L)
{
    return static_cast<LumeniteDB::DB **>(luaL_checkudata(L, 1, "LumeniteDB.DB"));
}

int LumeniteDB::db_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    auto **ud = static_cast<LumeniteDB::DB **>(lua_newuserdata(L, sizeof(LumeniteDB::DB*)));
    *ud = new DB();

    luaL_getmetatable(L, "LumeniteDB.DB");
    lua_setmetatable(L, -2);

    if (!(*ud)->open(filename)) {
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
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__tablename");
    const char *tablename = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    Model model;
    model.tablename = tablename;

    // Parse columns from the model definition table
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_istable(L, -1)) {
            Column col;
            lua_getfield(L, -1, "name");
            col.name = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "type");
            col.type = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "primary_key");
            col.primary_key = lua_toboolean(L, -1);
            lua_pop(L, 1);
            model.columns.push_back(col);
        }
        lua_pop(L, 1);
    }

    models[tablename] = model;

    // Create metatable for instances
    lua_newtable(L); // metatable
    lua_pushstring(L, tablename);
    lua_setfield(L, -2, "__model");
    lua_pushvalue(L, -1); // copy for upvalue
    int instance_meta_upvalue = lua_gettop(L);

    // Create table to represent the model (returned to Lua)
    lua_newtable(L); // model table

    // Define model.new = function(tbl)
    lua_pushcclosure(L, [](lua_State *L) -> int {
        luaL_checktype(L, 1, LUA_TTABLE); // args

        lua_newtable(L); // instance

        // Copy keys from input table into instance
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            const char *key = lua_tostring(L, -2);
            if (key) {
                lua_pushvalue(L, -2); // key
                lua_insert(L, -2);    // value
                lua_settable(L, -4);
            }
            lua_pop(L, 1);
        }

        // Attach metatable
        lua_pushvalue(L, lua_upvalueindex(1)); // instance metatable
        lua_setmetatable(L, -2);

        return 1;
    }, 1); // upvalue = instance metatable
    lua_setfield(L, -2, "new");

    return 1; // return model table (with .new)
}



int LumeniteDB::db_create_all(lua_State *L)
{
    for (const auto &[tablename, model]: models) {
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << tablename << " (";
        for (size_t i = 0; i < model.columns.size(); ++i) {
            const auto &col = model.columns[i];
            ss << col.name << " " << col.type;
            if (col.primary_key) ss << " PRIMARY KEY";
            if (i != model.columns.size() - 1) ss << ", ";
        }
        ss << ");";
        db_instance->exec(ss.str());
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
    const char *tablename = lua_tostring(L, -1);
    if (!tablename) return luaL_error(L, "Missing '__model' in instance");

    session.tablename = tablename;
    session.pending_inserts.push_back(row);
    return 0;
}

int LumeniteDB::db_session_commit(lua_State *L)
{
    for (const auto &row: session.pending_inserts) {
        std::stringstream keys, values;
        keys << "(";
        values << "(";
        size_t count = 0;
        for (const auto &[k, v]: row.values) {
            if (count++) {
                keys << ", ";
                values << ", ";
            }
            keys << k;
            values << "'" << v << "'";
        }
        keys << ")";
        values << ")";
        std::string sql = "INSERT INTO " + session.tablename +
                          " " + keys.str() + " VALUES " + values.str() + ";";
        db_instance->exec(sql);
    }
    session.pending_inserts.clear();
    return 0;
}

int LumeniteDB::db_select_all(lua_State *L)
{
    const char *tablename = luaL_checkstring(L, 1);
    std::string query = "SELECT * FROM " + std::string(tablename) + ";";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_instance->handle, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return luaL_error(L, "SQLite prepare failed: %s", sqlite3_errmsg(db_instance->handle));
    }

    lua_newtable(L);
    int index = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        lua_newtable(L);
        int col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char *name = sqlite3_column_name(stmt, i);
            const unsigned char *text = sqlite3_column_text(stmt, i);
            if (text) lua_pushstring(L, reinterpret_cast<const char *>(text));
            else lua_pushnil(L);
            lua_setfield(L, -2, name);
        }
        lua_rawseti(L, -2, index++);
    }

    sqlite3_finalize(stmt);
    return 1;
}

extern "C" int luaopen_lumenite_db(lua_State *L)
{
    luaL_newmetatable(L, "LumeniteDB.DB");
    lua_pushcfunction(L, LumeniteDB::db_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

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
