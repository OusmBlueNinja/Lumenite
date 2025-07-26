-- app.lua

--[[
   Lumenite Entry Point

   This is your main application bootstrap file.
   It loads route handlers, middleware, filters, and models.

   Each file in the `app/` folder encapsulates a part of your app:
     - filters.lua     → defines custom template filters
     - middleware.lua  → defines pre- and post-request logic
     - routes.lua      → defines HTTP route handlers
     - models.lua      → defines database models (ORM)

   You can customize the port or add environment setup here.
   This file is the first thing run by the Lumenite engine.
--]]



require("app.models")
require("app.filters")
require("app.middleware")

require("app.routes.web")
require("app.routes.api")

app:listen(8080)
