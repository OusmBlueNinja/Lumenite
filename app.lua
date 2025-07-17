

app:get("/", function(req)
    print(app.session_get("id"))
    return app.render_template("index.html", {})
end)

app:get("/update/<argument>", function(req, argument)
    app.session_set("id", argument)

    return app.render_template("index.html", {})
end)

app:listen(8080)
