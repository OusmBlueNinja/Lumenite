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


static void create_model_table(lua_State *L, const LumeniteDB::Model &model)
{
    lua_newtable(L); // metatable
    lua_pushstring(L, model.tablename.c_str());
    lua_setfield(L, -2, "__model");
    lua_pushvalue(L, -1); // __index = metatable
    lua_setfield(L, -2, "__index");
    int metatableRef = lua_gettop(L);

    lua_newtable(L);
    int modelTable = lua_gettop(L);

    lua_pushvalue(L, metatableRef); // upvalue = instance metatable
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_newtable(L);
        int instanceIndex = lua_gettop(L);

        lua_pushnil(L);
        while (lua_next(L, 1)) {
            lua_pushvalue(L, -2); // key
            lua_pushvalue(L, -2); // value
            lua_settable(L, instanceIndex);
            lua_pop(L, 1);
        }

        lua_pushvalue(L, lua_upvalueindex(1));
        lua_setmetatable(L, -2);
        return 1;
    }, 1);
    lua_setfield(L, modelTable, "new");

    for (const auto &col: model.columns) {
        lua_newtable(L); // column table

        lua_pushstring(L, col.name.c_str());
        lua_pushcclosure(L, [](lua_State *L) -> int
        {
            const char *colname = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s DESC", colname);
            return 1;
        }, 1);
        lua_setfield(L, -2, "desc");

        lua_pushstring(L, col.name.c_str());
        lua_pushcclosure(L, [](lua_State *L) -> int
        {
            const char *colname = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s ASC", colname);
            return 1;
        }, 1);
        lua_setfield(L, -2, "asc");

        lua_setfield(L, modelTable, col.name.c_str());
    }

    lua_newtable(L);
    register_default_query_methods(L, model.tablename);
    lua_setfield(L, modelTable, "query");
}


static void register_default_query_methods(lua_State *L, const std::string &tablename)
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
    lua_setfield(L, -2, "order_by");

    // query.limit(n)
    lua_pushcfunction(L, [](lua_State *L) -> int {
                      luaL_checktype(L, 1, LUA_TTABLE);
                      int n = luaL_checkinteger(L, 2);
                      lua_pushinteger(L, n);
                      lua_setfield(L, 1, "__limit");
                      lua_pushvalue(L, 1);
                      return 1;
                      });
    lua_setfield(L, -2, "limit");

    // query.filter_by({ name = "Alice", id = 5 })
    lua_pushcfunction(L, [](lua_State *L) -> int {
                      luaL_checktype(L, 1, LUA_TTABLE); // query object
                      luaL_checktype(L, 2, LUA_TTABLE); // key-value filter

                      std::stringstream sql;
                      bool first = true;

                      // Clear previous args
                      lua_newtable(L); // arg list
                      int argTable = lua_gettop(L);

                      lua_pushnil(L);
                      while (lua_next(L, 2)) {
                      const char *key = lua_tostring(L, -2);
                      if (!key) continue;

                      if (!first) sql << " AND ";
                      sql << key << " = ?";
                      first = false;

                      // Add value to arg list (as string)
                      if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                      lua_pushvalue(L, -1);
                      lua_rawseti(L, argTable, lua_rawlen(L, argTable) + 1);
                      } else {
                      return luaL_error(L, "filter_by values must be string or number");
                      }

                      lua_pop(L, 1);
                      }

                      lua_pushstring(L, sql.str().c_str());
                      lua_setfield(L, 1, "__filter_sql");

                      lua_pushvalue(L, argTable);
                      lua_setfield(L, 1, "__filter_args");

                      lua_pushvalue(L, 1); // return query object
                      return 1;
                      });
    lua_setfield(L, -2, "filter_by");


    // query.where = alias for filter
    lua_getfield(L, -1, "filter");
    lua_setfield(L, -2, "where");

    // query.get(id)
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = "SELECT * FROM " + std::string(tablename) + " WHERE id = ? LIMIT 1;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return luaL_error(L, "SQLite prepare failed: %s", sqlite3_errmsg(LumeniteDB::db_instance->handle));
        }

        if (lua_isinteger(L, 2)) {
            sqlite3_bind_int(stmt, 1, static_cast<int>(lua_tointeger(L, 2)));
        } else if (lua_isstring(L, 2)) {
            sqlite3_bind_text(stmt, 1, lua_tostring(L, 2), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_finalize(stmt);
            return luaL_error(L, "Expected string or integer ID for get()");
        }

        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            lua_newtable(L);
            int col_count = sqlite3_column_count(stmt);
            for (int i = 0; i < col_count; ++i) {
                const char *col = sqlite3_column_name(stmt, i);
                const char *val = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
                if (val) lua_pushstring(L, val);
                else lua_pushnil(L);
                lua_setfield(L, -2, col);
            }
        } else {
            lua_pushnil(L);
        }

        sqlite3_finalize(stmt);
        return 1;
    }, 1);
    lua_setfield(L, -2, "get");

    // query.first()
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = "SELECT * FROM " + std::string(tablename);

        lua_getfield(L, 1, "__filter");
        if (lua_isstring(L, -1)) {
            sql += " WHERE " + std::string(lua_tostring(L, -1));
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) {
            sql += " ORDER BY " + std::string(lua_tostring(L, -1));
        }
        lua_pop(L, 1);

        sql += " LIMIT 1;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return luaL_error(L, "SQLite error: %s", sqlite3_errmsg(LumeniteDB::db_instance->handle));
        }

        bind_filter_args(L, stmt); // <- bind parameters securely

        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            lua_newtable(L);
            int col_count = sqlite3_column_count(stmt);
            for (int i = 0; i < col_count; ++i) {
                const char *col = sqlite3_column_name(stmt, i);
                const unsigned char *val = sqlite3_column_text(stmt, i);
                if (val) lua_pushstring(L, (const char *) val);
                else lua_pushnil(L);
                lua_setfield(L, -2, col);
            }
        } else {
            lua_pushnil(L);
        }

        sqlite3_finalize(stmt);
        return 1;
    }, 1);
    lua_setfield(L, -2, "first");


    // query.all()
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        const char *tablename = lua_tostring(L, lua_upvalueindex(1));
        std::string sql = "SELECT * FROM " + std::string(tablename);

        lua_getfield(L, 1, "__filter");
        if (lua_isstring(L, -1)) {
            sql += " WHERE " + std::string(lua_tostring(L, -1));
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) {
            sql += " ORDER BY " + std::string(lua_tostring(L, -1));
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "__limit");
        if (lua_isinteger(L, -1)) {
            sql += " LIMIT " + std::to_string(lua_tointeger(L, -1));
        }
        lua_pop(L, 1);

        sql += ";";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(LumeniteDB::db_instance->handle, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return luaL_error(L, "SQLite error: %s", sqlite3_errmsg(LumeniteDB::db_instance->handle));
        }

        bind_filter_args(L, stmt); // <- secure binding of user input

        lua_newtable(L);
        int index = 1;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            lua_newtable(L);
            int col_count = sqlite3_column_count(stmt);
            for (int i = 0; i < col_count; ++i) {
                const char *col = sqlite3_column_name(stmt, i);
                const unsigned char *val = sqlite3_column_text(stmt, i);
                if (val) lua_pushstring(L, (const char *) val);
                else lua_pushnil(L);
                lua_setfield(L, -2, col);
            }
            lua_rawseti(L, -2, index++);
        }

        sqlite3_finalize(stmt);
        return 1;
    }, 1);
    lua_setfield(L, -2, "all");
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
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "__tablename");
    const char *tablename = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    Model model;
    model.tablename = tablename;

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

    // Generate the model table and return
    create_model_table(L, model);
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
