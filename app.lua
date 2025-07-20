local IP_LOG_FILE = "iplog.json"

-- Load stored logs
local function load_iplog()
    local f = io.open(IP_LOG_FILE, "r")
    if not f then
        return {}
    end
    local content = f:read("*a")
    f:close()
    local ok, data = pcall(app.json, content)
    return ok and type(data) == "table" and data or {}
end

-- Save logs
local function save_iplog(logs)
    local f = io.open(IP_LOG_FILE, "w")
    assert(f, "Failed to open iplog.json for writing")
    f:write(app.jsonify(logs).body)
    f:close()
end

-- Find a cached IP
local function find_ip(logs, ip)
    for _, entry in ipairs(logs) do
        if entry.ip == ip then
            return entry
        end
    end
    return nil
end

-- Turn country code into flag URL
local function flag_url_from_code(code)
    if not code or #code ~= 2 then
        return nil
    end
    return "https://ipgeolocation.io/static/flags/" .. code:lower() .. "_64.png"
end

-- Try multiple APIs (fallback pattern)
local function fetch_ip_data(ip)
    local candidates = {}

    -- API 1: ip-api.com
    table.insert(candidates, function()
        local res = app.http_get("http://ip-api.com/json/" .. ip)
        if not res or res.status ~= 200 then
            return nil
        end
        local ok, data = pcall(app.json, res.body or "")
        if not ok or type(data) ~= "table" then
            return nil
        end
        return {
            source = "ip-api.com",
            ip = data.query,
            country = data.country,
            countryCode = data.countryCode,
            city = data.city,
            region = data.regionName,
            lat = data.lat,
            lon = data.lon
        }
    end)

    -- API 2: ipwho.is
    table.insert(candidates, function()
        local res = app.http_get("http://ipwho.is/" .. ip)
        if not res or res.status ~= 200 then
            return nil
        end
        local ok, data = pcall(app.json, res.body or "")
        if not ok or type(data) ~= "table" or data.success == false then
            return nil
        end
        return {
            source = "ipwho.is",
            ip = data.ip,
            country = data.country,
            countryCode = data.country_code,
            city = data.city,
            region = data.region,
            lat = data.latitude,
            lon = data.longitude
        }
    end)

    -- Try each provider until one succeeds
    for _, try in ipairs(candidates) do
        local data = try()
        if data then
            data.received_at = os.date("!%Y-%m-%dT%H:%M:%SZ")
            data.country_flag = flag_url_from_code(data.countryCode)
            return data
        end
    end

    return nil
end

-- Main route: lookup and cache IP
app:get("/api/iplog", function(req)
    local ok, result = pcall(function()
        local ip = req.query.ip
        if not ip or ip == "" then
            return { status = 400, body = "Missing ?ip= parameter" }
        end

        local logs = load_iplog()
        local cached = find_ip(logs, ip)
        if cached then
            print("[/api/iplog] Cache hit:", ip)
            return app.jsonify(cached)
        end

        local data = fetch_ip_data(ip)
        if not data then
            return { status = 500, body = "All IP providers failed" }
        end

        table.insert(logs, data)
        save_iplog(logs)
        return app.jsonify(data)
    end)

    if not ok then
        print("[Lua Error] /api/iplog failed:", result)
        return { status = 500, body = "Internal server error" }
    end

    return result
end)

-- Return full log
app:get("/api/iplog/all", function(req)
    return app.jsonify(load_iplog())
end)

app:get("/", function()
    return {
        status = 200,
        headers = { ["Content-Type"] = "text/html" },
        body = [[
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>IP Geolocation Viewer</title>
    <style>
        body {
            font-family: sans-serif;
            margin: 2rem;
            background: #f5f5f5;
            color: #222;
        }
        h1 {
            color: #444;
        }
        form {
            margin-bottom: 1rem;
        }
        input[type="text"] {
            padding: 0.5rem;
            font-size: 1rem;
            width: 250px;
        }
        button {
            padding: 0.5rem 1rem;
            font-size: 1rem;
        }
        #result, #all {
            margin-top: 1.5rem;
            background: #fff;
            border-radius: 8px;
            padding: 1rem;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        .flag {
            height: 32px;
            vertical-align: middle;
            margin-left: 8px;
        }
        table {
            border-collapse: collapse;
            width: 100%;
            margin-top: 1rem;
        }
        th, td {
            padding: 0.5rem;
            border: 1px solid #ddd;
            text-align: left;
        }
    </style>
</head>
<body>
    <h1>🌍 IP Geolocation Lookup</h1>

    <form onsubmit="lookup(event)">
        <input type="text" id="ip" placeholder="Enter IP (e.g. 8.8.8.8)" required>
        <button>Lookup</button>
    </form>

    <div id="result"></div>

    <h2>🗂️ Cached IPs</h2>
    <div id="all">Loading...</div>

    <script>
        async function lookup(event) {
            event.preventDefault();
            const ip = document.getElementById("ip").value.trim();
            const res = await fetch("/api/iplog?ip=" + encodeURIComponent(ip));
            const data = await res.json();
            renderResult(data);
            loadAll(); // refresh list
        }

        function renderResult(data) {
            const div = document.getElementById("result");
            div.innerHTML = `
                <h2>📍 Location Info</h2>
                <p><strong>IP:</strong> ${data.ip}</p>
                <p><strong>Country:</strong> ${data.country} (${data.countryCode})
                    ${data.country_flag ? `<img src="${data.country_flag}" class="flag">` : ""}
                </p>
                <p><strong>City:</strong> ${data.city || '-'} | <strong>Region:</strong> ${data.region || '-'}</p>
                <p><strong>Lat, Lon:</strong> ${data.lat}, ${data.lon}</p>
                <p><strong>Source:</strong> ${data.source} | <strong>Time:</strong> ${data.received_at}</p>
            `;
        }

        async function loadAll() {
            const res = await fetch("/api/iplog/all");
            const list = await res.json();
            const div = document.getElementById("all");

            if (list.length === 0) {
                div.innerHTML = "<p>No cached IPs yet.</p>";
                return;
            }

            let rows = list.map(d => `
                <tr>
                    <td>${d.ip}</td>
                    <td>${d.country || "?"} (${d.countryCode || ""})
                        ${d.country_flag ? `<img src="${d.country_flag}" class="flag">` : ""}
                    </td>
                    <td>${d.city || "-"}</td>
                    <td>${d.region || "-"}</td>
                    <td>${d.lat || "?"}, ${d.lon || "?"}</td>
                    <td>${d.source}</td>
                    <td>${d.received_at}</td>
                </tr>
            `).join("");

            div.innerHTML = `
                <table>
                    <thead>
                        <tr>
                            <th>IP</th>
                            <th>Country</th>
                            <th>City</th>
                            <th>Region</th>
                            <th>Coords</th>
                            <th>Source</th>
                            <th>Time</th>
                        </tr>
                    </thead>
                    <tbody>${rows}</tbody>
                </table>
            `;
        }

        loadAll(); // initial load
    </script>
</body>
</html>
        ]]
    }
end)

app.before_request(function(req)
    print(req.headers["User-Agent"])
end)

app.after_request(function(req, res)
    res.headers["X-Powered-By"] = "Lumenite"
    return res
end)




-- Start the server
app:listen(8080)
