local db = require("LumeniteDB") -- This is a builtin module inside of Luminite.




-- Open or create DB
if not db:open("users.db") then
    error("Failed to open DB: " .. db:error())
end

-- Create users table
local ok = db:exec([[
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password TEXT NOT NULL
)
]])
if not ok then
    error("Table create failed: " .. db:error())
end

-- POST /register
app:post("/register", function(req)
    local data = app.from_json(req.body)
    local u = data.username
    local p = data.password

    if not u or not p then
        return { status = 400, body = "Missing fields" }
    end

    local sql = string.format(
            "INSERT INTO users (username, password) VALUES (%s, %s)",
            db:sanitize(u), db:sanitize(p)
    )
    if not db:exec(sql) then
        return { status = 400, body = db:error() }
    end

    return { status = 200, body = "User registered." }
end)

-- POST /login
app:post("/login", function(req)
    local data = app.from_json(req.body)
    local u = data.username
    local p = data.password

    local sql = string.format(
            "SELECT * FROM users WHERE username = %s AND password = %s",
            db:sanitize(u), db:sanitize(p)
    )
    local rows = db:query(sql)

    if #rows > 0 then
        return app.jsonify({ success = true, user = rows[1] })
    else
        return app.jsonify({ success = false, message = "Invalid login" })
    end
end)

-- GET /users (debug)
app:get("/users", function()
    local rows = db:query("SELECT id, username FROM users")
    return app.jsonify(rows)
end)

app:get("/", function()
    local html = [[
<!DOCTYPE html>
<html>
<head>
    <title>Lumenite Users</title>
    <meta charset="utf-8">
    <style>
        body { font-family: sans-serif; margin: 2rem; }
        table, th, td {
            border: 1px solid #ccc;
            border-collapse: collapse;
            padding: 0.5rem;
        }
    </style>
</head>
<body>
    <h1>Registered Users</h1>
    <table id="userTable">
        <thead>
            <tr><th>ID</th><th>Username</th></tr>
        </thead>
        <tbody></tbody>
    </table>

    <script>
        fetch("/users")
            .then(res => res.json())
            .then(data => {
                const tbody = document.querySelector("#userTable tbody");
                data.forEach(user => {
                    const row = document.createElement("tr");
                    row.innerHTML = `<td>${user.id}</td><td>${user.username}</td>`;
                    tbody.appendChild(row);
                });
            })
            .catch(err => {
                document.body.innerHTML += "<p style='color:red;'>Failed to load users.</p>";
                console.error(err);
            });
    </script>
</body>
</html>
]]

    return { status = 200, headers = { ["Content-Type"] = "text/html" }, body = html }

end)

app:listen(8080)
