-- app/models.lua
local db = require("lumenite.db")

--[[
   Model definitions for Lumenite.
   Use this file to register and configure your application’s database models.

   • db.open(filename)            – Open (or create) the SQLite database under ./db/
   • db.Column(name, type, opts)  – Declare a column (opts.primary_key = true)
   • db.Model{…}                  – Define a new model/table
   • db.create_all()              – Create all tables you’ve defined
   • model.new(data)              – Instantiate a row for insertion
   • model.query                   – Built‑in query API with methods:
       • :get(id)       – Fetch a single row by primary key
       • :all()         – Fetch all matching rows
       • :first()       – Fetch the first matching row
       • :filter_by{…}  – Add a WHERE clause
       • :order_by(expr)– Add an ORDER BY clause
   • db.session_add(row)          – Stage a row for insertion
   • db.session_commit()          – Commit all staged inserts
--]]



local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

-- 2) define a simple User model
local User = db.Model {
   __tablename = "users",
   id = db.Column("id", "INTEGER", { primary_key = true }),
   name = db.Column("name", "TEXT"),
   created_at = db.Column("created_at", "TEXT")
}

-- 3) create the table
db.create_all()

-- 4) insert a few rows
for _, name in ipairs { "Alice", "Bob", "Charlie" } do
   local u = User.new { name = name }
   db.session_add(u)
end
db.session_commit()

-- 5) select_all test
local all = db.select_all("users")
print("All users:")
for i, row in ipairs(all) do
   print(i, row.id, row.name)
end

-- 6) query.filter_by + .all()
local alices = User.query:filter_by({ name = "Alice" }):all()
assert(#alices == 1, "Expected exactly one Alice")
print("Queried Alice -> id=" .. alices[1].id)

-- 7) query:get(id)
local bob = User.query:get(2)
assert(bob and bob.name == "Bob", "Expected Bob at id=2")
print("User.get(2) -> name=" .. bob.name)

-- 8) query:first() with order_by
local last = User.query:order_by(User.name:desc()):first()
assert(last, "Last is nil")
print("First by name DESC ->", last.id, last.name)

print("All tests passed!")
