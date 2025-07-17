app:get("/", function(req)
    return app.render_template("index.html", { id = app.session_get("id") })
end)

app:get("/update/<argument>", function(req, argument)
    app.session_set("id", argument)

    return app.render_template("index.html", {})
end)

app:listen(8080)
