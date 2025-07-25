// src/modules/LumeniteDb.cpp

#include "LumeniteDB.h"
#include <sqlite3.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <ctime>


#include "../ErrorHandler.h"

std::string LumeniteDB::db_filename{};
bool LumeniteDB::sql_log_enabled{};
std::ofstream LumeniteDB::sql_log_stream{};


namespace fs = std::filesystem;


std::map<std::string, LumeniteDB::Model> LumeniteDB::models;
LumeniteDB::Session LumeniteDB::session;
LumeniteDB::DB *LumeniteDB::db_instance = nullptr;

static int instance_newindex(lua_State *L);

static void bind_filter_args(lua_State *L, sqlite3_stmt *stmt);


static std::string current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}


// ── Exec runner (no return) ───────────────────────────────────────────────
static void run_sql_exec(lua_State *L, const std::string &sql)
{
    if (LumeniteDB::sql_log_stream.is_open()) {
        LumeniteDB::sql_log_stream
                << "[" << current_timestamp() << "] " << sql << "\n";
        LumeniteDB::sql_log_stream.flush();
    }
    if (!LumeniteDB::db_instance)
        luaL_error(L, "No DB connection. Call db.open() first");
    if (!LumeniteDB::db_instance->exec(sql))
        luaL_error(L, "SQL error: %s",
                   LumeniteDB::db_instance->lastError().c_str());
}

// ── Query runner (returns Lua table + prints rows) ─────────────────────────
static int run_sql_query(lua_State *L, const std::string &sql)
{
    // 1) log the SQL
    if (LumeniteDB::sql_log_stream.is_open()) {
        LumeniteDB::sql_log_stream
                << "[" << current_timestamp() << "] " << sql << "\n";
        LumeniteDB::sql_log_stream.flush();
    }
    if (!LumeniteDB::db_instance)
        luaL_error(L, "No DB connection. Call db.open() first");

    // 2) prepare
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(
            LumeniteDB::db_instance->handle,
            sql.c_str(), -1, &stmt, nullptr
        ) != SQLITE_OK) {
        luaL_error(L, "SQLite prepare failed: %s",
                   sqlite3_errmsg(LumeniteDB::db_instance->handle));
    }

    // 3) bind any filter args
    bind_filter_args(L, stmt);

    // 4) fetch & print rows
    lua_newtable(L);
    int row = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int cols = sqlite3_column_count(stmt);

        //// —— debug print
        //std::cout << "[lumenite.db.result] row " << (row - 1) << ": ";
        //for (int c = 0; c < cols; ++c) {
        //    const char *col = sqlite3_column_name(stmt, c);
        //    const unsigned char *txt = sqlite3_column_text(stmt, c);
        //    const char *val = txt ? reinterpret_cast<const char *>(txt) : "NULL";
        //    std::cout << col << "=" << val;
        //    if (c + 1 < cols) std::cout << ", ";
        //}
        //std::cout << "\n";

        // —— build Lua table
        lua_newtable(L);
        for (int c = 0; c < cols; ++c) {
            const char *col = sqlite3_column_name(stmt, c);
            const unsigned char *txt = sqlite3_column_text(stmt, c);
            if (txt) lua_pushstring(L, reinterpret_cast<const char *>(txt));
            else lua_pushnil(L);
            lua_setfield(L, -2, col);
        }
        lua_rawseti(L, -2, row++);
    }

    // 5) cleanup & return the table
    sqlite3_finalize(stmt);
    return 1;
}


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

static int proxy_index(lua_State *L)
{
    lua_getfield(L, 1, "__data"); // push data table
    lua_pushvalue(L, 2); // push key
    lua_gettable(L, -2); // data[key]
    return 1;
}

static int proxy_newindex(lua_State *L)
{
    // upvalue #1 = tablename
    const char *tablename = lua_tostring(L, lua_upvalueindex(1));

    // 1) rawset into proxy.__data
    lua_getfield(L, 1, "__data"); // stack: proxy, key, val, data
    lua_pushvalue(L, 2); // key
    lua_pushvalue(L, 3); // val
    lua_rawset(L, -3); // data[key] = val
    lua_pop(L, 1); // pop data

    // 2) queue the UPDATE
    lua_getfield(L, 1, "__data");
    lua_getfield(L, -1, "id");
    const char *id = lua_tostring(L, -1);
    lua_pop(L, 2);

    LumeniteDB::Update upd;
    upd.tablename = tablename;
    upd.changes["id"] = id;
    lua_pushvalue(L, 3);
    const char *v = lua_tostring(L, -1);
    lua_pop(L, 1);
    upd.changes[lua_tostring(L, 2)] = v ? v : "";

    LumeniteDB::session.pending_updates.push_back(std::move(upd));
    return 0;
}


static void clone_query(lua_State *L)
{
    lua_newtable(L);
    int newTbl = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        // stack: [ ... , key, value ]
        lua_pushvalue(L, -2); // copy key
        lua_pushvalue(L, -2); // copy value
        lua_settable(L, newTbl); // newTbl[key] = value
        lua_pop(L, 1); // pop value, keep key for next()
    }

    // Replace slot‑1 with our newTbl
    lua_replace(L, 1);
}


static void register_default_query_methods(lua_State *L,
                                           int idx,
                                           const std::string &tablename)
{
    // ─── order_by(expr) ──────────────────────────────────────────────────────
    // Just remember the ORDER BY clause; don't execute yet.
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        clone_query(L);
        const char *expr = luaL_checkstring(L, 2);
        lua_pushstring(L, expr);
        lua_setfield(L, 1, "__order_by");
        lua_pushvalue(L, 1);
        return 1;
    }, 1);
    lua_setfield(L, idx, "order_by");

    // ─── limit(n) ────────────────────────────────────────────────────────────
    // Just remember the LIMIT; don't execute yet.
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        clone_query(L);
        int n = luaL_checkinteger(L, 2);
        lua_pushinteger(L, n);
        lua_setfield(L, 1, "__limit");
        lua_pushvalue(L, 1);
        return 1;
    }, 1);
    lua_setfield(L, idx, "limit");

    // ─── filter_by({ k=v, … }) ────────────────────────────────────────────────
    // Build a WHERE‑fragment and args list; don't execute until first()/all().
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        clone_query(L);
        luaL_checktype(L, 2, LUA_TTABLE);

        std::stringstream where;
        bool first = true;

        lua_newtable(L);
        int argsIdx = lua_gettop(L);

        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            const char *col = lua_tostring(L, -2);
            if (first) {
                where << "WHERE ";
                first = false;
            } else {
                where << " AND ";
            }
            where << col << " = ?";

            // collect the value
            lua_pushvalue(L, -1);
            lua_rawseti(L, argsIdx, lua_rawlen(L, argsIdx) + 1);
            lua_pop(L, 1);
        }

        // store just the fragment (no SELECT, no semicolon)
        lua_pushstring(L, where.str().c_str());
        lua_setfield(L, 1, "__filter_sql");
        lua_pushvalue(L, argsIdx);
        lua_setfield(L, 1, "__filter_args");

        lua_pushvalue(L, 1);
        return 1;
    }, 1);
    lua_setfield(L, idx, "filter_by");

    // … inside register_default_query_methods …

    // ─── get(id) ─────────────────────────────────────────────────────────────
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        const char *tbl = lua_tostring(L, lua_upvalueindex(1));
        // pick arg 1 or 2 for colon vs dot call
        int arg = (lua_istable(L, 1) && (lua_isinteger(L, 2) || lua_isstring(L, 2))) ? 2 : 1;
        if (!lua_isinteger(L, arg) && !lua_isstring(L, arg))
            return luaL_error(L, "Expected integer or string ID");

        // stash single filter arg
        lua_newtable(L);
        lua_pushvalue(L, arg);
        lua_rawseti(L, -2, 1);
        lua_setfield(L, 1, "__filter_args");

        std::string sql = std::string("SELECT * FROM ") + tbl +
                          " WHERE id = ? LIMIT 1;";
        // pushes a 1‑element array of rows
        run_sql_query(L, sql);

        // extract the first row
        lua_rawgeti(L, -1, 1);
        lua_replace(L, -2); // stack now: [ row_table ]

        // wrap in proxy
        lua_newtable(L); // [ row, proxy ]
        lua_pushvalue(L, -2); // [ row, proxy, row ]
        lua_setfield(L, -2, "__data"); // proxy.__data = row

        // build metatable
        lua_newtable(L); // [ row, proxy, mt ]
        lua_pushcfunction(L, proxy_index);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, tbl);
        lua_pushcclosure(L, proxy_newindex, 1);
        lua_setfield(L, -2, "__newindex");
        lua_setmetatable(L, -2); // set mt on proxy

        lua_remove(L, -2); // remove raw row, leave [ proxy ]
        return 1;
    }, 1);
    lua_setfield(L, idx, "get");

    // ─── first() ────────────────────────────────────────────────────────────
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        const char *tbl = lua_tostring(L, lua_upvalueindex(1));
        std::stringstream ss;
        ss << "SELECT * FROM " << tbl << " ";

        lua_getfield(L, 1, "__filter_sql");
        if (lua_isstring(L, -1)) ss << lua_tostring(L, -1) << " ";
        lua_pop(L, 1);

        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) ss << "ORDER BY " << lua_tostring(L, -1) << " ";
        lua_pop(L, 1);

        ss << "LIMIT 1;";
        run_sql_query(L, ss.str());

        lua_rawgeti(L, -1, 1);
        lua_replace(L, -2); // [ row_table ]

        lua_newtable(L);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "__data");

        lua_newtable(L);
        lua_pushcfunction(L, proxy_index);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, tbl);
        lua_pushcclosure(L, proxy_newindex, 1);
        lua_setfield(L, -2, "__newindex");
        lua_setmetatable(L, -2);

        lua_remove(L, -2);
        return 1;
    }, 1);
    lua_setfield(L, idx, "first");


    // ─── all() ──────────────────────────────────────────────────────────────
    lua_pushstring(L, tablename.c_str());
    lua_pushcclosure(L, [](lua_State *L)-> int
    {
        const char *tbl = lua_tostring(L, lua_upvalueindex(1));
        std::stringstream ss;
        ss << "SELECT * FROM " << tbl << " ";

        lua_getfield(L, 1, "__filter_sql");
        if (lua_isstring(L, -1)) ss << lua_tostring(L, -1) << " ";
        lua_pop(L, 1);

        lua_getfield(L, 1, "__order_by");
        if (lua_isstring(L, -1)) ss << "ORDER BY " << lua_tostring(L, -1) << " ";
        lua_pop(L, 1);

        lua_getfield(L, 1, "__limit");
        if (lua_isinteger(L, -1)) ss << "LIMIT " << lua_tointeger(L, -1) << " ";
        lua_pop(L, 1);

        ss << ";";
        return run_sql_query(L, ss.str());
    }, 1);
    lua_setfield(L, idx, "all");
}


static int model_new(lua_State *L)
{
    int def = (!lua_istable(L, 1) && lua_istable(L, 2)) ? 2 : 1;
    luaL_checktype(L, def, LUA_TTABLE);

    lua_newtable(L);
    int inst = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, def)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_settable(L, inst);
        lua_pop(L, 1);
    }

    const char *mtn = lua_tostring(L, lua_upvalueindex(1));
    luaL_getmetatable(L, mtn);
    lua_setmetatable(L, -2);
    return 1;
}

static void create_model_table(lua_State *L, const LumeniteDB::Model &M)
{
    // instance metatable (for User.new)
    std::string mtn = "LumeniteDB.instance." + M.tablename;
    luaL_newmetatable(L, mtn.c_str());
    lua_pushstring(L, M.tablename.c_str());
    lua_setfield(L, -2, "__model");
    lua_pushcfunction(L, instance_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    // model table
    lua_newtable(L);
    int md = lua_gettop(L);

    // .new
    lua_pushstring(L, mtn.c_str());
    lua_pushcclosure(L, model_new, 1);
    lua_setfield(L, md, "new");

    // column helpers (User.name:asc(), :desc())
    for (auto &c: M.columns) {
        lua_newtable(L);
        lua_pushstring(L, c.name.c_str());
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *col = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s ASC", col);
            return 1;
        }, 1);
        lua_setfield(L, -2, "asc");

        lua_pushstring(L, c.name.c_str());
        lua_pushcclosure(L, [](lua_State *L)-> int
        {
            const char *col = lua_tostring(L, lua_upvalueindex(1));
            lua_pushfstring(L, "%s DESC", col);
            return 1;
        }, 1);
        lua_setfield(L, -2, "desc");

        lua_setfield(L, md, c.name.c_str());
    }

    // .query
    lua_newtable(L);
    register_default_query_methods(L, lua_gettop(L), M.tablename);
    lua_setfield(L, md, "query");
}


// Catches foo.name = "bar" and queues an UPDATE
static int instance_newindex(lua_State *L)
{
    lua_settop(L, 3);
    lua_rawset(L, 1);

    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__model");
    const char *tn = lua_tostring(L, -1);
    lua_pop(L, 2);

    lua_getfield(L, 1, "id");
    const char *id = lua_tostring(L, -1);
    lua_pop(L, 1);

    LumeniteDB::Update upd;
    upd.tablename = tn;
    upd.changes["id"] = id;
    lua_pushvalue(L, 3);
    const char *v = lua_tostring(L, -1);
    lua_pop(L, 1);
    upd.changes[lua_tostring(L, 2)] = v ? v : "";

    LumeniteDB::session.pending_updates.push_back(std::move(upd));
    return 0;
}


bool LumeniteDB::DB::open(const std::string &f)
{
    return sqlite3_open(f.c_str(), &handle) == SQLITE_OK;
}

bool LumeniteDB::DB::exec(const std::string &sql)
{
    char *err = nullptr;
    if (sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        error = err ? err : "Unknown error";
        sqlite3_free(err);
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
    const char *fn = luaL_checkstring(L, 1);
    db_filename = fn;

    fs::path dbdir("db");
    if (!fs::exists(dbdir)) fs::create_directories(dbdir);

    fs::path logdir("log");
    if (!fs::exists(logdir)) fs::create_directories(logdir);

    fs::path full = dbdir / fn;
    auto **ud = static_cast<DB **>(lua_newuserdata(L, sizeof(DB*)));
    *ud = new DB();
    luaL_getmetatable(L, "LumeniteDB.DB");
    lua_setmetatable(L, -2);

    if (!(*ud)->open(full.string())) {
        lua_pushnil(L);
        lua_pushstring(L, (*ud)->lastError().c_str());
        return 2;
    }

    fs::path logfile = logdir / (db_filename + ".log");
    sql_log_stream.open(logfile, std::ios::app);
    if (!sql_log_stream) {
        std::cerr << "Warning: could not open SQL log at "
                << logfile.string() << "\n";
    }

    db_instance = *ud;
    return 1;
}


int LumeniteDB::db_gc(lua_State *L)
{
    delete *check(L);
    return 0;
}


int LumeniteDB::db_column(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *type = luaL_checkstring(L, 2);

    bool pk = false;
    std::string defv; // will hold whatever `options.default` was

    if (lua_istable(L, 3)) {
        // primary_key?
        lua_getfield(L, 3, "primary_key");
        pk = lua_toboolean(L, -1);
        lua_pop(L, 1);

        // default?
        lua_getfield(L, 3, "default");
        if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
            defv = lua_tostring(L, -1); // number→string or string
        }
        lua_pop(L, 1);
    }

    // return { name=…, type=…, primary_key=…, default_value=… }
    lua_newtable(L);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, type);
    lua_setfield(L, -2, "type");
    lua_pushboolean(L, pk);
    lua_setfield(L, -2, "primary_key");
    lua_pushstring(L, defv.c_str());
    lua_setfield(L, -2, "default_value"); // <— changed key here
    return 1;
}

int LumeniteDB::db_model(lua_State *L)
{
    int d = 1;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) d = 2;
    luaL_checktype(L, d, LUA_TTABLE);

    // __tablename
    lua_getfield(L, d, "__tablename");
    if (!lua_isstring(L, -1))
        return luaL_error(L, "db.Model: missing '__tablename'");
    std::string tn = lua_tostring(L, -1);
    lua_pop(L, 1);

    Model M;
    M.tablename = tn;

    lua_pushnil(L);
    while (lua_next(L, d)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            Column c;
            lua_getfield(L, -1, "name");
            c.name = luaL_checkstring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "type");
            c.type = luaL_checkstring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "primary_key");
            c.primary_key = lua_toboolean(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "default_value");
            c.default_value = luaL_optstring(L, -1, "");
            lua_pop(L, 1);

            M.columns.push_back(std::move(c));
        }
        lua_pop(L, 1);
    }

    models[tn] = M;
    create_model_table(L, M);
    return 1;
}


int LumeniteDB::db_create_all(lua_State *L)
{
    for (auto it = models.rbegin(); it != models.rend(); ++it) {
        const auto &[tn,mdl] = *it;
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << tn << " (";
        for (size_t i = 0; i < mdl.columns.size(); ++i) {
            const auto &c = mdl.columns[i];
            ss << c.name << " " << c.type;
            if (c.primary_key) ss << " PRIMARY KEY";
            if (!c.default_value.empty()) {
                bool num = std::all_of(c.default_value.begin(), c.default_value.end(),
                                       [](unsigned char ch) { return std::isdigit(ch) || ch == '-' || ch == '+'; });
                ss << " DEFAULT " << (num ? c.default_value : ("'" + c.default_value + "'"));
            }
            if (i + 1 < mdl.columns.size()) ss << ", ";
        }
        ss << ");";
        run_sql_exec(L, ss.str());
    }
    return 0;
}

int LumeniteDB::db_session_add(lua_State *L)
{
    luaL_checktype(L, 1,LUA_TTABLE);
    Row row;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        row.values[lua_tostring(L, -2)] = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
    }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__model");
    session.tablename = lua_tostring(L, -1);
    lua_pop(L, 2);
    session.pending_inserts.push_back(std::move(row));
    return 0;
}

int LumeniteDB::db_session_commit(lua_State *L)
{
    // INSERTs
    for (auto &r: session.pending_inserts) {
        std::stringstream k, v;
        k << "(";
        v << "(";
        bool first = true;
        for (auto &kv: r.values) {
            if (!first) {
                k << ", ";
                v << ", ";
            }
            first = false;
            k << kv.first;
            v << "'" << kv.second << "'";
        }
        k << ")";
        v << ")";

        // build and print the INSERT SQL
        std::string sql =
                "INSERT INTO " + session.tablename +
                " " + k.str() + " VALUES " + v.str() + ";";

        run_sql_exec(L, sql);
    }
    session.pending_inserts.clear();

    // UPDATEs
    for (auto &u: session.pending_updates) {
        std::stringstream ss;
        ss << "UPDATE " << u.tablename << " SET ";
        bool first = true;
        for (auto &cv: u.changes) {
            if (cv.first == "id") continue;
            if (!first) ss << ", ";
            first = false;
            ss << cv.first << "='" << cv.second << "'";
        }
        ss << " WHERE id='" << u.changes["id"] << "';";

        // build and print the UPDATE SQL
        std::string sql = ss.str();

        run_sql_exec(L, sql);
    }
    session.pending_updates.clear();

    return 0;
}


int LumeniteDB::db_select_all(lua_State *L)
{
    const char *tn = luaL_checkstring(L, 1);
    std::string q = std::string("SELECT * FROM ") + tn + ";";
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_instance->handle, q.c_str(), -1, &s, nullptr) != SQLITE_OK)
        return luaL_error(L, "SQLite prepare failed: %s", sqlite3_errmsg(db_instance->handle));

    lua_newtable(L);
    int i = 1;
    while (sqlite3_step(s) == SQLITE_ROW) {
        lua_newtable(L);
        for (int c = 0; c < sqlite3_column_count(s); ++c) {
            const char *col = sqlite3_column_name(s, c);
            const unsigned char *txt = sqlite3_column_text(s, c);
            if (txt) lua_pushstring(L, (const char *) txt);
            else lua_pushnil(L);
            lua_setfield(L, -2, col);
        }

        lua_rawseti(L, -2, i++);
    }
    sqlite3_finalize(s);
    return 1;
}

extern "C" int luaopen_lumenite_db(lua_State *L)
{
    std::cout << YELLOW << "[~] Notice  : " << RESET
            << "The module " << BOLD << "'lumenite.db'" << RESET
            << " is currently in " << BOLD RED << "Alpha" << RESET << ".\n"
            << "             Use with caution - it may be incomplete or insecure.\n";

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
