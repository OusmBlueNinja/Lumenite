#include "LumeniteDB.h"
#include <sstream>
#include <iostream>
#include <cstring>

// Static variables
std::map<std::string, LumeniteDB::Model> LumeniteDB::models;
LumeniteDB::Session LumeniteDB::session;
LumeniteDB::DB *LumeniteDB::db_instance = nullptr;

static bool run_sql_or_throw(lua_State *L, const std::string &sql)
{
    if (!LumeniteDB::db_instance) {
        luaL_error(L, "No database connection. Did you call db.open(path)?");
        return false;
    }

    if (!LumeniteDB::db_instance->exec(sql)) {
        luaL_error(L, "SQL error: %s", LumeniteDB::db_instance->lastError().c_str());
        return false;
    }

    return true;
}


static void register_default_query_methods(lua_State *L, const std::string &tablename);

static void bind_filter_args(lua_State *L, sqlite3_stmt *stmt)
{
    lua_getfield(L, 1, "__filter_args");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    int n = lua_rawlen(L, -1);
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

    lua_pop(L, 1); // pop __filter_args
}


static void register_default_query_methods(lua_State *L,
                                           int queryTableIndex,
                                           const std::string &tablename)
{
    // query.order_by(expr)
    lua_pushcfunction(L, [](lua_State *L) -> int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      const char *expr = luaL_checkstring(L, 2);
                      lua_pushstring(L, expr);
                      lua_setfield(L, 1, "__order_by");
                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "order_by");

    // query.limit(n)
    lua_pushcfunction(L, [](lua_State *L) -> int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      int n = luaL_checkinteger(L, 2);
                      lua_pushinteger(L, n);
                      lua_setfield(L, 1, "__limit");
                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "limit");

    // query.filter_by({ k = v, … })
    lua_pushcfunction(L, [](lua_State *L) -> int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      luaL_checktype(L, 2, LUA_TTABLE);

                      std::stringstream sql;
                      bool first = true;

                      // build a fresh args list
                      lua_newtable(L);
                      int argsTbl = lua_gettop(L);

                      lua_pushnil(L);
                      while (lua_next(L, 2)) {
                      const char *k = lua_tostring(L, -2);
                      if (!first) sql << " AND ";
                      sql << k << " = ?";
                      first = false;

                      if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                      lua_pushvalue(L, -1);
                      lua_rawseti(L, argsTbl, lua_rawlen(L, argsTbl) + 1);
                      } else {
                      return luaL_error(L, "filter_by values must be string or number");
                      }
                      lua_pop(L, 1);
                      }

                      lua_pushstring(L, sql.str().c_str());
                      lua_setfield(L, 1, "__filter_sql");
                      lua_pushvalue(L, argsTbl);
                      lua_setfield(L, 1, "__filter_args");

                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, queryTableIndex, "filter_by");

    // query.get(id)
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        if (!lua_isinteger(L, 1) && !lua_isstring(L, 1))
            return luaL_error(L, "Expected integer or string ID");
        std::string sql = std::string("SELECT * FROM ") + tablename + " WHERE id = ? LIMIT 1;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle,
                               sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return luaL_error(L, "SQLite prepare failed: %s",
                              sqlite3_errmsg(LumeniteDB::db_instance->handle));

        if (lua_isinteger(L, 1))
            sqlite3_bind_int(stmt, 1, int(lua_tointeger(L, 1)));
        else
            sqlite3_bind_text(stmt, 1, lua_tostring(L, 1), -1, SQLITE_TRANSIENT);

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
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = std::string("SELECT * FROM ") + tablename;
        lua_getfield(L, 1, "__filter_sql");
        if (lua_isstring(L, -1)) sql += " WHERE " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) sql += " ORDER BY " + std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
        sql += " LIMIT 1;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle,
                               sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return luaL_error(L, "SQLite error: %s",
                              sqlite3_errmsg(LumeniteDB::db_instance->handle));

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
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = std::string("SELECT * FROM ") + tablename;
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

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle,
                               sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return luaL_error(L, "SQLite error: %s",
                              sqlite3_errmsg(LumeniteDB::db_instance->handle));

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


// helper to implement User.new / User:new
static int model_new(lua_State *L)
{
    // figure out if def‑table is at 1 or 2
    int defIdx = 1;
    if (!lua_istable(L, 1) && lua_istable(L, 2)) {
        defIdx = 2;
    }
    luaL_checktype(L, defIdx, LUA_TTABLE);

    // copy def‑table into a fresh instance
    lua_newtable(L); // ↑ [ ..., instance ]
    lua_pushnil(L);
    while (lua_next(L, defIdx) != 0) {
        // stack: key at -2, value at -1
        lua_pushvalue(L, -2); // copy key
        lua_pushvalue(L, -2); // copy value
        lua_settable(L, -4); // instance[key] = value
        lua_pop(L, 1); // pop the value, keep key
    }

    // attach the metatable (name is upvalue #1)
    const char *mtName = lua_tostring(L, lua_upvalueindex(1));
    luaL_getmetatable(L, mtName);
    lua_setmetatable(L, -2);

    return 1; // return the new instance
}

static void create_model_table(lua_State *L, const LumeniteDB::Model &model)
{
    // 1) Build a unique registry metatable for instances
    std::string mtName = "LumeniteDB.instance." + model.tablename;
    luaL_newmetatable(L, mtName.c_str()); // ↑ [ mt ]
    lua_pushstring(L, model.tablename.c_str());
    lua_setfield(L, -2, "__model"); // pops string
    lua_pop(L, 1); // pops mt

    // 2) Create the model table itself
    lua_newtable(L); // ↑ [ model ]
    int modelIdx = lua_gettop(L);

    // 3) Install User.new  (as both dot- and colon-style)
    lua_pushstring(L, mtName.c_str()); // upvalue = registry metatable name
    lua_pushcclosure(L, model_new, 1); // pops upvalue, pushes closure
    lua_setfield(L, modelIdx, "new"); // pops closure

    // 4) Install each column helper as User.<colname>
    for (const auto &col: model.columns) {
        // a) desc()
        lua_pushstring(L, col.name.c_str()); // upvalue = "colname"
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s DESC", c);
            return 1;
        }, 1);
        lua_setfield(L, modelIdx, (col.name + "_desc").c_str());

        // b) asc()
        lua_pushstring(L, col.name.c_str());
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *c = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s ASC", c);
            return 1;
        }, 1);
        lua_setfield(L, modelIdx, (col.name + "_asc").c_str());
    }

    // 5) Install the .query sub‑table
    lua_newtable(L); // ↑ [ model, queryTbl ]
    register_default_query_methods(L, lua_gettop(L), model.tablename);
    lua_setfield(L, modelIdx, "query"); // pops queryTbl

    // leave [ model ] on the stack for luaopen to return
}


// DB methods
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

// Lua bindings

LumeniteDB::DB **LumeniteDB::check(lua_State *L)
{
    return static_cast<DB **>(luaL_checkudata(L, 1, "LumeniteDB.DB"));
}

int LumeniteDB::db_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    auto **ud = static_cast<DB **>(lua_newuserdata(L, sizeof(LumeniteDB::DB*)));
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
    // Figure out where your `def`‑table really is:
    // • If called as db:Model{…} then stack[1]=db and stack[2]=defTable.
    // • If called as db.Model{…} then stack[1]=defTable.
    int defIdx = 1;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        defIdx = 2;
    }

    // Now defIdx must be the table holding your fields
    if (!lua_istable(L, defIdx)) {
        return luaL_error(L,
                          "db.Model: expected a definition table, got %s",
                          lua_typename(L, lua_type(L, defIdx))
        );
    }

    // Pull out and validate __tablename
    lua_getfield(L, defIdx, "__tablename");
    if (!lua_isstring(L, -1)) {
        return luaL_error(L,
                          "db.Model: missing or invalid '__tablename' field"
        );
    }
    const char *tablename = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Build your C++ Model struct
    Model model;
    model.tablename = tablename;

    // Iterate every key/value in the def‑table
    lua_pushnil(L);
    while (lua_next(L, defIdx) != 0) {
        // key at -2, value at -1
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char *key = lua_tostring(L, -2);
            if (strcmp(key, "__tablename") != 0 && lua_istable(L, -1)) {
                Column col;
                // name
                lua_getfield(L, -1, "name");
                col.name = luaL_checkstring(L, -1);
                lua_pop(L, 1);
                // type
                lua_getfield(L, -1, "type");
                col.type = luaL_checkstring(L, -1);
                lua_pop(L, 1);
                // primary_key
                lua_getfield(L, -1, "primary_key");
                col.primary_key = lua_toboolean(L, -1);
                lua_pop(L, 1);

                model.columns.push_back(col);
            }
        }
        lua_pop(L, 1); // pop the value, keep the key for next()
    }

    // Store and hand off to your factory
    models[model.tablename] = model;
    create_model_table(L, model);

    // Stack now has exactly 1 return: the new model table
    return 1;
}


int LumeniteDB::db_create_all(lua_State *L)
{
    std::vector<std::pair<std::string, Model> > reversed;
    for (const auto &entry: models) {
        reversed.push_back(entry);
    }

    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        const auto &[tablename, model] = *it;

        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << tablename << " (";
        for (size_t i = 0; i < model.columns.size(); ++i) {
            const auto &col = model.columns[i];
            ss << col.name << " " << col.type;
            if (col.primary_key) ss << " PRIMARY KEY";
            if (i != model.columns.size() - 1) ss << ", ";
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

        run_sql_or_throw(L, sql);
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
