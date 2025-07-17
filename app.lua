
app.get("/", function(req)
    return app.render_template("index.html", {})
end)

app.listen(8080)
