-- app/routes/web.lua
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
        project_name = "TestProject",
        content = "<p>This content was injected into the layout.</p>",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC")
    })
end)
