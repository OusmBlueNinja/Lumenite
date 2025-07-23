local db = require("lumenite.db")
local conn = db.open("test.sqlite")


---@param tbl table
---@param indent string?
---@param visited table?
function print_table(tbl, indent, visited)
    indent = indent or "  "
    visited = visited or {}

    if visited[tbl] then
        print(indent .. "*<circular reference>*")
        return
    end
    visited[tbl] = true

    for k, v in pairs(tbl) do
        local keyStr = tostring(k)
        io.write(indent .. "[" .. keyStr .. "] = ")

        if type(v) == "table" then
            print("{")
            print_table(v, indent .. "  ", visited)
            print(indent .. "}")
        elseif type(v) == "function" then
            print("<function>")
        elseif type(v) == "userdata" then
            print("<userdata>")
        elseif type(v) == "thread" then
            print("<thread>")
        else
            print(tostring(v))
        end
    end
end

local User = db.Model({
    __tablename = "users",
    id = db.Column("id", "INTEGER", { primary_key = true }),
    name = db.Column("name", "TEXT"),
    created_at = db.Column("created_at", "TEXT")
})

db.create_all()
print(print_table(_G))


local u = User.new({ id = 1, name = "Alice", created_at = "2025-01-01" })

print(print_table(u))
print(print_table(db))

db.session_add(u)
db.session_commit()

local users = db.select_all("users")
for i, user in ipairs(users) do
    print(string.format("User #%d: id=%s, name=%s, created_at=%s", i, user.id, user.name, user.created_at))
end
