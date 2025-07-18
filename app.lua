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



-- Pseudo-random data sources
local FIRST_NAMES = { "Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace", "Henry", "Ivy", "Jack", "Liam", "Mia", "Noah", "Olivia", "Peter", "Quinn", "Rose", "Sam", "Tina", "Victor" }
local LAST_NAMES = { "Smith", "Johnson", "Lee", "Brown", "Garcia", "Miller", "Davis", "Martinez", "Taylor", "Anderson", "Thomas", "Moore", "Jackson", "White", "Harris" }
local CITIES = { "New York", "Paris", "Tokyo", "Berlin", "Sydney", "Toronto", "Chicago", "Rome", "Madrid", "Amsterdam" }
local COUNTRIES = { "USA", "France", "Japan", "Germany", "Australia", "Canada", "Italy", "Spain", "Netherlands" }
local STREETS = { "Main", "Oak", "Maple", "Pine", "Elm", "Cedar", "Sunset", "Washington", "Lake", "Hill", "Park" }
local EMAIL_DOMAINS = { "gmail.com", "yahoo.com", "outlook.com", "protonmail.com", "icloud.com", "mail.com" }
local TAGS = { "admin", "member", "guest", "beta", "vip", "trial", "banned", "moderator", "editor", "developer" }
local THEMES = { "light", "dark", "solarized", "dracula" }
local LANGUAGES = { "en", "fr", "de", "es", "jp", "it", "nl" }

-- Random pick helper
local function pick(list)
    return list[math.random(1, #list)]
end

-- Realistic email from name + domain
local function random_email(first, last)
    local username = string.lower(first .. "." .. last)
    return username .. "@" .. pick(EMAIL_DOMAINS)
end

-- Phone number
local function random_phone()
    return string.format("+1-%03d-%03d-%04d", math.random(100,999), math.random(100,999), math.random(1000,9999))
end

-- ISO8601 timestamp within 2 years
local function random_timestamp()
    local now = os.time()
    local offset = math.random(0, 60 * 60 * 24 * 365 * 2)
    return os.date("%Y-%m-%dT%H:%M:%SZ", now - offset)
end

-- Unique tags (1–3)
local function random_tags()
    local count = math.random(1, 3)
    local result, seen = {}, {}
    while #result < count do
        local tag = pick(TAGS)
        if not seen[tag] then
            table.insert(result, tag)
            seen[tag] = true
        end
    end
    return result
end

-- Generate pseudo-random users
local function generate_users(count)
    local users = {}

    for i = 1, count do
        local first = pick(FIRST_NAMES)
        local last = pick(LAST_NAMES)
        local street = pick(STREETS)
        local city = pick(CITIES)
        local country = pick(COUNTRIES)

        table.insert(users, {
            id = i,
            name = first .. " " .. last,
            age = math.random(18, 70),
            email = random_email(first, last),
            city = city,
            country = country,
            phone = random_phone(),
            registered_at = random_timestamp(),
            active = math.random() > 0.2,
            address = {
                street = string.format("%d %s St.", math.random(1, 9999), street),
                zip = string.format("%05d", math.random(10000, 99999))
            },
            tags = random_tags(),
            settings = {
                theme = pick(THEMES),
                email_notifications = math.random() > 0.5,
                language = pick(LANGUAGES)
            }
        })
    end

    return users
end

-- Route: GET /api/random
app:get("/api/random", function(req)
    local count = tonumber(req.query.count) or 10
    local users = generate_users(count)
    return app.jsonify(users)
end)


app:listen(8080)