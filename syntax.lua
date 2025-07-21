---@meta
-- Lumenite Framework IntelliSense
-- This file provides type hints and descriptions for IDEs (Lua LS / Sumneko / ZeroBrane)
-- DO NOT require() this file during runtime!

--==============================
--== Request & Response Types ==
--==============================

---@class Request
---@field method string    @HTTP method (GET, POST, etc.)
---@field path string      @URL path of the request (e.g. "/users")
---@field body string      @Raw body content, usually from POST/PUT
---@field headers table<string, string> @HTTP request headers
---@field query table<string, string>   @Parsed query string parameters

---@class Response
---@field status integer                @HTTP status code (e.g. 200, 404)
---@field headers table<string, string> @Response headers
---@field body string                  @Body content to return to client


--===============
--== App Class ==
--===============

---@class App
local app = {}

-- ROUTING ---------------------

---Register a GET route handler
---@param path string                @The path to match (e.g. "/")
---@param handler fun(req: Request): string|Response|table @Function to handle the request
function app:get(path, handler)
end

---Register a POST route handler
---@param path string
---@param handler fun(req: Request): string|Response|table
function app:post(path, handler)
end

---Register a PUT route handler
---@param path string
---@param handler fun(req: Request): string|Response|table
function app:put(path, handler)
end

---Register a DELETE route handler
---@param path string
---@param handler fun(req: Request): string|Response|table
function app:delete(path, handler)
end


-- SESSION ---------------------

---Get a session variable (string value)
---@param key string      @Session key
---@return string         @Stored value or empty string if not set
function app:session_get(key)
    return ""
end

---Set a session variable (string value)
---@param key string      @Session key
---@param value string    @Value to store
function app:session_set(key, value)
end


-- TEMPLATE RENDERING ----------

---Render a template file with variables
---@param filename string           @Template filename (e.g. "home.html")
---@param context table             @Context variables (table of key-value pairs)
---@return string                   @Rendered HTML output
function app:render_template(filename, context)
    return ""
end

---Render a string as a template with context
---@param template_string string    @Raw template string (Jinja-style)
---@param context table             @Context variables
---@return string                   @Rendered result
function app:render_template_string(template_string, context)
    return ""
end


-- TEMPLATE FILTERS -------------

---Register a custom template filter function
---@param name string               @Filter name used in templates (e.g. "upper")
---@param fn fun(input: string): string @Filter function applied to template variables
function app:template_filter(name, fn)
end


-- JSON UTILITIES ---------------

---Convert a Lua table into a JSON HTTP response
---@param table table               @Lua table to encode as JSON
---@return Response                 @JSON response with headers/body/status
function app:jsonify(table)
    return {}
end

---Parse a JSON string into a Lua table
---@param json string               @JSON string to parse
---@return table                    @Parsed Lua table
function app:json(json)
    return {}
end

---Alias for `json`; parses JSON into Lua table
---@param json string
---@return table
function app:from_json(json)
    return {}
end


-- REQUEST HOOKS ----------------

---Run a function before each request (can intercept)
---@param fn fun(req: Request): Response|nil @Function to run before routing (may return a response to skip routing)
function app:before_request(fn)
end

---Run a function after each request is handled
---@param fn fun(req: Request, res: Response): Response|nil @Function to post-process the final response
function app:after_request(fn)
end


-- HTTP CLIENT ------------------

---Make an HTTP GET request from Lua
---@param url string               @The target URL to request
---@return table                   @Table with `status`, `body`, and optional `error`
function app:http_get(url)
    return {}
end


-- SERVER START -----------------

---Start the HTTP server on a given port
---@param port integer             @Port number to bind to (e.g. 8080)
function app:listen(port)
end


-- GLOBAL INJECTION -------------

---@type App
_G.app = app
return app
