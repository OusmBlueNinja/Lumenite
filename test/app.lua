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


app:get("/test-download", function()
    return app.send_file("./adsd.lua", {
        as_attachment = false,
        download_name = "example-download.txt",
        content_type = "text/plain",
        status = 200,
        headers = {
            ["X-From-Test"] = "true",
            ["Cache-Control"] = "no-store"
        }
    })
end)


app:listen(8080)
