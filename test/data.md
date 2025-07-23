Perfect — here's the first part of the fully C++-implemented Lumenite ORM that works exactly with this Lua code:

```lua
local db = require("lumenite.db")

local User = db.Model {
    __tablename = "users",
    id = db.Column("id", db.Integer, { primary_key = true }),
    name = db.Column("name", db.String),
    created_at = db.Column("created_at", db.String)
}

db.create_all()

local user = User({
    id = 1,
    name = "Dave",
    created_at = "2024-01-01"
})

db.session.add(user)
db.session.commit()

local results = User.query:get():order_by(User.created_at:desc()):limit(2):all()
```

---

### 🧱 Overview

We’ll build:

- ✅ `db.Model{...}` → model registration in C++
- ✅ `db.Column(...)` → column objects w/ type + metadata
- ✅ `User({...})` → object instance creation (Lua-friendly C++)
- ✅ `db.session.add(...)` / `commit()` → INSERT queued objects
- ✅ `User.query:get():order_by(...):limit(...):all()` → fluent SELECT
- ✅ `User.created_at:desc()` → column expression in Lua, resolved in C++

---

## ✅ Core API Sketch (`LumeniteORM.cpp`)

Here’s part 1: **Model registration, columns, and basic session scaffolding**.

### 🔹 C++: Column Definition (Column\:desc)

```c++
static int column_desc(lua_State *L) {
    const char *colname = luaL_checkstring(L, 1);
    lua_pushfstring(L, "%s DESC", colname);
    return 1;
}

static int column_asc(lua_State *L) {
    const char *colname = luaL_checkstring(L, 1);
    lua_pushfstring(L, "%s ASC", colname);
    return 1;
}

static void push_column(lua_State *L, const std::string &name, const std::string &type, bool is_primary) {
    lua_newtable(L);

    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "__column_name");

    lua_pushstring(L, type.c_str());
    lua_setfield(L, -2, "__column_type");

    lua_pushboolean(L, is_primary);
    lua_setfield(L, -2, "__primary_key");

    // Add desc() method
    lua_pushstring(L, name.c_str());
    lua_pushcclosure(L, column_desc, 1);
    lua_setfield(L, -2, "desc");

    lua_pushstring(L, name.c_str());
    lua_pushcclosure(L, column_asc, 1);
    lua_setfield(L, -2, "asc");
}
```

### 🔹 C++: `db.Column(name, type, opts)`

```c++
static int l_column(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *type = luaL_checkstring(L, 2);
    bool is_primary = false;

    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "primary_key");
        if (lua_isboolean(L, -1)) is_primary = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    push_column(L, name, type, is_primary);
    return 1;
}
```

---

### 🔹 C++: `db.Model{...}`

```c++
static int l_model(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::string tablename;

    lua_getfield(L, 1, "__tablename");
    if (!lua_isstring(L, -1))
        return luaL_error(L, "Model must have __tablename");
    tablename = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Create metatable for instances
    lua_newtable(L); // metatable
    lua_pushvalue(L, 1);         // store original model def as metatable.__model_def
    lua_setfield(L, -2, "__model_def");

    lua_pushcfunction(L, [](lua_State *L) -> int {
        lua_newtable(L); // user instance

        lua_getmetatable(L, 1);
        lua_getfield(L, -1, "__model_def");
        lua_replace(L, -2); // now stack[-1] = model_def

        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const char *key = lua_tostring(L, -2);
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "__column_name");
                if (lua_isstring(L, -1)) {
                    const char *col = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, 2, key);
                    lua_setfield(L, -4, col);
                } else {
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }

        return 1;
    });
    lua_setfield(L, -2, "__call");

    lua_newtable(L); // Model table to return
    lua_pushvalue(L, 1); lua_setfield(L, -2, "__model_def");
    lua_pushstring(L, tablename.c_str()); lua_setfield(L, -2, "__tablename");

    // Map model.name = "colname" for query usage
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        const char *key = lua_tostring(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "__column_name");
            if (lua_isstring(L, -1)) {
                lua_setfield(L, -4, key); // model.key = "colname"
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    // Push model.__call metatable
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2); // set call behavior

    return 1;
}
```

---

### 🔹 C++: Session queue (to support `db.session.add(obj)`)

```c++
std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>> pending_inserts;

static int l_session_add(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__model_def");
    lua_getfield(L, -1, "__tablename");
    const char *tablename = lua_tostring(L, -1);
    lua_pop(L, 1);

    std::unordered_map<std::string, std::string> row;

    lua_pushnil(L);
    while (lua_next(L, -2)) {
        const char *key = lua_tostring(L, -2);
        lua_getfield(L, -2, key);
        if (lua_isstring(L, -1))
            row[key] = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_pop(L, 1);
    }

    pending_inserts.emplace_back(tablename, std::move(row));
    return 0;
}
```

---

### 🔹 C++: `db.session.commit()`

```c++
static sqlite3 *global_db = nullptr;

static int l_session_commit(lua_State *L) {
    for (const auto &[table, row] : pending_inserts) {
        std::ostringstream fields, values;
        for (auto &[k, v] : row) {
            if (fields.tellp() > 0) {
                fields << ", ";
                values << ", ";
            }
            fields << k;
            values << "'" << v << "'";
        }

        std::string sql = "INSERT INTO " + table + " (" + fields.str() + ") VALUES (" + values.str() + ")";
        sqlite3_exec(global_db, sql.c_str(), nullptr, nullptr, nullptr);
    }

    pending_inserts.clear();
    return 0;
}
```

---

### 🔹 C++: Registration Entry

```c++
extern "C" int luaopen_lumenite_db(lua_State *L) {
    lua_newtable(L);

    lua_pushcfunction(L, l_column); lua_setfield(L, -2, "Column");
    lua_pushcfunction(L, l_model); lua_setfield(L, -2, "Model");

    lua_pushstring(L, "TEXT"); lua_setfield(L, -2, "String");
    lua_pushstring(L, "INTEGER"); lua_setfield(L, -2, "Integer");

    // Session table
    lua_newtable(L);
    lua_pushcfunction(L, l_session_add); lua_setfield(L, -2, "add");
    lua_pushcfunction(L, l_session_commit); lua_setfield(L, -2, "commit");
    lua_setfield(L, -2, "session");

    return 1;
}
```

---

## 🔜 Next

The final part still missing is:

- `db.create_all()` — generate tables from registered models
- `User.query:get():order_by(...):limit(...):all()` — full SELECT support

Want me to deliver the query engine next (query context, fluent builder, and result rows)?
