-- JSON file path (relative or absolute if needed)
local USERS_FILE = "users.json"

-- Helper to read all users from the file
local function load_users()
    local f = io.open(USERS_FILE, "r")
    if not f then
        return {}
    end

    local content = f:read("*a")
    f:close()

    if content == "" then
        return {}
    end

    local ok, data = pcall(app.json, content)
    if ok and type(data) == "table" then
        return data
    else
        return {}
    end
end

-- Helper to save users back to the file
local function save_users(users)
    local body = app.jsonify(users).body  -- stringify without headers
    local f = io.open(USERS_FILE, "w")
    f:write(body)
    f:close()
end

-- POST /api/users — add a user
app:post("/api/users", function(req)
    local ok, body = pcall(app.json, req.body or "")
    if not ok or type(body) ~= "table" then
        return {
            status = 400,
            body = "Invalid JSON"
        }
    end

    local name = body.name
    local age = body.age

    if type(name) ~= "string" or type(age) ~= "number" then
        return {
            status = 400,
            body = "Expected { name = string, age = number }"
        }
    end

    local users = load_users()
    table.insert(users, { name = name, age = age })
    save_users(users)

    return {
        status = 201,
        body = "User added"
    }
end)

-- GET /api/users — list all users
app:get("/api/users", function(req)
    local users = load_users()
    return app.jsonify(users)
end)

app:get("/", function(req)
    return app.render_template("index.html")
end)



app:listen(8080)