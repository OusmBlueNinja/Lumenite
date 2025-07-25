-- app/routes.lua
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
        project_name = "TestProject",
        content = "<p>This content was injected into the layout.</p>",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC")
    })
end)

