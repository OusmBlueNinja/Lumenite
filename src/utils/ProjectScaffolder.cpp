//
// Created by spenc on 7/21/2025.
//

#include "ProjectScaffolder.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

#define MSG_OK "[+] "

static void writeFile(const fs::path &path, const std::string &content)
{
    if (fs::exists(path)) {
        std::cout << "[!] Skipped (already exists): \"" << path.string() << "\"\n";
    } else {
        std::ofstream file(path);
        if (file) file << content;
        std::cout << MSG_OK << "Wrote: " << path << "\n";
    }
}

static void createDir(const fs::path &path)
{
    fs::create_directories(path);

    std::cout << MSG_OK << "Created: " << path << "\n";
}

void ProjectScaffolder::createWorkspace(const std::string &name)
{
    fs::path root = fs::current_path();

    std::cout << "[*] Initializing project in: " << root.string() << "\n";

    // Create directories
    createDir(root / "templates");
    createDir(root / ".lumenite");
    createDir(root / ".vscode");

    // app.lua
    writeFile(root / "app.lua", R"(-- app.lua
local safe = require("LumeniteSafe")

app:get("/", function(request)
    return app.render_template("index.html", {
        title = "Welcome to Lumenite!",
        message = "This page is rendered using a template.",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC")
    })
end)

app.after_request(function(request, response)
    response.headers["X-Powered-By"] = "Lumenite"
    return response
end)

app:template_filter("safe", function(input)
    return safe.escape(input)
end)


app:listen(8080)
)");

    // templates/index.html
    writeFile(root / "templates" / "index.html", R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>{{ title }}</title>
    <style>
        body { font-family: sans-serif; padding: 2rem; background: #f9f9f9; }
        h1 { color: #333; }
        p  { color: #555; }
    </style>
</head>
<body>
    <h1>{{ title }}</h1>
    <p>{{ message }}</p>
    <p><em>Viewed at {{ timestamp }}</em></p>
</body>
</html>
)");

    // types/__syntax__.lua
    writeFile(root / ".lumenite" / "__syntax__.lua", R"(---@meta

-- This file provides IntelliSense annotations for the Lumenite web framework.
-- Do not edit manually — it is generated automatically and used by the language server.

---@class Request
---@field method string
---@field path string
---@field headers table<string, string>
---@field query table<string, string|string[]>
---@field form table<string, string|string[]>
---@field body string
---@field remote_ip string

---@class Response
---@field status integer
---@field headers table<string, string>
---@field body string

---@class App
local app = {}

---@param path string
---@param handler fun(req: Request): string|Response|table
function app:get(path, handler) end

---@param path string
---@param handler fun(req: Request): string|Response|table
function app:post(path, handler) end

---@param path string
---@param handler fun(req: Request): string|Response|table
function app:put(path, handler) end

---@param path string
---@param handler fun(req: Request): string|Response|table
function app:delete(path, handler) end

---@param key string
---@return string
function app.session_get(key) return "" end

---@param key string
---@param value string
function app.session_set(key, value) end

---@param name string
---@param fn fun(input: string): string
function app:template_filter(name, fn) end

---@param filename string
---@param context table
---@return string
function app.render_template(filename, context) return "" end

---@param template_string string
---@param context table
---@return string
function app.render_template_string(template_string, context) return "" end

---@param table table
---@return Response
function app.jsonify(table) return {} end

---@param json string
---@return table
function app.json(json) return {} end

---@param json string
---@return table
function app.from_json(json) return {} end

---@param fn fun(req: Request): Response|nil
function app.before_request(fn) end

---@param fn fun(req: Request, res: Response): Response|nil
function app.after_request(fn) end

---@param url string
---@return table
function app.http_get(url) return {} end

---@param port integer
function app:listen(port) end

---@type App
_G.app = app
return app


)");

    // .gitignore
    writeFile(root / ".gitignore", R"(
*.db
*.log
.vscode/
build/
)");

    std::cout << "[+] Created Lumenite workspace: " << name << "\n";
}
