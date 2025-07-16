local messages = {}

local function get_time()
    return os.date("%H:%M:%S")
end

local function escape_html(str)
    return (str:gsub("[<>&\"]", {
        ["<"] = "&lt;",
        [">"] = "&gt;",
        ["&"] = "&amp;",
        ["\""] = "&quot;"
    }))
end

app.get("/", function(req)
    return app.render_template("index.html", {})
end)

app.get("/api/messages", function(req)
    return app.json(messages)
end)

app.get("/api/send", function(req)
    local msg = req.query["msg"] or ""
    msg = escape_html(msg)
    if msg ~= "" then
        table.insert(messages, { msg = msg, time = get_time() })
    end
    return "OK"
end)

app.listen(8080)
