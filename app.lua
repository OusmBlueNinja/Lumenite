app:get("/", function(req)
    return app.render_template("index.html", { id = app.session_get("id") })
end)

app:get("/admin/<name>/<admin>", function(req, name, admin)


    return app.render_template("index.html", { name = name, admin = admin })


end)


app.template_filter("upper", function(val)
    return string.upper(val)
end)

app:listen(8080)
