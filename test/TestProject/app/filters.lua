-- app/filters.lua
local safe = require("lumenite.safe")

app:template_filter("safe", function(input)
    return safe.escape(input)
end)
