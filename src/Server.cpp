// Server.cpp

#include "Server.h"
#include "Router.h"
#include "LumeniteApp.h"
#include "SessionManager.h"
#include "ErrorHandler.h"

#include <json/json.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
typedef SOCKET SocketType;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <ifaddrs.h>
  typedef int SocketType;
#endif

static constexpr auto DEFAULT_CONTENT_TYPE = "text/html";


static const std::unordered_map<int, std::string> statusMessages = {
    // 1xx: Informational
    {100, "Continue"},
    {101, "Switching Protocols"},
    {102, "Processing"},
    {103, "Early Hints"},

    // 2xx: Success
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {207, "Multi-Status"},
    {208, "Already Reported"},
    {226, "IM Used"},

    // 3xx: Redirection
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},

    // 4xx: Client Errors
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Payload Too Large"},
    {414, "URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Range Not Satisfiable"},
    {417, "Expectation Failed"},
    {418, "I'm a teapot"},
    {421, "Misdirected Request"},
    {422, "Unprocessable Entity"},
    {423, "Locked"},
    {424, "Failed Dependency"},
    {425, "Too Early"},
    {426, "Upgrade Required"},
    {428, "Precondition Required"},
    {429, "Too Many Requests"},
    {431, "Request Header Fields Too Large"},
    {451, "Unavailable For Legal Reasons"},

    // 5xx: Server Errors
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {506, "Variant Also Negotiates"},
    {507, "Insufficient Storage"},
    {508, "Loop Detected"},
    {510, "Not Extended"},
    {511, "Network Authentication Required"},
};

std::string getColorForStatus(int code)
{
    if (code >= 100 && code < 200) return MAGENTA; // Informational
    if (code >= 200 && code < 300) return GREEN; // Success
    if (code >= 300 && code < 400) return CYAN; // Redirection
    if (code == 400) return BOLD YELLOW; // Bad Request
    if (code == 401 || code == 403) return BOLD MAGENTA; // Unauthorized / Forbidden
    if (code == 404) return BOLD BLUE; // Not Found
    if (code >= 400 && code < 500) return YELLOW; // Other Client Errors
    if (code == 500) return BOLD RED; // Internal Server Error
    if (code >= 500 && code < 600) return RED; // Other Server Errors
    return RESET; // Unknown or custom
}


Server::Server(int port_, lua_State *L_)
    : port(port_), L(L_)
{
}


static std::string urlDecode(const std::string &value)
{
    std::ostringstream result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result << ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            int hex = 0;
            std::istringstream(value.substr(i + 1, 2)) >> std::hex >> hex;
            result << static_cast<char>(hex);
            i += 2;
        } else {
            result << value[i];
        }
    }
    return result.str();
}

// Case-insensitive header lookup
static std::string getHeaderValue(const std::unordered_map<std::string, std::string> &h, const std::string &key)
{
    std::string lk = key;
    std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
    for (auto &p: h) {
        std::string k = p.first;
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (k == lk) return p.second;
    }
    return "";
}

// Print “Lumenite Server running at http://…”
static void printLocalIPs(int port)
{
    std::vector<std::string> addrs;
#ifdef _WIN32
    ULONG bufLen = 15000;
    auto buf = (IP_ADAPTER_ADDRESSES *) malloc(bufLen);
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, nullptr, buf, &bufLen) == ERROR_SUCCESS) {
        for (auto ad = buf; ad; ad = ad->Next) {
            for (auto ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
                if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                    char ip[INET_ADDRSTRLEN];
                    getnameinfo(ua->Address.lpSockaddr, sizeof(sockaddr_in), ip, sizeof(ip), nullptr, 0,NI_NUMERICHOST);
                    std::string s(ip);
                    if (s.rfind("169.254.", 0) != 0 && s != "0.0.0.0") addrs.push_back(s);
                }
            }
        }
    }
    free(buf);
#else
    struct ifaddrs *ifa;
    if (!getifaddrs(&ifa)) {
        for (auto p = ifa; p; p = p->ifa_next) {
            if (p->ifa_addr && p->ifa_addr->sa_family==AF_INET) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&((sockaddr_in*)p->ifa_addr)->sin_addr,ip,sizeof(ip));
                std::string s(ip);
                if (s.rfind("169.254.",0)!=0 && s!="0.0.0.0") addrs.push_back(s);
            }
        }
        freeifaddrs(ifa);
    }
#endif

    std::cout << "\033[1;36m *\033[0m \033[1mLumenite Server\033[0m running at:\n";
    for (auto &ip: addrs)
        std::cout << "   \033[1m->\033[0m \033[33mhttp://" << ip << ":" << port << "\033[0m\n";
    std::cout << "\033[1;36m *\033[0m Press \033[1mCTRL+C\033[0m to quit\n";
}

// —————————————————————————————————————————————
// Lua integration helpers (pulled from original):
// —————————————————————————————————————————————

static void push_lua_request(lua_State *L, const HttpRequest &req)
{
    lua_newtable(L);
    lua_pushstring(L, "method");
    lua_pushstring(L, req.method.c_str());
    lua_settable(L, -3);
    lua_pushstring(L, "path");
    lua_pushstring(L, req.path.c_str());
    lua_settable(L, -3);
    lua_pushstring(L, "body");
    lua_pushstring(L, req.body.c_str());
    lua_settable(L, -3);
    lua_pushstring(L, "remote_ip");
    lua_pushstring(L, req.remote_ip.c_str());
    lua_settable(L, -3);

    lua_pushstring(L, "headers");
    lua_newtable(L);
    for (auto &h: req.headers) {
        lua_pushstring(L, h.first.c_str());
        lua_pushstring(L, h.second.c_str());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "query");
    lua_newtable(L);
    for (auto &q: req.query) {
        if (q.second.size() == 1) {
            lua_pushstring(L, q.first.c_str());
            lua_pushstring(L, q.second[0].c_str());
            lua_settable(L, -3);
        } else {
            lua_pushstring(L, q.first.c_str());
            lua_newtable(L);
            for (size_t i = 0; i < q.second.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushstring(L, q.second[i].c_str());
                lua_settable(L, -3);
            }
            lua_settable(L, -3);
        }
    }
    lua_settable(L, -3);

    lua_pushstring(L, "form");
    lua_newtable(L);
    for (auto &f: req.form) {
        if (f.second.size() == 1) {
            lua_pushstring(L, f.first.c_str());
            lua_pushstring(L, f.second[0].c_str());
        } else {
            lua_pushstring(L, f.first.c_str());
            lua_newtable(L);
            for (size_t i = 0; i < f.second.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushstring(L, f.second[i].c_str());
                lua_settable(L, -3);
            }
        }
        lua_settable(L, -3);
    }
    lua_settable(L, -3);
}

static void push_lua_response(lua_State *L, const HttpResponse &res)
{
    lua_newtable(L);
    lua_pushstring(L, "status");
    lua_pushinteger(L, res.status);
    lua_settable(L, -3);
    lua_pushstring(L, "body");
    lua_pushstring(L, res.body.c_str());
    lua_settable(L, -3);

    lua_pushstring(L, "headers");
    lua_newtable(L);
    for (auto &h: res.headers) {
        lua_pushstring(L, h.first.c_str());
        lua_pushstring(L, h.second.c_str());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);
}

static void handle_lua_error(lua_State *L, HttpResponse &res)
{
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "__kind");
        bool isAbort = lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "__LUMENITE_ABORT__";
        lua_pop(L, 1);
        if (isAbort) {
            lua_getfield(L, -1, "status");
            int code = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 500;
            lua_pop(L, 1);
            res.status = code;

            lua_getfield(L, -1, "message");
            std::string msg;
            if (lua_isstring(L, -1)) msg = lua_tostring(L, -1);
            lua_pop(L, 1);

            // Log abort
            auto now = std::time(nullptr);
            std::tm *tm = std::localtime(&now);
            std::ostringstream ds, ts;
            ds << "\033[90m" << std::put_time(tm, "%d/%b/%Y") << "\033[0m";
            ts << "\033[37m:\033[35m" << std::put_time(tm, "%H:%M:%S") << "\033[0m";
            auto sc = getColorForStatus(code);

            std::cout
                    << "\033[1m[" << ds.str() << ts.str() << "]\033[0m "
                    << "\033[1m\033[31mABORT          \033[0m"
                    << sc << std::setw(4) << code << "\033[0m "
                    << "\033[1m\033[31m" << msg << "\033[0m\n";

            // Fallback HTML
            std::ostringstream fb;
            fb << "<h1>" << code << " "
                    << (statusMessages.count(code) ? statusMessages.at(code) : "Error")
                    << "</h1>";
            res.body = fb.str();
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
            lua_pop(L, 1);
            return;
        }
    }
    // Generic Lua error
    std::cerr << "\033[31m[Lua Error]\033[0m " << lua_tostring(L, -1) << "\n";
    lua_pop(L, 1);
    res.status = 500;
    res.body = "<h1>500 Internal Server Error</h1>";
    res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
}

static void parse_lua_response(lua_State *L, HttpResponse &res)
{
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "status");
        if (lua_isinteger(L, -1)) res.status = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1))
                    res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "body");
        if (lua_isstring(L, -1)) {
            size_t sz;
            const char *s = lua_tolstring(L, -1, &sz);
            res.body.assign(s, sz);
        }
        lua_pop(L, 1);
    } else if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
        size_t sz;
        const char *s = lua_tolstring(L, -1, &sz);
        res.body.assign(s, sz);
    }
    lua_pop(L, 1);

    if (!res.headers.count("Content-Type"))
        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
}

// —————————————————————————————————————————————
// 1) Read full HTTP request (headers + body) into HttpRequest
// —————————————————————————————————————————————
static bool receiveRequest(SocketType sock, const std::string &clientIp, HttpRequest &req)
{
    std::string raw;
    char buf[4096];
    ssize_t n;

    while (raw.find("\r\n\r\n") == std::string::npos) {
        n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        raw.append(buf, (size_t) n);
    }

    auto hdrEnd = raw.find("\r\n\r\n");
    std::string hdrs = raw.substr(0, hdrEnd);
    std::string body = raw.substr(hdrEnd + 4);

    std::istringstream ss(hdrs);
    std::string line;
    // Request‐line: METHOD PATH-QUERY HTTP/VERSION
    std::getline(ss, line);
    if (!line.empty() && line.back() == '\r') line.pop_back(); {
        std::istringstream ls(line);
        std::string httpVer;
        ls >> req.method >> req.path >> httpVer;
    }

    // headers
    while (std::getline(ss, line) && !line.empty()) {
        if (line.back() == '\r') line.pop_back();
        auto c = line.find(':');
        if (c != std::string::npos) {
            std::string k = line.substr(0, c);
            std::string v = line.substr(c + 1);
            if (!v.empty() && v.front() == ' ') v.erase(0, 1);
            req.headers[k] = v;
        }
    }

    // parse query string
    if (auto qm = req.path.find('?'); qm != std::string::npos) {
        std::string qs = req.path.substr(qm + 1);
        req.path.resize(qm);
        for (size_t p = 0; p < qs.size();) {
            auto amp = qs.find('&', p);
            std::string kv = qs.substr(p, amp - p);
            auto eq = kv.find('=');
            std::string k = urlDecode(kv.substr(0, eq));
            std::string v = eq != std::string::npos ? urlDecode(kv.substr(eq + 1)) : "";
            req.query[k].push_back(v);
            if (amp == std::string::npos) break;
            p = amp + 1;
        }
    }

    // read body per Content-Length
    size_t contentLen = 0;
    if (auto it = req.headers.find("Content-Length"); it != req.headers.end()) {
        contentLen = std::stoul(it->second);
    }
    while (body.size() < contentLen) {
        n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, (size_t) n);
    }
    req.body = body;

    // parse form
    if (req.headers["Content-Type"] == "application/x-www-form-urlencoded") {
        for (size_t p = 0; p < body.size();) {
            auto amp = body.find('&', p);
            std::string pr = body.substr(p, amp - p);
            auto eq = pr.find('=');
            std::string k = urlDecode(pr.substr(0, eq));
            std::string v = eq != std::string::npos ? urlDecode(pr.substr(eq + 1)) : "";
            req.form[k].push_back(v);
            if (amp == std::string::npos) break;
            p = amp + 1;
        }
    }

    // remote IP & X-Forwarded-For
    req.remote_ip = clientIp;
    if (auto it = req.headers.find("X-Forwarded-For"); it != req.headers.end()) {
        std::string ff = it->second;
        if (auto c = ff.find(','); c != std::string::npos) ff.resize(c);
        ff.erase(0, ff.find_first_not_of(" \t"));
        ff.erase(ff.find_last_not_of(" \t") + 1);
        req.remote_ip = ff;
    }

    return true;
}

// —————————————————————————————————————————————
// 2) Invoke before_request, route, after_request in Lua
// —————————————————————————————————————————————
static void processRequest(lua_State *L, HttpRequest &req, HttpResponse &res)
{
    try {
        SessionManager::start(req, res);

        // before_request hooks
        for (int ref: LumeniteApp::before_request_refs) {
            lua_rawgeti(L,LUA_REGISTRYINDEX, ref);
            push_lua_request(L, req);
            if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                handle_lua_error(L, res);
                lua_pop(L, 1);
                continue;
            }
            if (lua_istable(L, -1)) {
                // override res from table
                lua_getfield(L, -1, "status");
                if (lua_isinteger(L, -1)) res.status = lua_tointeger(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, -1, "body");
                if (lua_isstring(L, -1)) res.body = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, -1, "headers");
                if (lua_istable(L, -1)) {
                    lua_pushnil(L);
                    while (lua_next(L, -2)) {
                        if (lua_isstring(L, -2) && lua_isstring(L, -1))
                            res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                lua_pop(L, 1);
                return;
            }
            lua_pop(L, 1);
        }

        // route match
        int luaRef = 0;
        std::vector<std::string> args;
        Router::match(req.method, req.path, luaRef, args);

        if (luaRef) {
            lua_rawgeti(L,LUA_REGISTRYINDEX, luaRef);
            push_lua_request(L, req);
            for (auto &a: args) lua_pushlstring(L, a.data(), a.size());

            // insert traceback
            lua_getglobal(L, "debug");
            lua_getfield(L, -1, "traceback");
            lua_remove(L, -2);
            int tb = lua_gettop(L) - (1 + args.size()) - 1;
            lua_insert(L, tb);

            if (lua_pcall(L, 1 + (int)args.size(), 1, tb) != LUA_OK) {
                handle_lua_error(L, res);
            } else {
                parse_lua_response(L, res);
            }
            lua_remove(L, tb);
        } else {
            res.status = 404;
            res.body = "<h1>404 Not Found</h1>";
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
        }

        // after_request hooks
        for (int ref: LumeniteApp::after_request_refs) {
            lua_rawgeti(L,LUA_REGISTRYINDEX, ref);
            push_lua_request(L, req);
            push_lua_response(L, res);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                handle_lua_error(L, res);
                lua_pop(L, 1);
                continue;
            }
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "status");
                if (lua_isinteger(L, -1)) res.status = lua_tointeger(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "body");
                if (lua_isstring(L, -1)) res.body = lua_tostring(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "headers");
                if (lua_istable(L, -1)) {
                    lua_pushnil(L);
                    while (lua_next(L, -2)) {
                        if (lua_isstring(L, -2) && lua_isstring(L, -1))
                            res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    } catch (...) {
        res.status = 500;
        res.body = "<h1>500 Internal Server Error</h1>";
        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
    }
}

// —————————————————————————————————————————————
// 3) Decide if we keep the connection alive
// —————————————————————————————————————————————
static bool shouldKeepAlive(const HttpRequest &req)
{
    if (auto it = req.headers.find("Connection"); it != req.headers.end()) {
        std::string v = it->second;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "close") return false;
        if (v == "keep-alive") return true;
    }
    // HTTP/1.1 defaults to persistent
    return true;
}

// —————————————————————————————————————————————
// 4) Log to console
// —————————————————————————————————————————————
static void logRequest(const HttpRequest &req, const HttpResponse &res)
{
    auto now = std::time(nullptr);
    auto lt = *std::localtime(&now);
    std::ostringstream ds, ts;
    ds << "\033[90m" << std::put_time(&lt, "%d/%b/%Y") << "\033[0m";
    ts << "\033[37m:\033[35m" << std::put_time(&lt, "%H:%M:%S") << "\033[0m";
    auto sc = getColorForStatus(res.status);

    const char *mc = "\033[37m";
    if (req.method == "GET") mc = "\033[36m";
    else if (req.method == "POST") mc = "\033[35m";
    else if (req.method == "DELETE") mc = "\033[31m";

    std::cout
            << "\033[1m[" << ds.str() << ts.str() << "]\033[0m "
            << "\033[1m\033[37m" << std::left << std::setw(16)
            << req.remote_ip << "\033[0m "
            << sc << res.status << "\033[0m "
            << mc << req.method << "\033[0m "
            << "\033[34m" << req.path << "\033[0m\n";
}

// —————————————————————————————————————————————
// 5) Send raw bytes
// —————————————————————————————————————————————
static void sendRaw(SocketType sock, const std::string &data)
{
    send(sock, data.data(), static_cast<int>(data.size()), 0);
}

// —————————————————————————————————————————————
// Server::run — listen, accept, spawn per-client threads
// —————————————————————————————————————————————
[[noreturn]] void Server::run() const
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SocketType lsock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(lsock, (sockaddr *) &addr, sizeof(addr));
    listen(lsock, 10);

    printLocalIPs(port);

    while (true) {
        sockaddr_in ca{};
        socklen_t llen = sizeof(ca);
        SocketType csock = accept(lsock, (sockaddr *) &ca, &llen);
        if (csock < 0) continue;

        char ipb[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ca.sin_addr, ipb, sizeof(ipb));
        std::string clientIp(ipb);

        // handle each client in its own thread
        std::thread([this, csock, clientIp]()
        {
            bool keep = true;
            while (keep) {
                HttpRequest req;
                HttpResponse res; // default

                if (!receiveRequest(csock, clientIp, req)) break;

                processRequest(L, req, res);

                keep = shouldKeepAlive(req);
                res.headers["Connection"] = keep ? "keep-alive" : "close";
                res.headers["Content-Length"] = std::to_string(res.body.size());

                sendRaw(csock, res.serialize());
                logRequest(req, res);
            }
#ifdef _WIN32
            closesocket(csock);
#else
            close(csock);
#endif
        }).detach();
    }
}
