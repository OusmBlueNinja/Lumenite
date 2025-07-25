-- Try to guess the variable name that refers to `val` in the caller's scope
local function guess_variable_name(val)
    local level = 3 -- skip this function and print_table
    local i = 1
    while true do
        local name, v = debug.getlocal(level, i)
        if not name then break end
        if v == val then
            return name
        end
        i = i + 1
    end
    return "<unknown>"
end
local debug = {}
-- Recursive table printer with guessed name
function debug.print_table(tbl, indent, visited, name)
    indent = indent or ""
    visited = visited or {}

    name = name or guess_variable_name(tbl) or "<table>"
    print(indent .. name .. " = {")

    if visited[tbl] then
        print(indent .. "  *<circular reference>*")
        print(indent .. "}")
        return
    end
    visited[tbl] = true

    for k, v in pairs(tbl) do
        local keyStr = tostring(k)
        io.write(indent .. "  " .. keyStr .. " = ")

        if type(v) == "table" then
            print("{")
            debug.print_table(v, indent .. "    ", visited)
            print(indent .. "  }")
        elseif type(v) == "function" then
            print("function")
        elseif type(v) == "userdata" then
            print("userdata")
        elseif type(v) == "thread" then
            print("thread")
        else
            print(tostring(v))
        end
    end
    print(indent .. "}")
end

return debug
