-- app.lua

local messages = {}

app.get("/", function(req)
    return app.render_template("index.html", {})
end)


-- Return messages newer than ?after=<timestamp>
app.get("/api/messages", function(req)
    local after = tonumber(req.query.after) or 0
    local out = {}
    for _, m in ipairs(messages) do
        if m.ts > after then
            table.insert(out, m)
        end
    end
    return app.json({ messages = out })
end)

-- Post a new message as JSON { user=..., text=... }
app.post("/api/messages", function(req)
    local body = req.json or {}
    local user = body.user or "Anonymous"
    local text = body.text or ""
    local msg = {
        user = user,
        text = text,
        ts   = math.floor(os.time() * 1000)  -- milliseconds
    }
    table.insert(messages, msg)
    return app.json({ ok = true })
end)


app.get("/count", function(req)
    local sum = 0
    for i = 1,1000000 do sum = sum + 1 end
    return app.json({ total = sum })
end)


-- Start the server on port 8080 (or change to your desired port)
app.listen(8080)
