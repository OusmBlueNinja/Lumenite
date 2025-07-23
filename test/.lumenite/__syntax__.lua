

---@meta

-- This file provides IntelliSense annotations for the Lumenite web framework.
-- Do not edit manually — it is generated automatically and used by the language server.


---@alias Headers table<string, string>

---@class SendFileOptions
---@field as_attachment boolean   @Optional
---@field download_name string    @Optional
---@field content_type  string    @Optional
---@field status        integer   @Optional
---@field headers       Headers   @Optional



---@class Request
---@field method string                     -- HTTP method (GET, POST, etc.)
---@field path string                       -- Request path (e.g. "/api/data")
---@field headers Headers                   -- All HTTP headers as a key-value table
---@field query table<string, string|string[]> -- Query parameters (?key=val&key=val2)
---@field form table<string, string|string[]>  -- POST/PUT form fields
---@field body string                       -- Raw request body
---@field remote_ip string                  -- IP address of the client

---@class Response
---@field status integer                    -- HTTP status code (e.g. 200, 404)
---@field headers Headers                   -- Response headers
---@field body string                       -- Response body as a string

---@class App
local app = {}

--- Registers a handler for HTTP GET requests
--- @param path string                      -- Route path (e.g. "/about")
--- @param handler fun(req: Request): string|Response|table
function app:get(path, handler) end

--- Registers a handler for HTTP POST requests
--- @param path string
--- @param handler fun(req: Request): string|Response|table
function app:post(path, handler) end

--- Registers a handler for HTTP PUT requests
--- @param path string
--- @param handler fun(req: Request): string|Response|table
function app:put(path, handler) end

--- Registers a handler for HTTP DELETE requests
--- @param path string
--- @param handler fun(req: Request): string|Response|table
function app:delete(path, handler) end

--- Retrieves a session variable by key
--- @param key string
--- @return string                          -- Value from the user's session
function app.session_get(key) return "" end

--- Sets a session variable (persists across requests)
--- @param key string
--- @param value string
function app.session_set(key, value) end

--- Registers a custom template filter callable from within templates
--- @param name string                      -- Name used in templates (e.g. {{ val|upper }})
--- @param fn fun(input: string): string    -- Filter function (string in → string out)
function app:template_filter(name, fn) end

--- Renders a template file with context variables
--- @param filename string                 -- Path to template file (e.g. "index.html")
--- @param context table                   -- Table of variables to inject
--- @return string                         -- Rendered HTML
function app.render_template(filename, context) return "" end

--- Renders a raw template string with context
--- @param template_string string
--- @param context table
--- @return string
function app.render_template_string(template_string, context) return "" end

--- Sends a file to the client.
--- Returns a file as HTTP response with optional headers, download flags, and MIME type detection.
--- @param path string
--- @param options? SendFileOptions
--- @return Response
function app.send_file(path, options) end

--- Converts a Lua table to a JSON HTTP response
--- Automatically sets Content-Type and status = 200.
--- @param table table
--- @return Response
function app.jsonify(table) return {} end

--- Parses a JSON string into a Lua table
--- @param json string
--- @return table
function app.json(json) return {} end

--- Parses a JSON string into a Lua table (alias of `json`)
--- @param json string
--- @return table
function app.from_json(json) return {} end

--- Registers a function that runs before every request
--- Used for middleware such as logging, auth checks, etc.
--- @param fn fun(req: Request): Response|nil
function app.before_request(fn) end

--- Registers a function that runs after a response is generated
--- Used to modify response before it is sent to the client.
--- @param fn fun(req: Request, res: Response): Response|nil
function app.after_request(fn) end

--- Performs an internal HTTP GET request (e.g. to an external API)
--- @param url string
--- @return table                          -- Parsed JSON or raw response depending on content
function app.http_get(url) return {} end

--- Starts the Lumenite web server on the specified port
--- @param port integer
function app:listen(port) end

---@type App
_G.app = app
return app


