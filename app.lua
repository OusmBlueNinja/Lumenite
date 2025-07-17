-- local messages = {}

app.get("/", function(req)
    return app.render_template("index.html", {})
end)


-- app.get("/api/messages", function(req)
--     local after = tonumber(req.query.after) or 0
--     local out = {}
--     for _, m in ipairs(messages) do
--         if m.ts > after then
--             table.insert(out, m)
--         end
--     end
--     return app.json({ messages = out })
-- end)
--
-- app.post("/api/messages", function(req)
--     local body = req.json or {}
--     local user = body.user or "Anonymous"
--     local text = body.text or ""
--     local msg = {
--         user = user,
--         text = text,
--         ts   = math.floor(os.time() * 1000)
--     }
--     table.insert(messages, msg)
--     return app.json({ ok = true })
-- end)


app.listen(8080)
