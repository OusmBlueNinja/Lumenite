-- app.lua
require("app.filters")
require("app.middleware")
require("app.routes")

app:listen(8080)
