local safe = require("LumeniteSafe")


-- hello.lua
-- Simple Lumenite demo with HTML and JSON responses

-- Route: GET /
-- Serves a friendly HTML page
app:get("/", function()
    return {
        status = 200,
        headers = {
            ["Content-Type"] = "text/html"
        },
        body = [[
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Hello, Lumenite!</title>
    <style>
        body { font-family: sans-serif; padding: 2rem; background: #f9f9f9; }
        h1 { color: #333; }
        p  { color: #555; }
        #timestamp { margin-top: 1rem; font-weight: bold; color: #2a2; }
    </style>
</head>
<body>
    <h1>Hello, Lumenite!</h1>
    <p>This is a minimal web app running on the Lumenite framework.</p>
    <p>Try the <a href="/json">JSON endpoint</a> too.</p>

    <p>Check out the <a href="/template">Template Engine!</a> as well.</p>


    <p id="timestamp">Loading time...</p>

    <script>
        fetch("/json")
            .then(res => res.json())
            .then(data => {
                document.getElementById("timestamp").textContent =
                    "Server time: " + data.timestamp;
            })
            .catch(err => {
                document.getElementById("timestamp").textContent =
                    "Failed to fetch time.";
                console.error(err);
            });
    </script>
</body>
</html>
        ]]
    }
end)

app:get("/template", function(req)
    local users = {
        { id = 1, name = "Alice" },
        { id = 2 },
        { id = 3, name = "Charlie" }
    }


    -- Note: The template engine is not Jinja2, its just a start of a Jinja2 Compatible Engine.
    return app.render_template("template_demo.html", {
        title = "This HTML page is rendered using a Jinja-style template!",
        message = "This HTML page is rendered using a Jinja-style template!",
        timestamp = os.date("!%Y-%m-%d %H:%M:%S UTC"), -- UTC time
        users = users,
        name = "Guest",
        show_example = [[
{% for user in users %}
<li>
    ID: <strong>{{ user.id | default(\"N/A\") }}</strong> —
    Name: <strong>{{ user.name | default(\"N/A\") }}</strong>
</li>
{% endfor %}

]]

    })
end)




-- Route: GET /json
-- Returns a JSON-formatted message
app:get("/json", function()
    return app.jsonify({
        message = "Hello from Lumenite JSON endpoint!",
        status = "ok",
        timestamp = os.date("!%Y-%m-%dT%H:%M:%SZ")
    })
end)

app.before_request(function(req)
    -- print(req.headers["User-Agent"])
end)

app.after_request(function(req, res)
    res.headers["X-Powered-By"] = "Lumenite"
    res.headers["X-Host"] = req.remote_ip

    return res
end)

app:template_filter("greet", function(input)
    return "Hello, " .. input .. "!"
end)

app:template_filter("safe", function(input)
    return safe.escape(input)
end)

app:post("/", function(req)

    return app.jsonify({ req })

end)

app:get("/whoami", function(req)

    return app.jsonify({ req })

end)



-- Start server on port 8080
app:listen(8080)
