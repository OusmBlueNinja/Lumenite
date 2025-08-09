-- app/models.lua
local db = require("lumenite.db")

-- fresh start for dev/testing
local function try_remove(path) pcall(os.remove, path) end
try_remove("db/user.db")
try_remove("log/user.db.log")

-- open/create DB under ./db/
local conn, err = db.open("user.db")
assert(conn, "db.open failed: " .. tostring(err))

-- Model: users
-- Note: keep created_at as INTEGER with default os.time() so CREATE TABLE uses a numeric DEFAULT
local User = db.Model{
   __tablename = "users",
   id          = db.Column("id", "INTEGER", { primary_key = true }), -- INTEGER PRIMARY KEY ⇒ rowid
   name        = db.Column("name", "TEXT"),
   created_at  = db.Column("created_at", "INTEGER", { default = os.time() })
}

-- create tables
db.create_all()

-- seed rows (id will auto-assign because INTEGER PRIMARY KEY)
for _, name in ipairs({ "Alice", "Bob", "Charlie" }) do
   db.session_add(User.new{ name = name })
end
db.session_commit()

-- print all
local function print_rows(tag, rows)
   print(tag)
   for i, r in ipairs(rows) do
      print(i, r.id, r.name, r.created_at)
   end
end

-- select_all sanity
local all = db.select_all("users")
assert(#all == 3, "expected 3 users after seed")
print_rows("All users:", all)

-- filter_by + all()
local alices = User.query:filter_by{ name = "Alice" }:all()
assert(#alices == 1, "expected exactly one Alice")
print("Queried Alice -> id=" .. alices[1].id)

-- get(id) (note: ids come back as strings from SQLite text fetch)
local bob = User.query:get(2)
assert(bob and bob.name == "Bob", "expected Bob at id=2")
print("User.get(2) -> name=" .. bob.name)

-- first() with order_by(desc)
local last = User.query:order_by(User.name:desc()):first()
assert(last ~= nil, "first() returned nil unexpectedly")
print("First by name DESC ->", last.id, last.name)

-- limit() + order_by()
local top2 = User.query:order_by(User.id:asc()):limit(2):all()
assert(#top2 == 2 and top2[1].name == "Alice" and top2[2].name == "Bob", "limit/order_by failed")
print_rows("Top 2 by id asc:", top2)

-- proxy update flow (queues UPDATE; apply on commit)
local c = User.query:get(3)      -- Charlie
assert(c and c.name == "Charlie", "expected Charlie at id=3")
c.name = "Charlene"              -- queued update
db.session_commit()              -- applies
local c2 = User.query:get(3)
assert(c2.name == "Charlene", "proxy update did not persist")
print("Updated id=3 ->", c2.name)

-- get() / first() nil behavior on missing rows
assert(User.query:get(999) == nil, "get(999) should be nil")
assert(User.query:filter_by{ name = "Nobody" }:first() == nil, "first() on empty should be nil")

print("All tests passed!")
