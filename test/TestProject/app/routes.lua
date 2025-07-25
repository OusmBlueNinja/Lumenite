-- app/routes.lua
app:get("/", function(request)
    return app.render_template("template.html", {
        title = "Welcome to Lumenite",
        project_name = "TestProject",
        content = "<p>This content was injected into the layout.</p>",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC")
    })
end)
