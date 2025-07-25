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
   • db.session_commit()          – Commit all staged inserts and updates
   • db.select_all(tableName)     – Fetch all rows from a raw table
--]]

-- 1) Open (or create) the database file
local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

-- 2) Define a simple User model
local User = db.Model {
   __tablename = "users",
   id          = db.Column("id", "INTEGER", { primary_key = true }),
   name        = db.Column("name", "TEXT"),
   created_at  = db.Column("created_at", "TEXT")
}

-- 3) Create the table
db.create_all()

-- 4) Insert a few rows
for _, name in ipairs { "Alice", "Bob", "Charlie" } do
   local u = User.new { name = name, created_at = os.date("%Y-%m-%d") }
   db.session_add(u)
end
db.session_commit()

local all = db.select_all("users")
print("All users:")
for i, row in ipairs(all) do
   print(i, row.id, row.name)
end

local alices = User.query:filter_by({ name = "Alice" }):all()
assert(#alices == 1, "Expected exactly one Alice")
print("Queried Alice -> id=" .. alices[1].id)

local bob = User.query:get(2)
assert(bob and bob.name == "Bob", "Expected Bob at id=2")
print("User.get(2) -> name=" .. bob.name)

local last = User.query:order_by(User.name:desc()):first()
assert(last, "Expected a non-nil result from first()")
print("First by name DESC ->", last.id, last.name)

local alice = User.query:get(1)
print("Before update ->", alice.id, alice.name)
alice.name = "Alicia"
db.session_commit()
local updated = User.query:get(1)
assert(updated.name == "Alicia", "Expected name='Alicia', got " .. tostring(updated.name))
print("After update  ->", updated.id, updated.name)

print("All tests passed!")
