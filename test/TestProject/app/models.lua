-- app/models.lua
local db = require("lumenite.db")

--[[
   Model definitions for Lumenite.
   Use this file to register and configure your application's database models.

   • db.open(filename)            - Open (or create) the SQLite database under ./db/
   • db.Column(name, type, opts)  - Declare a column (opts.primary_key = true)
   • db.Model{…}                  - Define a new model/table
   • db.create_all()              - Create all tables you've defined
   • model.new(data)              - Instantiate a row for insertion
   • model.query                   - Built‑in query API with methods:
       • :get(id)       - Fetch a single row by primary key
       • :all()         - Fetch all matching rows
       • :first()       - Fetch the first matching row
       • :filter_by{…}  - Filter rows by a set of conditions
       • :order_by(expr)- Order by a set of conditions
   • db.session_add(row)          - Stage a row for insertion
   • db.session_commit()          - Commit all staged inserts
--]]



local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

local User = db.Model {
   __tablename = "users",
   id = db.Column("id", "INTEGER", { primary_key = true }),
   name = db.Column("name", "TEXT"),
   created_at = db.Column("created_at", "TEXT", { default = os.time })
}

-- 3) create the table
db.create_all()
