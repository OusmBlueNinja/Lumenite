local db = require("LumeniteDB") -- This is a builtin module inside of Luminite.
local crypto = require("LumeniteCrypto")

function test_crypto()
    local ok = true

    local function check(label, cond)
        if cond then
            print("[+]    " .. label)
        else
            print("[-]    " .. label)
            ok = false
        end
    end

    -- Test cases for hashing & verifying
    local test_passwords = {
        { label = "normal password", pw = "hunter2", should_pass = true },
        { label = "empty password", pw = "", should_pass = true },
        { label = "long password", pw = string.rep("a", 1000), should_pass = true },
        { label = "unicode password", pw = "pässwörd♥", should_pass = true },
        { label = "wrong password match", pw = "wrongpw", ref = "hunter2", should_pass = false },
    }

    for _, case in ipairs(test_passwords) do
        local pw = case.pw
        local ref = case.ref or pw
        local hash = crypto.hash(ref)
        local result = crypto.verify(pw, hash)
        check("crypto.verify " .. case.label, result == case.should_pass)
    end

    -- AES encryption/decryption tests
    local good_key = string.rep("K", 32)
    local bad_key = string.rep("X", 32)
    local short_key = "badkey"
    local payloads = {
        { label = "empty string", data = "" },
        { label = "short string", data = "Hello" },
        { label = "unicode string", data = "Привет мир" },
        { label = "long data", data = string.rep("X", 2048) },
        { label = "binary content", data = string.char(0, 1, 2, 255, 128, 64) },
    }

    for _, case in ipairs(payloads) do
        local encrypted = crypto.encrypt(good_key, case.data)
        local decrypted = crypto.decrypt(good_key, encrypted)
        check("crypto.decrypt " .. case.label, decrypted == case.data)
    end

    -- Decryption with wrong key (should return garbage, not match)
    local encrypted = crypto.encrypt(good_key, "supersecret")
    local bad_result = crypto.decrypt(bad_key, encrypted)
    check("decrypt with wrong key (should not match)", bad_result ~= "supersecret")

    -- Attempt to use a bad key length (should throw)
    local success, err = pcall(function()
        crypto.encrypt(short_key, "data")
    end)
    print(success, err)
    check("encrypt with short key should error", not success and err:match("32 bytes"))

    -- Corrupt ciphertext by altering last bytes
    local valid = crypto.encrypt(good_key, "msg")
    local corrupted = valid:sub(1, -3) .. "\x00\x00"

    local corrupted_ok, corrupted_result = pcall(function()
        return crypto.decrypt(good_key, corrupted)
    end)
    print(corrupted_ok, corrupted_result)
    check("decrypt corrupted data should fail", not corrupted_ok)

    if ok then
        print("[+] All crypto tests passed")
    else
        print("[ :( ] Some crypto tests failed")
    end

    return ok
end



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
