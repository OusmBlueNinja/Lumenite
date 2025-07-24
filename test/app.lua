local db = require("lumenite.db")
local debug = require("debug_lua")

db.open("unit_test.db")

--debug.print_table(db)

-- Define model
local User = db:Model({
    __tablename = "users",
    id = db.Column("id", "INTEGER", { primary_key = true }),
    name = db.Column("name", "TEXT"),
    age = db.Column("age", "INTEGER"),
    created_at = db.Column("created_at", "TEXT")
})





-- Create table
db.create_all()


--debug.print_table(User)

-- Insert test data
local u1 = User.new({ name = "Alice", age = 30, created_at = "2025-01-01" })
local u2 = User.new({ name = "Bob", age = 25, created_at = "2025-01-02" })
local u3 = User.new({ name = "Charlie", age = 28, created_at = "2025-01-03" })

debug.print_table(u1)


db.session_add(u1)
db.session_add(u2)
db.session_add(u3)
db.session_commit()

-- Verifies .get()
local g = User.query.get(2)
assert(g.name == "Bob", "Expected name 'Bob' for ID 2")

-- Verifies .all()
local all = User.query.all()
assert(#all == 3, "Expected 3 users in total")

-- Verifies .order_by()
local ordered = User.query
    .order_by(User.created_at.desc())
    .all()
assert(ordered[1].name == "Charlie", "Expected most recent user to be Charlie")

-- Verifies .limit()
local limited = User.query
    .order_by(User.id.asc())
    .limit(2)
    .all()
assert(#limited == 2 and limited[1].name == "Alice", "Expected limit 2 starting from Alice")

-- Verifies .filter_by()
local filtered = User.query
    .filter_by({ name = "Alice" })
    .first()
assert(filtered ~= nil and filtered.name == "Alice", "Expected filter_by name=Alice to return Alice")

-- Verifies .first()
local first = User.query
    .order_by(User.age.asc())
    .first()
assert(first.name == "Bob", "Expected youngest user to be Bob")

-- Print all results if no assertion fails
print("[PASS] All ORM database tests passed.")
