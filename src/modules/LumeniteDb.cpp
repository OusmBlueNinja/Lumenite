#include "LumeniteDB.h"
#include <sqlite3.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <filesystem>

#include "../ErrorHandler.h"

namespace fs = std::filesystem;


// Static variables
std::map<std::string, LumeniteDB::Model> LumeniteDB::models;
LumeniteDB::Session LumeniteDB::session;
LumeniteDB::DB *LumeniteDB::db_instance = nullptr;

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
        if (lua_isinteger(L, -1)) {
            sqlite3_bind_int(stmt, i, (int) lua_tointeger(L, -1));
        } else if (lua_isnumber(L, -1)) {
            sqlite3_bind_double(stmt, i, lua_tonumber(L, -1));
        } else if (lua_isstring(L, -1)) {
            sqlite3_bind_text(stmt, i, lua_tostring(L, -1), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, i);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// === default query methods installation ===
static void register_default_query_methods(lua_State *L, int queryTableIndex, const std::string &tablename)
{
    // query.order_by(expr)
    lua_pushcfunction(L, [](lua_State* L)->int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      const char* expr = luaL_checkstring(L, 2);
                      lua_pushstring(L, expr);
                      lua_setfield(L, 1, "__order_by");
                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "order_by");

    // query.limit(n)
    lua_pushcfunction(L, [](lua_State* L)->int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      int n = luaL_checkinteger(L, 2);
                      lua_pushinteger(L, n);
                      lua_setfield(L, 1, "__limit");
                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "limit");

    // query.filter_by({ k=v, … })
    lua_pushcfunction(L, [](lua_State* L)->int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      luaL_checktype(L, 2, LUA_TTABLE);
                      std::stringstream sql;
                      bool first = true;

                      // build fresh args table
                      lua_newtable(L);
                      int argsIdx = lua_gettop(L);

                      lua_pushnil(L);
                      while (lua_next(L, 2)) {
                      const char* k = lua_tostring(L, -2);
                      if (!first) sql << " AND ";
                      sql << k << " = ?";
                      first = false;

                      if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                      lua_pushvalue(L, -1);
                      lua_rawseti(L, argsIdx, lua_rawlen(L, argsIdx) + 1);
                      } else {
                      return luaL_error(L, "filter_by values must be string or number");
                      }
                      lua_pop(L, 1);
                      }

                      lua_pushstring(L, sql.str().c_str());
                      lua_setfield(L, 1, "__filter_sql");
                      lua_pushvalue(L, argsIdx);
                      lua_setfield(L, 1, "__filter_args");

                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "filter_by");


    // query.get(id) — supports both dot and colon syntax
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        int idArg = 1;
        // if called as User.query:get(id), the first arg is the table
        if (lua_istable(L, 1) && (lua_isinteger(L, 2) || lua_isstring(L, 2))) {
            idArg = 2;
        }
        if (!lua_isinteger(L, idArg) && !lua_isstring(L, idArg)) {
            return luaL_error(L, "Expected integer or string ID");
        }
        std::string sql = std::string("SELECT * FROM ") + tablename + " WHERE id = ? LIMIT 1;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(
                LumeniteDB::db_instance->handle,
                sql.c_str(), -1,
                &stmt, nullptr
            ) != SQLITE_OK) {
            return luaL_error(
                L,
                "SQLite prepare failed: %s",
                sqlite3_errmsg(LumeniteDB::db_instance->handle)
            );
        }
        if (lua_isinteger(L, idArg)) {
            sqlite3_bind_int(stmt, 1, (int) lua_tointeger(L, idArg));
        } else {
            sqlite3_bind_text(
                stmt,
                1,
                lua_tostring(L, idArg),
                -1,
                SQLITE_TRANSIENT
            );
        }
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lua_newtable(L);
            for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
                const char *col = sqlite3_column_name(stmt, i);
                const unsigned char *txt = sqlite3_column_text(stmt, i);
                if (txt) lua_pushstring(L, (const char *) txt);
                else lua_pushnil(L);
                lua_setfield(L, -2, col);
            }
        } else {
            lua_pushnil(L);
        }
        sqlite3_finalize(stmt);
        return 1;
    }, 1);
    lua_setfield(L, queryTableIndex, "get");


    // query.first()
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        const char *t = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = std::string("SELECT * FROM ") + t;
        lua_getfield(L, 1, "__filter_sql");
        if (lua_isstring(L, -1)) sql += " WHERE " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) sql += " ORDER BY " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        sql += " LIMIT 1;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return luaL_error(L, "SQLite error: %s", sqlite3_errmsg(LumeniteDB::db_instance->handle));

        bind_filter_args(L, stmt);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lua_newtable(L);
            for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
                const char *col = sqlite3_column_name(stmt, i);
                const unsigned char *txt = sqlite3_column_text(stmt, i);
                if (txt) lua_pushstring(L, (const char *) txt);
                else lua_pushnil(L);
                lua_setfield(L, -2, col);
            }
        } else {
            lua_pushnil(L);
        }
        sqlite3_finalize(stmt);
        return 1;
    }, 1);
    lua_setfield(L, queryTableIndex, "first");

    // query.all()
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        const char *t = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = std::string("SELECT * FROM ") + t;
        lua_getfield(L, 1, "__filter_sql");
        if (lua_isstring(L, -1)) sql += " WHERE " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) sql += " ORDER BY " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "__limit");
        if (lua_isinteger(L, -1)) sql += " LIMIT " + std::to_string(lua_tointeger(L, -1));
        lua_pop(L, 1);
        sql += ";";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return luaL_error(L, "SQLite error: %s", sqlite3_errmsg(LumeniteDB::db_instance->handle));

        bind_filter_args(L, stmt);
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
    }, 1);
    lua_setfield(L, queryTableIndex, "all");
}

// === model_new ===
static int model_new(lua_State *L)
{
    int defIdx = 1;
    if (!lua_istable(L, 1) && lua_istable(L, 2)) {
        defIdx = 2;
    }
    luaL_checktype(L, defIdx, LUA_TTABLE);

    // create instance and remember its index
    lua_newtable(L);
    int instIdx = lua_gettop(L);

    // copy fields from def‐table
    lua_pushnil(L);
    while (lua_next(L, defIdx) != 0) {
        lua_pushvalue(L, -2); // key
        lua_pushvalue(L, -2); // value
        lua_settable(L, instIdx); // instance[key] = value
        lua_pop(L, 1); // pop value, keep key
    }

    // set its metatable (name from upvalue #1)
    const char *mtName = lua_tostring(L, lua_upvalueindex(1));
    luaL_getmetatable(L, mtName);
    lua_setmetatable(L, -2);

    return 1;
}

// === create_model_table ===
static void create_model_table(lua_State *L, const LumeniteDB::Model &model)
{
    std::string mtName = "LumeniteDB.instance." + model.tablename;
    luaL_newmetatable(L, mtName.c_str());
    lua_pushstring(L, model.tablename.c_str());
    lua_setfield(L, -2, "__model");
    lua_pop(L, 1);

    lua_newtable(L);
    int modelIdx = lua_gettop(L);

    // .new
    lua_pushstring(L, mtName.c_str());
    lua_pushcclosure(L, model_new, 1);
    lua_setfield(L, modelIdx, "new");

    for (const auto &col: model.columns) {
        lua_newtable(L); // ──> [ model, …, helperTbl ]

        lua_pushstring(L, col.name.c_str()); // ──> [ model, …, helperTbl, colName ]

        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s ASC", c);
            return 1;
        }, 1);
        lua_setfield(L, -2, "asc"); // helperTbl.asc = closure; pops closure

        lua_pushstring(L, col.name.c_str()); // push upvalue again
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s DESC", c);
            return 1;
        }, 1);
        lua_setfield(L, -2, "desc"); // helperTbl.desc = closure

        // assign helperTbl into the model table as field <colname>
        lua_setfield(L, modelIdx, col.name.c_str());
        // pops helperTbl, leaving [ model, … ]
    }


    // .query
    lua_newtable(L);
    register_default_query_methods(L, lua_gettop(L), model.tablename);
    lua_setfield(L, modelIdx, "query");
}

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

int LumeniteDB::db_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);

    fs::path dir("db");
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

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
    const char *tn = lua_tostring(L, -1);
    if (!tn) return luaL_error(L, "Missing '__model' in instance");

    session.tablename = tn;
    session.pending_inserts.push_back(row);
    return 0;
}

int LumeniteDB::db_session_commit(lua_State *L)
{
    for (const auto &row: session.pending_inserts) {
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
        run_sql_or_throw(L, "INSERT INTO " + session.tablename + " " + keys.str() + " VALUES " + vals.str() + ";");
    }
    session.pending_inserts.clear();
    return 0;
}

int LumeniteDB::db_select_all(lua_State *L)
{
    const char *tn = luaL_checkstring(L, 1);
    std::string query = std::string("SELECT * FROM ") + tn + ";";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_instance->handle, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return luaL_error(L, "SQLite prepare failed: %s", sqlite3_errmsg(db_instance->handle));

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
    // notice
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
