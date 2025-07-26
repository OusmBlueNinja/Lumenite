#!/usr/bin/env lua
local db = require("lumenite.db")
local function test()
   -- ensure a clean slate
   local dbfile = "fulltest.db"
   os.remove("db/" .. dbfile)
   os.remove("./log/" .. dbfile .. ".log")



   -- 1) open
   local conn, err = db.open(dbfile)
   assert(conn, "db.open failed: " .. tostring(err))

   -- 2) define model (with default created_at)
   local User = db.Model {
      __tablename = "users",
      id          = db.Column("id", "INTEGER", { primary_key = true }),
      name        = db.Column("name", "TEXT"),
      created_at  = db.Column("created_at", "TEXT", { default = os.time() })
   }

   -- 3) create table
   db.create_all()

   -- 4) insert three rows
   for _, name in ipairs { "Alice", "Bob", "Charlie" } do
      local u = User.new { name = name }
      db.session_add(u)
   end
   db.session_commit()

   -- 5) raw select_all
   local raw = db.select_all("users")
   assert(#raw == 3, "raw select_all should return 3 rows")
   local ts = raw[1].created_at
   assert(ts:match("^%d+$"), "created_at must be digits")
   assert(raw[2].created_at == ts, "row 2 default must match")
   assert(raw[3].created_at == ts, "row 3 default must match")

   -- 6) all()
   assert(#User.query:all() == 3, "query:all() => 3 rows")

   -- 7) limit
   assert(#User.query:limit(2):all() == 2, "limit(2) => 2 rows")

   -- 8) filter_by + all
   do
      local a = User.query:filter_by({ name = "Alice" }):all()
      assert(#a == 1 and a[1].name == "Alice", "filter_by{name='Alice'}")
   end

   -- 9) get(id)
   do
      local bob = User.query:get(2)
      assert(bob and bob.name == "Bob", "get(2) => Bob")
   end

   -- 10) filter_by + first
   do
      local c = User.query:filter_by({ name = "Charlie" }):first()
      assert(c and c.id == "3", "filter_by{name='Charlie'}:first() => id=3")
   end

   -- 11) order_by ASC / DESC
   do
      local asc  = User.query:order_by(User.name:asc()):first()
      local desc = User.query:order_by(User.name:desc()):first()
      assert(asc and desc, "nil response")
      assert(asc.name == "Alice", "ASC first() must be Alice")
      assert(desc.name == "Charlie", "DESC first() must be Charlie")
   end

   -- 12) update + commit via proxy
   do
      local alice = User.query:get(1)
      assert(alice, "nil response")
      assert(alice.name == "Alice", "pre‑update")
      alice.name = "Alicia"
      db.session_commit()
      local upd = User.query:get(1)
      assert(upd, "nil response")
      assert(upd.name == "Alicia", "post-update must be Alicia, Got: " .. upd.name)
   end

   -- 13) chaining filter_by + order_by + limit
   do
      local subset = User.query
          :filter_by({ created_at = ts })
          :order_by(User.name:desc())
          :limit(1)
          :all()


      assert(#subset == 1, "invalid response length, expected 1, got: " .. #subset) -- this is a know bug, working on it rn.
      assert(subset[1].name == "Charlie", "chain filter/order/limit => Charlie")
   end


   print("✅ All tests passed!")
end


test()
