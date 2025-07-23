-- app.lua
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


app:get("/test-download/<filepath>", function(req, filepath)
    return app.send_file(filepath, {
        as_attachment = false,
        download_name = filepath,
        content_type = "text/plain",
        status = 200,
        headers = {
            ["X-From-Test"] = "true",
            ["Cache-Control"] = "no-store"

        }
    })
end)



app:get("/test", function()
    app.abort(501, "This is a test error. It works!")
    return "<h1>This is a Big Error :(</h1>"
end)


app:listen(8080)
