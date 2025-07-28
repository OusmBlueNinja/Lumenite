-- app/middleware.lua
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


