#include "ProjectScaffolder.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include "Version.h"
namespace fs = std::filesystem;

// Color codes
#define RESET     "\033[0m"
#define BOLD      "\033[1m"
#define DIM       "\033[2m"
#define GREEN     "\033[32m"
#define BLUE      "\033[34m"
#define CYAN      "\033[36m"
#define MAGENTA   "\033[35m"
#define YELLOW    "\033[33m"
#define RED       "\033[31m"
#define GRAY      "\033[38;5;240m"
#define PURPLE    "\033[38;5;99m"
#define LBLUE     "\033[38;5;81m"
#define LGREEN    "\033[38;5;120m"
#define MOON1     "\033[38;5;141m"
#define MOON2     "\033[38;5;135m"
#define MOON4     "\033[38;5;63m"
#define MOON5     "\033[38;5;111m"
#define MOON6     "\033[38;5;250m"

std::string ProjectScaffolder::colorizePath(const std::string &pathStr, const std::string &projectName)
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

        // Decide color based on path type
        if (partStr == projectName) {
            out << CYAN << BOLD << partStr << RESET;
        } else if (fs::exists(absolute) && fs::is_directory(absolute)) {
            out << LBLUE << partStr << RESET; // folder
        } else {
            out << LGREEN << partStr << RESET; // file
        }
    }

    return out.str();
}


void ProjectScaffolder::log(const std::string &action, const std::string &text)
{
    std::string prefix = "[+] ";
    std::string color;

    if (action == "Created") { color = BOLD GREEN; } else if (action == "Wrote") { color = BOLD BLUE; } else if (
        action == "Skipped") {
        color = YELLOW;
        prefix = "[!] ";
    } else if (action == "Warning") {
        color = YELLOW;
        prefix = "[!] ";
    } else if (action == "Note") {
        color = CYAN;
        prefix = "[~] ";
    } else if (action == "Error") {
        color = BOLD RED;
        prefix = "[x] ";
    } else {
        color = RESET;
    }

    std::ostringstream aligned;
    aligned << std::left << std::setw(8) << action;

    std::cout << prefix << color << aligned.str() << ":" << RESET << " ";

    // For path-related actions, use colorized path
    if (action == "Created" || action == "Wrote" || action == "Skipped")
        std::cout << colorizePath(text, projectName);
    else
        std::cout << color << text;

    std::cout << RESET << "\n";
}


void ProjectScaffolder::createDir(const fs::path &relPath)
{
    fs::path full = rootPath / relPath;

    if (fs::exists(full)) {
        if (deleteExisting) {
            std::error_code ec;
            fs::remove_all(full, ec);
            if (!ec) {
                log("Deleted", relPath.string());
                fs::create_directories(full);
                log("Created", relPath.string());
            } else {
                log("Error", "Failed to delete directory: " + ec.message());
            }
            return;
        }

        if (!force) {
            log("Skipped", relPath.string());
            return;
        }
    }

    fs::create_directories(full);
    log("Created", relPath.string());
}

void ProjectScaffolder::writeFile(const fs::path &relPath, const std::string &content)
{
    fs::path fullPath = rootPath / relPath;

    if (fs::exists(fullPath)) {
        if (deleteExisting) {
            std::error_code ec;
            fs::remove(fullPath, ec);
            if (!ec) {
                log("Deleted", relPath.string());
            } else {
                log("Error", "Failed to delete file: " + ec.message());
                return;
            }
        } else if (!force) {
            log("Skipped", relPath.string());
            return;
        }
    }

    std::ofstream file(fullPath);
    if (file) {
        file << content;
        log("Wrote", relPath.string());
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

    std::cout << CYAN << "[*] Initializing Lumenite project in: " << rootPath.string() << RESET << "\n";

    std::ostringstream config;
    config << "project_name: " << projectName << "\n";
    config << "lumenite_version: " LUMENITE_RELEASE_VERSION "\n";

    writeFile("config.luma", config.str());

    // Create main subdirectories
    createDir("app");


    // app/routes.lua
    std::string routesLua = R"(-- app/routes.lua
local crypto = require("lumenite.crypto")
local models = require("app.models")

--[[
   Application Routes

   Define your URL endpoints and route handlers here.
   Routes map incoming requests to specific logic and responses.

   This example defines a simple GET route for the homepage ("/")
   that renders a template with dynamic values.

   You can define more routes using:
     app:get(path, handler)
     app:post(path, handler)
     app:json(path, handler)
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

    size_t pos = routesLua.find("{{project_name}}");
    if (pos != std::string::npos) {
        routesLua.replace(pos, 16, name);
    }
    writeFile("app/routes.lua", routesLua);

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
-- app/models.lua
local db = require("lumenite.db")

--[[
   Model definitions for Lumenite.
   Use this file to register and configure your application's database models.

   • db.open(filename)            - Open (or create) the SQLite database under ./db/
   • db.Column(name, type, opts)  - Declare a column (opts.primary_key = true)
   • db.Model{…}                  - Define a new model/table
   • db.create_all()              - Create all tables you've defined
   • model.new(data)              - Instantiate a row for insertion
   • model.query                   - Built‑in query API with methods:
       • :get(id)       - Fetch a single row by primary key
       • :all()         - Fetch all matching rows
       • :first()       - Fetch the first matching row
       • :filter_by{…}  - Add a WHERE clause
       • :order_by(expr)- Add an ORDER BY clause
   • db.session_add(row)          - Stage a row for insertion
   • db.session_commit()          - Commit all staged inserts
--]]



local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

-- 2) define a simple User model
local User = db.Model {
   __tablename = "users",
   id = db.Column("id", "INTEGER", { primary_key = true }),
   name = db.Column("name", "TEXT"),
   created_at = db.Column("created_at", "TEXT")
}

-- 3) create the table
db.create_all()

-- 4) insert a few rows
for _, name in ipairs { "Alice", "Bob", "Charlie" } do
   local u = User.new { name = name }
   db.session_add(u)
end
db.session_commit()

-- 5) select_all test
local all = db.select_all("users")
print("All users:")
for i, row in ipairs(all) do
   print(i, row.id, row.name)
end

-- 6) query.filter_by + .all()
local alices = User.query:filter_by({ name = "Alice" }):all()
assert(#alices == 1, "Expected exactly one Alice")
print("Queried Alice -> id=" .. alices[1].id)

-- 7) query:get(id)
local bob = User.query:get(2)
assert(bob and bob.name == "Bob", "Expected Bob at id=2")
print("User.get(2) -> name=" .. bob.name)

-- 8) query:first() with order_by
local last = User.query:order_by(User.name:desc()):first()
assert(last, "Last is nil")
print("First by name DESC ->", last.id, last.name)

print("All tests passed!")



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
        font-family: sans-serif;
        padding: 2rem;
        background: #f9f9f9;
      }
      header {
        font-size: 1.5rem;
        font-weight: bold;
        color: #333;
      }
      footer {
        margin-top: 2rem;
        font-size: 0.85rem;
        color: #777;
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
      <em>Rendered at {{ timestamp }}</em>
    </footer>
  </body>
</html>

)");

    createDir("db");    


    createDir(".lumenite");

    writeFile(".lumenite/db.lua", R"(---@meta
---@module "lumenite.db"
local db = {}

---@alias ColumnOptions { primary_key?: boolean }

---@alias ColumnDef { name: string, type: string, primary_key: boolean }

---@class ColumnHelper
---@field asc  fun(self: ColumnHelper): string  @“<col> ASC”
---@field desc fun(self: ColumnHelper): string  @“<col> DESC”

---@class QueryTable
---@field filter_by fun(self: QueryTable, filters: { [string]: string|number }): QueryTable @add a WHERE clause
---@field order_by  fun(self: QueryTable, expr: string):         QueryTable @add an ORDER BY clause
---@field limit     fun(self: QueryTable, n: integer):           QueryTable @limit results
---@field get       fun(self: QueryTable, id: string|integer):   table?      @fetch one by id
---@field first     fun(self: QueryTable):                       table?      @fetch first match
---@field all       fun(self: QueryTable):                       table[]     @fetch all matches

---@class ModelTable
---@field new   fun(def: { [string]: any }): table    @create a new instance
---@field query QueryTable                            @the query API
---@field [string] ColumnHelper                       @each column name → helper with :asc()/:desc()

---@class DB
---@field open           fun(filename: string):      DB?, string?        @open/create `./db/filename`
---@field Column         fun(name: string, type: string, options?: ColumnOptions): ColumnDef
---@field Model          fun(def: { __tablename: string, [string]: ColumnDef }): ModelTable
---@field create_all     fun():                      nil                @CREATE TABLE IF NOT EXISTS …
---@field session_add    fun(row: table):            nil                @stage an insert
---@field session_commit fun():                      nil                @commit staged inserts
---@field select_all     fun(tablename: string):     table[]            @SELECT * FROM tablename

--- Opens (or creates) a SQLite file under `./db/`
---@param filename string
---@return DB?, string?       — the DB instance or nil+error
function db.open(filename) end

--- Defines a new column descriptor
---@param name string
---@param type string
---@param options? ColumnOptions
---@return ColumnDef
function db.Column(name, type, options) end

--- Defines a new model
---@param def { __tablename: string, [string]: ColumnDef }
---@return ModelTable
function db.Model(def) end

--- Creates all defined tables
function db.create_all() end

--- Stage a row for insertion
---@param row table
function db.session_add(row) end

--- Commit all staged inserts
function db.session_commit() end

--- Select * from a table
---@param tablename string
---@return table[]
function db.select_all(tablename) end

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
require("app.routes")

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
