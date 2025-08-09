#include "ProjectScaffolder.h"
#include "../ErrorHandler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include "Version.h"
namespace fs = std::filesystem;


std::string ProjectScaffolder::colorizePath(const std::string &pathStr, const std::string &projectName) const
{
    fs::path path(pathStr);
    std::ostringstream out;
    const std::string sep = "/";

    fs::path current;
    bool first = true;

    for (const auto &part: path) {
        std::string partStr = part.string();
        current /= part;
        fs::path absolute = rootPath / current;

        if (!first)
            out << sep;
        else
            first = false;

        if (partStr == projectName) {
            out << MAGENTA << BOLD << partStr << RESET;
        } else if (fs::exists(absolute) && fs::is_directory(absolute)) {
            out << LBLUE << partStr << RESET; // folder
        } else {
            out << LGREEN << partStr << RESET; // file
        }
    }

    return out.str();
}


void ProjectScaffolder::log(const std::string &action, const std::string &text) const
{
    std::string prefix = RESET "[" GREEN "+" RESET"] ";
    std::string color;

    if (action == "Created") {
        color = BOLD GREEN;
    } else if (action == "Wrote") {
        color = BOLD BLUE;
    } else if (action == "Skipped" || action == "Warning") {
        color = YELLOW;
        prefix = RESET "[" YELLOW "!" RESET"] ";
    } else if (action == "Note") {
        color = CYAN;
        prefix = RESET "[" CYAN "~" RESET"] ";
    } else if (action == "Error") {
        color = BOLD RED;
        prefix = RESET "[" BOLD RED "x" RESET"] ";
    } else {
        color = RESET;
    }

    std::ostringstream aligned;
    aligned << std::left << std::setw(8) << action;

    std::cout << prefix << color << aligned.str() << ":" << RESET << " ";

    if (action == "Created" || action == "Wrote" || action == "Skipped")
        std::cout << colorizePath(text, projectName);
    else
        std::cout << color << text;

    std::cout << RESET << "\n";
}


void ProjectScaffolder::createDir(const fs::path &path) const
{
    const fs::path full = rootPath / path;

    if (fs::exists(full)) {
        if (deleteExisting) {
            std::error_code ec;
            fs::remove_all(full, ec);
            if (!ec) {
                log("Deleted", path.string());
                fs::create_directories(full);
                log("Created", path.string());
            } else {
                log("Error", "Failed to delete directory: " + ec.message());
            }
            return;
        }

        if (!force) {
            log("Skipped", path.string());
            return;
        }
    }

    fs::create_directories(full);
    log("Created", path.string());
}

void ProjectScaffolder::writeFile(const fs::path &path, const std::string &content) const
{
    const fs::path fullPath = rootPath / path;

    if (fs::exists(fullPath)) {
        if (deleteExisting) {
            std::error_code ec;
            fs::remove(fullPath, ec);
            if (!ec) {
                log("Deleted", path.string());
            } else {
                log("Error", "Failed to delete file: " + ec.message());
                return;
            }
        } else if (!force) {
            log("Skipped", path.string());
            return;
        }
    }

    if (std::ofstream file(fullPath); file) {
        file << content;
        log("Wrote", path.string());
    }
}


void ProjectScaffolder::createWorkspace(const std::string &name, const std::vector<std::string> &args)
{
    projectName = name;
    rootPath = fs::current_path() / name;


    for (const auto &arg: args) {
        if (arg == "--force") force = true;
        if (arg == "--delete") deleteExisting = true;
    }

    if (fs::exists(rootPath)) {
        if (deleteExisting) {
            std::error_code ec;
            fs::remove_all(rootPath, ec);
            if (ec) {
                log("Error", "failed to delete: " + ec.message());

                return;
            }

            log("Deleting", rootPath.filename().string());
            fs::create_directories(rootPath);
        } else if (!force) {
            log("Error", "directory already exists:  " + rootPath.string());
            log("Note", "Use '--force' to overwrite files or '--delete' to fully rebuild.");

            return;
        } else {
            log("Warning", " writing into existing directory: " + rootPath.filename().string());
        }
    } else {
        fs::create_directories(rootPath);
    }


    log("Created", name);

    // Banner
    std::cout << "\n" <<
            MOON1 << " _                                _ _       \n" <<
            MOON2 << "| |                              (_) |      \n" <<
            PURPLE << "| |    _   _ _ __ ___   ___ _ __  _| |_ ___ \n" <<
            MOON4 << "| |   | | | | '_ ` _ \\ / _ \\ '_ \\| | __/ _ \\\n" <<
            MOON5 << "| |___| |_| | | | | | |  __/ | | | | ||  __/\n" <<
            MOON6 << "\\_____/\\__,_|_| |_| |_|\\___|_| |_|_|\\__\\___|\n" <<
            RESET << std::endl;

    std::cout << BOLD << MOON6 << "A fresh Lumenite project\n\n" << RESET;

    std::cout << RESET "[" CYAN "*" RESET "] Initializing Lumenite project in: " << colorizePath(
        rootPath.string(), name) << RESET << "\n";

    std::ostringstream config;
    config << "project_name: " << projectName << "\n";
    config << "lumenite_version: " LUMENITE_RELEASE_VERSION "\n";

    writeFile("config.luma", config.str());

    createDir("app");

    createDir("app/routes");


    // --- app/routes/web.lua ---
    std::string webRoutes = R"(-- app/routes/web.lua
local crypto = require("lumenite.crypto")
local models = require("app.models")

--[[
   Web Routes

   Define routes that render HTML views or templates.
   These are typically used for browser-facing endpoints.

   You can define routes using:
     app:get(path, handler)
     app:post(path, handler)
--]]

app:get("/", function(request)
    return app.render_template("template.html", {
        title = "Welcome to Lumenite",
        project_name = "{{project_name}}",
        content = "<p>This content was injected into the layout.</p>",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC")
    })
end)
)";

    if (const size_t pos = webRoutes.find("{{project_name}}"); pos != std::string::npos) {
        webRoutes.replace(pos, 16, name);
    }
    writeFile("app/routes/web.lua", webRoutes);

    // --- app/routes/api.lua ---
    const std::string apiRoutes = R"(-- app/routes/api.lua
local models = require("app.models")

--[[
   API Routes

   Define routes that return JSON responses (REST-style).
   These are typically used by client apps or JavaScript.

   You can define routes using:
     app:get(path, handler)
     app:post(path, handler)
--]]

app:get("/api/ping", function(request)
    return app.jsonify({
        status = "ok",
        time = os.date("!%Y-%m-%d %H:%M:%S UTC"),
        headers = request.headers
    })
end)

)";

    writeFile("app/routes/api.lua", apiRoutes);


    // app/filters.lua
    writeFile("app/filters.lua", R"(-- app/filters.lua
local safe = require("lumenite.safe")

--[[
   Template Filters

   This file defines custom filters available in your templates.

   Filters allow you to transform data inside templates:
     Example usage in template.html:
       {{ title | upper }}         -- convert title to uppercase
       {{ content | safe }}        -- mark content as safe HTML

   Defining a filter:
     app:template_filter("name", function(input)
         -- do something with input
         return result
     end)

   This example defines a 'safe' filter using the Lumenite Safe module,
   which escapes HTML to prevent XSS vulnerabilities.

   You can add more filters here, like:
     "truncate", "markdown", "date_format", etc.
--]]



app:template_filter("safe", function(input)
    return safe.escape(input)
end)

)");

    // app/middleware.lua
    writeFile("app/middleware.lua", R"(-- app/middleware.lua
local models = require("app.models")

--[[
   Middleware configuration for Lumenite.
   Use this file to register hooks that run before or after each request.

   - app.before_request(fn): Called before every route
   - app.after_request(fn):  Called after every route

   Example use cases:
   • Logging
   • Authentication
   • Header manipulation
--]]

app.before_request(function(req)
    -- Example: log the User-Agent
    -- print(req.headers["User-Agent"])
end)

app.after_request(function(request, response)
    response.headers["X-Powered-By"] = "Lumenite"
    return response
end)


)");

    writeFile("app/models.lua", R"(-- app/models.lua

---@diagnostic disable: undefined-global
local db = require("lumenite.db")

-- 1) Open (or create) the SQLite file under ./db/
--    The engine ensures ./db and ./log exist and enables PRAGMA foreign_keys.
local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

-- 2) Define models
-- Tip: Use INTEGER for primary keys. SQLite will back it by rowid.
local User = db.Model{
  __tablename = "users",
  id          = db.Column("id", "INTEGER", { primary_key = true }),
  name        = db.Column("name", "TEXT"),
  created_at  = db.Column("created_at", "INTEGER", { default = os.time() }),
}

-- 3) Create tables if they don’t exist
db.create_all()

-- 4) Seed data (only if empty)
if (User.query:count() == 0) then
  for _, name in ipairs({ "Alice", "Bob", "Charlie" }) do
    db.session_add(User.new{ name = name })
  end
  db.session_commit()
end

-- 5) Example: select_all (plain tables; values are strings or nil)
do
  local all = db.select_all("users")
  print("All users:")
  for i, row in ipairs(all) do
    print(i, row.id, row.name, row.created_at)
  end
end

-- 6) Query API examples (chainable; executes on :all/:first/:get/:count)
do
  local alices = User.query:filter_by{ name = "Alice" }:all()
  assert(#alices >= 1, "Expected at least one Alice")
  print("Queried Alice -> id=" .. alices[1].id)

  local bob = User.query:get(2)  -- returns proxy or nil
  if bob then
    print("User.get(2) -> name=" .. bob.name)
  end

  local last = User.query:order_by(User.name:desc()):first()
  if last then
    print("First by name DESC ->", last.id, last.name)
  end
end

-- 7) Updates are queued on the proxy, then applied on db.session_commit()
do
  local u = User.query:filter_by{ name = "Charlie" }:first()
  if u then
    u.name = "Charlene"    -- queued UPDATE
    db.session_commit()    -- apply UPDATE
    print("Updated user id=" .. u.id .. " -> name=" .. (User.query:get(u.id).name))
  end
end

-- 8) Transactions + last_insert_id()
do
  db.begin()
  db.session_add(User.new{ name = "Dave" })
  db.session_commit()                    -- insert happens within transaction
  local new_id = db.last_insert_id()
  db.commit()
  print("Inserted Dave with id=" .. tostring(new_id))
end

-- 9) Delete by id (prepared)
--    Uncomment to try:
-- do
--   local eve = User.query:filter_by{ name = "Eve" }:first()
--   if eve then
--     db.delete("users", eve.id)
--     print("Deleted user id=" .. eve.id)
--   end
-- end

-- Export models + db so the app can require them
return {
  db   = db,
  User = User,
}




)");


    createDir("templates");
    // templates/template.html
    writeFile("templates/template.html", R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>{{ title }}</title>
  <style>
    body {
      margin: 0;
      padding: 0;
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(to bottom right, #1e1e2f, #2c2c3e);
      color: #f2f2f2;
      display: flex;
      flex-direction: column;
      min-height: 100vh;
    }

    header {
      background-color: #2a2a3a;
      padding: 1.5rem 2rem;
      font-size: 1.75rem;
      font-weight: 600;
      border-bottom: 2px solid #444;
      color: #fff;
      text-shadow: 0 1px 3px rgba(0, 0, 0, 0.6);
    }

    main {
      flex-grow: 1;
      padding: 2rem;
    }

    h2 {
      color: #a8d0ff;
      margin-bottom: 1rem;
    }

    footer {
      text-align: center;
      padding: 1rem;
      font-size: 0.85rem;
      background-color: #1b1b2b;
      color: #aaa;
      border-top: 1px solid #333;
    }

    em {
      font-style: normal;
      color: #888;
    }

    .powered {
      margin-top: 0.5rem;
      color: #666;
    }

    a {
      color: #77bbee;
      text-decoration: none;
    }

    a:hover {
      text-decoration: underline;
    }
  </style>
</head>
<body>
  <header>{{ project_name }}</header>
  <main>
    <h2>{{ title }}</h2>
    {{ content }}
  </main>
  <footer>
    <div><em>Rendered at {{ timestamp }}</em></div>
    <div class="powered">Powered by <a href="https://github.com/OusmBlueNinja/Lumenite" target="_blank">Lumenite</a></div>
  </footer>
</body>
</html>


)");

    createDir("db");


    createDir(".lumenite");

    writeFile(".lumenite/db.lua", R"(---@meta
---@module "lumenite.db"
local db = {}

--[[!!
Lumenite DB — Lua API (EmmyLua annotations)
-------------------------------------------
• All row values returned by query/all/select_all are strings (SQLite text) or nil.
• Query methods are chainable and do not execute until :first(), :all(), :get(), or :count().
• :get() and :first() return a *proxy* table; reading fields reads current values, assigning
  (e.g., proxy.name = "X") queues an UPDATE applied on db.session_commit().
• INTEGER PRIMARY KEY columns are recommended for ids (rowid).
• Defaults: when you pass `options.default` to Column(...), CREATE TABLE will include a DEFAULT
  literal (numeric unquoted, strings quoted).
!!]]

---@alias ColumnOptions { primary_key?: boolean, default?: any }

---@class ColumnDef
---@field name           string
---@field type           string
---@field primary_key    boolean
---@field default_value  string  @empty string if unset (stringified literal for DDL)

---@class ColumnHelper
---@field asc  fun(self: ColumnHelper): string  @returns "<col> ASC"
---@field desc fun(self: ColumnHelper): string  @returns "<col> DESC"

---@class QueryTable
---@field filter_by fun(self: QueryTable, filters: { [string]: string|number|boolean|nil }): QueryTable
---@field order_by  fun(self: QueryTable, expr: string): QueryTable
---@field limit     fun(self: QueryTable, n: integer): QueryTable
---@field get       fun(self: QueryTable, id: string|integer): table?    @proxy row or nil
---@field first     fun(self: QueryTable): table?                         @proxy row or nil
---@field all       fun(self: QueryTable):  table[]                       @array of plain row tables
---@field count     fun(self: QueryTable):  integer                       @row count for current filters

---@class ModelTable
---@field new   fun(def: { [string]: any }): table    @creates a new instance (to be inserted)
---@field query QueryTable                            @chainable query builder
---@field [string] ColumnHelper                       @each column name → helper with :asc()/:desc()

---@class DB
---@field open             fun(filename: string):      DB?, string?  @open/create `./db/<filename>`
---@field Column           fun(name: string, type: string, options?: ColumnOptions): ColumnDef
---@field Model            fun(def: { __tablename: string, [string]: ColumnDef }): ModelTable
---@field create_all       fun():                      nil
---@field session_add      fun(row: table):            nil            @stage an INSERT (from Model.new)
---@field session_commit   fun():                      nil            @apply staged INSERTs/UPDATEs
---@field select_all       fun(tablename: string):     table[]        @SELECT * FROM <tablename>
---@field begin            fun():                      nil            @BEGIN transaction
---@field commit           fun():                      nil            @COMMIT transaction
---@field rollback         fun():                      nil            @ROLLBACK transaction
---@field last_insert_id   fun():                      integer        @sqlite3_last_insert_rowid()
---@field delete           fun(tablename: string, id: string|integer): nil  @DELETE FROM <table> WHERE id=?

--- Opens (or creates) a SQLite file under `./db/`.
--- Also ensures `./db` and `./log` folders exist and enables `PRAGMA foreign_keys = ON`.
---@param filename string
---@return DB? db, string? err  -- the DB instance or nil+error
function db.open(filename) end

--- Defines a new column descriptor for use in db.Model.
--- If options.default is numeric, it's emitted unquoted; strings are quoted in DDL.
---@param name string
---@param type string
---@param options? ColumnOptions
---@return ColumnDef
function db.Column(name, type, options) end

--- Defines a new model/table. Example:
--- local User = db.Model{ __tablename="users", id=db.Column("id","INTEGER",{primary_key=true}) }
---@param def { __tablename: string, [string]: ColumnDef }
---@return ModelTable
function db.Model(def) end

--- Creates all registered tables with CREATE TABLE IF NOT EXISTS.
function db.create_all() end

--- Stage a row for insertion (from Model.new{...}). Applied on db.session_commit().
---@param row table
function db.session_add(row) end

--- Apply all staged INSERTs and queued UPDATEs (from proxy assignments).
function db.session_commit() end

--- Values are strings or nil.
---@param tablename string
---@return table[]
function db.select_all(tablename) end

--- BEGIN a transaction.
function db.begin() end

--- COMMIT the current transaction.
function db.commit() end

--- ROLLBACK the current transaction.
function db.rollback() end

--- Returns sqlite3_last_insert_rowid() of the current connection.
---@return integer
function db.last_insert_id() end

---@param tablename string
---@param id string|integer
function db.delete(tablename, id) end

return db


    )");

    writeFile(".lumenite/__syntax__.lua", R"(
---@meta

--[[----------------------------------------------------------------------------
  This file provides IntelliSense and type annotations for the Lumenite web framework.

  DO NOT EDIT THIS FILE MANUALLY.
  It is automatically generated and used by Lua language servers (such as EmmyLua / LuaLS)
  to enable autocompletion, documentation, and static type checking in Lumenite-based apps.

  Any manual changes will be overwritten during regeneration or update.
------------------------------------------------------------------------------]]

---@alias Headers table<string, string>
---@alias RouteHandler fun(req: Request, ...: string): string|Response|table



---@class SendFileOptions
---@field as_attachment? boolean
---@field download_name? string
---@field content_type? string
---@field status? integer
---@field headers? Headers

---@class Request
---@field method string
---@field path string
---@field headers Headers
---@field query table<string, string|string[]>
---@field form table<string, string|string[]>
---@field body string
---@field remote_ip string

---@class Response
---@field status integer
---@field headers Headers
---@field body string

---@class App
local app = {}

---@param path string
---@param handler RouteHandler
function app:get(path, handler) end

---@param path string
---@param handler RouteHandler
function app:post(path, handler) end

---@param path string
---@param handler RouteHandler
function app:put(path, handler) end

---@param path string
---@param handler RouteHandler
function app:delete(path, handler) end

---@param key string
---@return string
function app.session_get(key) end

---@param key string
---@param value string
function app.session_set(key, value) end

---@param name string
---@param fn fun(input: string): string
function app:template_filter(name, fn) end

---@param filename string
---@param context table
---@return string
function app.render_template(filename, context) end

---@param template_string string
---@param context table
---@return string
function app.render_template_string(template_string, context) end

---@param path string
---@param options? SendFileOptions
---@return Response
function app.send_file(path, options) end

---@param table table
---@return Response
function app.jsonify(table) end

---@param json string
---@return table
function app.json(json) end

---@param json string
---@return table
function app.from_json(json) end

---@param fn fun(req: Request): Response|nil
function app.before_request(fn) end

---@param fn fun(req: Request, res: Response): Response|nil
function app.after_request(fn) end

---@param url string
---@return table
function app.http_get(url) end

---@overload fun(status: integer)
---@param status integer
---@param message? string
function app.abort(status, message) end

---@param port integer
function app:listen(port) end

---@type App
_G.app = app

return app
    )");


    createDir("log");
    writeFile("log/latest.log", "Hello, World!");
    createDir("vendor");
    createDir("static");
    createDir("static/javascript");
    writeFile("static/javascript/index.js", R"()");
    createDir("static/styles");
    writeFile("static/styles/style.css", R"()");

    createDir("plugins");
    writeFile("plugins/modules.cpl", R"(# Lumenite Plugins
plugins: []
)");


    writeFile("app.lua", R"(-- app.lua

--[[
   Lumenite Entry Point

   This is your main application bootstrap file.
   It loads route handlers, middleware, filters, and models.

   Each file in the `app/` folder encapsulates a part of your app:
     - filters.lua     → defines custom template filters
     - middleware.lua  → defines pre- and post-request logic
     - routes.lua      → defines HTTP route handlers
     - models.lua      → defines database models (ORM)

   You can customize the port or add environment setup here.
   This file is the first thing run by the Lumenite engine.
--]]

require("app.models")
require("app.filters")
require("app.middleware")

require("app.routes.web")
require("app.routes.api")

app:listen(8080)

)");


    // README.md
    writeFile("README.md", "# " + name + "\n\nMade by [Lumenite](https://github.com/OusmBlueNinja/Lumenite)");

    // .gitignore
    writeFile(".gitignore", R"(
*.db
*.log
.vscode/
build/
)");

    createDir(".vscode");
    writeFile(".vscode/settings.json", R"({
  "files.associations": {
    "*.cpl": "yaml",
    "*.luma": "yaml",
    "*.lma": "yaml",
    "*.payload": "yaml",
    "*.pyld": "yaml",
    "*.pld": "yaml"
  },
  "vsicons.associations.folders": [
    {
      "icon": "config",
      "extensions": [
        "lumenite"
      ],
      "format": "svg"
    }
  ]
}
)");


    log("Created", "Lumenite workspace: " + name);
}
