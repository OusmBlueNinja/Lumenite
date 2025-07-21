#include <sstream>

#include "LumeniteDb.h"


bool LumeniteDB::DB::open(const std::string &file)
{
    if (sqlite3_open(file.c_str(), &handle) != SQLITE_OK) {
        error = sqlite3_errmsg(handle);
        return false;
    }
    return true;
}

bool LumeniteDB::DB::exec(const std::string &sql)
{
    char *msg = nullptr;
    if (sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &msg) != SQLITE_OK) {
        error = msg;
        sqlite3_free(msg);
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
    return static_cast<DB **>(luaL_checkudata(L, 1, "LumeniteDB"));
}

int LumeniteDB::db_open(lua_State *L)
{
    auto *db = *check(L);
    const char *file = luaL_checkstring(L, 2);
    lua_pushboolean(L, db->open(file));
    return 1;
}

int LumeniteDB::db_exec(lua_State *L)
{
    auto *db = *check(L);
    const char *sql = luaL_checkstring(L, 2);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lua_pushboolean(L, 0);
        db->error = sqlite3_errmsg(db->handle);
        return 1;
    }

    // Bind parameters
    int nparams = lua_gettop(L) - 2;
    for (int i = 0; i < nparams; ++i) {
        if (lua_isinteger(L, i + 3))
            sqlite3_bind_int64(stmt, i + 1, lua_tointeger(L, i + 3));
        else if (lua_isnumber(L, i + 3))
            sqlite3_bind_double(stmt, i + 1, lua_tonumber(L, i + 3));
        else if (lua_isboolean(L, i + 3))
            sqlite3_bind_int(stmt, i + 1, lua_toboolean(L, i + 3));
        else if (lua_isnil(L, i + 3))
            sqlite3_bind_null(stmt, i + 1);
        else if (lua_isstring(L, i + 3))
            sqlite3_bind_text(stmt, i + 1, lua_tostring(L, i + 3), -1, SQLITE_TRANSIENT);
        else {
            sqlite3_finalize(stmt);
            db->error = "Unsupported parameter type";
            lua_pushboolean(L, 0);
            return 1;
        }
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        db->error = sqlite3_errmsg(db->handle);
        sqlite3_finalize(stmt);
        lua_pushboolean(L, 0);
        return 1;
    }

    sqlite3_finalize(stmt);
    lua_pushboolean(L, 1);
    return 1;
}


int LumeniteDB::db_query(lua_State *L)
{
    auto *db = *check(L);
    const char *sql = luaL_checkstring(L, 2);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lua_pushnil(L);
        lua_pushstring(L, sqlite3_errmsg(db->handle));
        return 2;
    }

    // Bind parameters
    int nparams = lua_gettop(L) - 2;
    for (int i = 0; i < nparams; ++i) {
        if (lua_isinteger(L, i + 3))
            sqlite3_bind_int64(stmt, i + 1, lua_tointeger(L, i + 3));
        else if (lua_isnumber(L, i + 3))
            sqlite3_bind_double(stmt, i + 1, lua_tonumber(L, i + 3));
        else if (lua_isboolean(L, i + 3))
            sqlite3_bind_int(stmt, i + 1, lua_toboolean(L, i + 3));
        else if (lua_isnil(L, i + 3))
            sqlite3_bind_null(stmt, i + 1);
        else if (lua_isstring(L, i + 3))
            sqlite3_bind_text(stmt, i + 1, lua_tostring(L, i + 3), -1, SQLITE_TRANSIENT);
        else {
            sqlite3_finalize(stmt);
            lua_pushnil(L);
            lua_pushstring(L, "Unsupported parameter type");
            return 2;
        }
    }

    lua_newtable(L);
    int row_index = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        lua_newtable(L);
        int col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char *key = sqlite3_column_name(stmt, i);
            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_INTEGER:
                    lua_pushinteger(L, sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    lua_pushnumber(L, sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    lua_pushstring(L, reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
                    break;
                case SQLITE_NULL:
                    lua_pushnil(L);
                    break;
                default:
                    lua_pushstring(L, "[unsupported type]");
                    break;
            }
            lua_setfield(L, -2, key);
        }
        lua_rawseti(L, -2, row_index++);
    }

    sqlite3_finalize(stmt);
    return 1;
}


int LumeniteDB::db_error(lua_State *L)
{
    auto *db = *check(L);
    lua_pushstring(L, db->lastError().c_str());
    return 1;
}

int LumeniteDB::db_gc(lua_State *L)
{
    delete *check(L);
    return 0;
}


int LumeniteDB::db_sanitize(lua_State *L)
{
    const char *input = luaL_checkstring(L, lua_gettop(L)); // allow db:sanitize or db.sanitize

    std::ostringstream escaped;
    escaped << "'";

    for (const char *p = input; *p; ++p) {
        if (*p == '\'') escaped << "''";
        else escaped << *p;
    }

    escaped << "'";
    lua_pushstring(L, escaped.str().c_str());
    return 1;
}



extern "C" int luaopen_LumeniteDB(lua_State *L)
{
    using DB = LumeniteDB::DB;

    // Create metatable
    luaL_newmetatable(L, "LumeniteDB");

    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, LumeniteDB::db_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, LumeniteDB::db_exec);
    lua_setfield(L, -2, "exec");
    lua_pushcfunction(L, LumeniteDB::db_query);
    lua_setfield(L, -2, "query");
    lua_pushcfunction(L, LumeniteDB::db_error);
    lua_setfield(L, -2, "error");
    lua_pushcfunction(L, LumeniteDB::db_gc);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, LumeniteDB::db_sanitize);
    lua_setfield(L, -2, "sanitize");


    DB **ud = static_cast<DB **>(lua_newuserdata(L, sizeof(DB *)));
    *ud = new DB();

    luaL_getmetatable(L, "LumeniteDB");
    lua_setmetatable(L, -2);

    return 1;
}
