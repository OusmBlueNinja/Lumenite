-- app/routes/api.lua
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
    return {
        status = "ok",
        time = os.date("!%Y-%m-%d %H:%M:%S UTC")
    }
end)
