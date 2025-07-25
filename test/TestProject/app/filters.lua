-- app/filters.lua
local safe = require("lumenite.safe")

--[[
   Template Filters

   This file defines custom filters available in your templates.

   Filters allow you to transform data inside templates:
     Example usage in template.html:
       {{ title | upper }}         -- convert title to uppercase
       {{ content | safe }}        -- mark content as safe HTML

   Defining a filter:
     app:template_filter("name", function(input)
         -- do something with input
         return result
     end)

   This example defines a 'safe' filter using the Lumenite Safe module,
   which escapes HTML to prevent XSS vulnerabilities.

   You can add more filters here, like:
     "truncate", "markdown", "date_format", etc.
--]]



app:template_filter("safe", function(input)
    return safe.escape(input)
end)

