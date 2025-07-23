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


Server::Server(int port, lua_State *L)
    : port(port), L(L)
{
}


void printLocalIPs(int port)
{
    std::vector<std::string> addresses;

#ifdef _WIN32
    ULONG bufferSize = 15000;
    const auto adapterAddrs = static_cast<IP_ADAPTER_ADDRESSES *>(malloc(bufferSize));
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, nullptr, adapterAddrs, &bufferSize) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_ADDRESSES adapter = adapterAddrs; adapter != nullptr; adapter = adapter->Next) {
            for (PIP_ADAPTER_UNICAST_ADDRESS addr = adapter->FirstUnicastAddress; addr != nullptr; addr = addr->Next) {
                SOCKADDR *sa = addr->Address.lpSockaddr;
                char ip[INET6_ADDRSTRLEN];
                if (sa->sa_family == AF_INET) {
                    getnameinfo(sa, sizeof(sockaddr_in), ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST);

                    if (std::string ipStr = ip;
                        ipStr.rfind("169.254.", 0) != 0 && // Ignore link-local
                        ipStr != "0.0.0.0"
                    ) {
                        addresses.emplace_back(ipStr);
                    }
                }
            }
        }
    }
    free(adapterAddrs);
#else
    struct ifaddrs *ifaddr;
    getifaddrs(&ifaddr);
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            void *addr_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip, sizeof(ip));
            std::string ipStr = ip;

            if (
                ipStr.rfind("169.254.", 0) != 0 &&
                ipStr != "0.0.0.0"
            ) {
                addresses.emplace_back(ipStr);
            }
        }
    }
    freeifaddrs(ifaddr);
#endif

    std::cout << BOLD << CYAN << " *" << RESET << " " << BOLD << "Lumenite Server" << RESET << " running at:\n";

    for (const auto &ip: addresses) {
        std::cout << "   " << BOLD << "→" << RESET << " "
                << YELLOW << "http://" << ip << ":" << port << RESET << "\n";
    }

    std::cout << BOLD << CYAN << " *" << RESET << " Press " << BOLD << "CTRL+C" << RESET << " to quit\n";
}


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
    for (const auto &[k, v]: req.headers) {
        lua_pushstring(L, k.c_str());
        lua_pushstring(L, v.c_str());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "query");
    lua_newtable(L);
    for (auto &[key, values]: req.query) {
        if (values.size() == 1) {
            lua_pushstring(L, key.c_str());
            lua_pushstring(L, values[0].c_str());
            lua_settable(L, -3);
        } else {
            lua_pushstring(L, key.c_str());
            lua_newtable(L);
            for (size_t i = 0; i < values.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushstring(L, values[i].c_str());
                lua_settable(L, -3);
            }
            lua_settable(L, -3);
        }
    }
    lua_settable(L, -3);

    lua_pushstring(L, "form");
    lua_newtable(L);
    for (auto &[key, values]: req.form) {
        if (values.size() == 1) {
            lua_pushstring(L, key.c_str());
            lua_pushstring(L, values[0].c_str());
        } else {
            lua_pushstring(L, key.c_str());
            lua_newtable(L);
            for (size_t i = 0; i < values.size(); ++i) {
                lua_pushinteger(L, i + 1);
                lua_pushstring(L, values[i].c_str());
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
    for (const auto &[k, v]: res.headers) {
        lua_pushstring(L, k.c_str());
        lua_pushstring(L, v.c_str());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);
}


std::string urlDecode(const std::string &value)
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

std::string getHeaderValue(const std::unordered_map<std::string, std::string> &headers, const std::string &key)
{
    std::string keyLower = key;
    std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), tolower);

    for (const auto &[k, v]: headers) {
        std::string headerKey = k;
        std::transform(headerKey.begin(), headerKey.end(), headerKey.begin(), tolower);
        if (headerKey == keyLower) {
            return v;
        }
    }

    return "";
}


void handle_lua_error(lua_State *L, HttpResponse &res)
{
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "__kind");
        const bool isAbort = lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "__LUMENITE_ABORT__";
        lua_pop(L, 1);

        if (isAbort) {
            lua_getfield(L, -1, "status");
            const int code = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 500;
            res.status = code;
            lua_pop(L, 1);

            // Extract message
            std::string message{};
            lua_getfield(L, -1, "message");
            if (lua_isstring(L, -1)) {
                message = lua_tostring(L, -1);
                const std::string color = getColorForStatus(code);

                std::cerr << BOLD << color << code << RESET
                        << "  "
                        << "[Abort] "
                        << message << "\n";
            }
            lua_pop(L, 1);

            std::ostringstream fallback;
            fallback << "<h1>" << code << " "
                    << (statusMessages.contains(code) ? statusMessages.at(code) : "Error")
                    << "</h1>";
            res.body = fallback.str();

            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
            lua_pop(L, 1);
            return;
        }
    }

    std::cerr << RED << "[Lua Error] " << lua_tostring(L, -1) << RESET << "\n";
    lua_pop(L, 1);
    res.status = 500;
    res.body = "<h1>" + std::to_string(res.status) + " " + statusMessages.at(res.status) + "</h1>";
    res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
}


void parse_lua_response(lua_State *L, HttpResponse &res)
{
    try {
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "status");
            if (lua_isinteger(L, -1))
                res.status = lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "headers");
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                        res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                    }
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

        if (!res.headers.contains("Content-Type"))
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
    } catch (...) {
        res.status = 500;
        res.body = "<h1>" + std::to_string(res.status) + " " + statusMessages.at(res.status) + "</h1>";
        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
    }
}


[[noreturn]] void Server::run() const
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SocketType sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    listen(sock, 10);

    printLocalIPs(port);

    bool running = true; // Only here to make compiler warnings stop for "unreachable code"

    while (running) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(sock, reinterpret_cast<sockaddr *>(&clientAddr), &len);
        if (clientSock < 0) continue;

        std::vector<char> buf(8192);
        int n = recv(clientSock, buf.data(), static_cast<int>(buf.size()), 0);
        if (n <= 0) {
#ifdef _WIN32
            closesocket(clientSock);
#else
            close(clientSock);
#endif
            continue;
        }
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));


        HttpRequest req;
        HttpResponse res;


        try {
            std::string_view raw(buf.data(), n);
            size_t headerEnd = raw.find("\r\n\r\n");
            std::string_view header = headerEnd == std::string_view::npos
                                          ? raw
                                          : raw.substr(0, headerEnd);
            std::string_view body = headerEnd == std::string_view::npos
                                        ? std::string_view{}
                                        : raw.substr(headerEnd + 4);

            // Parse request line
            size_t lineEnd = header.find("\r\n");
            std::string_view reqLine = lineEnd == std::string_view::npos ? header : header.substr(0, lineEnd);
            size_t sp2 = reqLine.rfind(' ');
            if (size_t sp1 = reqLine.find(' '); sp1 != std::string_view::npos && sp2 != sp1) {
                req.method = std::string(reqLine.substr(0, sp1));
                req.path = std::string(reqLine.substr(sp1 + 1, sp2 - sp1 - 1));
            }


            // Parse headers
            size_t pos = lineEnd + 2;
            while (pos < header.size()) {
                size_t eol = header.find("\r\n", pos);
                if (eol == std::string_view::npos) break;
                auto line = header.substr(pos, eol - pos);
                if (size_t colon = line.find(':'); colon != std::string_view::npos) {
                    auto key = std::string(line.substr(0, colon));
                    auto val = std::string(line.substr(colon + 1));
                    if (!val.empty() && val.front() == ' ') val = val.substr(1);
                    req.headers.emplace(std::move(key), std::move(val));
                }
                pos = eol + 2;
            }

            // Body copy
            if (!body.empty()) req.body.assign(body.begin(), body.end());

            if (req.headers["Content-Type"] == "application/x-www-form-urlencoded") {
                std::string &local_body = req.body;

                size_t p = 0;
                while (p < local_body.size()) {
                    size_t amp = local_body.find('&', p);
                    std::string pair = local_body.substr(p, amp - p);

                    size_t eq = pair.find('=');
                    std::string key = urlDecode(pair.substr(0, eq));
                    std::string val = eq != std::string::npos ? urlDecode(pair.substr(eq + 1)) : "";

                    req.form[key].push_back(val); // support repeated keys
                    if (amp == std::string::npos) break;
                    p = amp + 1;
                }
            }


            if (auto qm = req.path.find('?'); qm != std::string::npos) {
                std::string qs = req.path.substr(qm + 1);
                req.path.resize(qm);

                size_t p = 0;
                while ((qm = qs.find('&', p)) != std::string::npos) {
                    auto kv = qs.substr(p, qm - p);
                    if (auto eq = kv.find('='); eq != std::string::npos) {
                        std::string key = urlDecode(kv.substr(0, eq));
                        std::string value = urlDecode(kv.substr(eq + 1));
                        req.query[key].push_back(value);
                    }
                    p = qm + 1;
                }

                auto kv = qs.substr(p);
                if (auto eq = kv.find('='); eq != std::string::npos) {
                    std::string key = urlDecode(kv.substr(0, eq));
                    std::string value = urlDecode(kv.substr(eq + 1));
                    req.query[key].push_back(value);
                }
            }


            req.remote_ip = ip;

            if (std::string xfwd = getHeaderValue(req.headers, "X-Forwarded-For"); !xfwd.empty()) {
                size_t comma = xfwd.find(',');
                std::string firstIp = comma != std::string::npos ? xfwd.substr(0, comma) : xfwd;

                // Trim spaces
                size_t start = firstIp.find_first_not_of(" \t");
                if (size_t end = firstIp.find_last_not_of(" \t");
                    start != std::string::npos && end != std::string::npos)
                    req.remote_ip = firstIp.substr(start, end - start + 1);
                else
                    req.remote_ip = firstIp;
            }


            SessionManager::start(req, res);

            bool earlyExit = false;

            if (LumeniteApp::before_request_ref != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, LumeniteApp::before_request_ref);
                push_lua_request(L, req);
                if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                    std::cerr << RED "[before_request error] " << lua_tostring(L, -1) << RESET "\n";
                    lua_pop(L, 1);
                } else if (lua_istable(L, -1)) {
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
                            res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);

                    earlyExit = true;
                }
                lua_pop(L, 1);
            }

            if (!earlyExit) {
                int luaRef = 0;
                std::vector<std::string> args;
                if (req.method == "GET" && req.path == "/") {
                    Router::match("GET", "/", luaRef, args);
                } else {
                    Router::match(req.method, req.path, luaRef, args);
                }

                if (luaRef) {
                    // Push function and arguments
                    lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
                    push_lua_request(L, req);

                    for (const auto &a: args)
                        lua_pushlstring(L, a.data(), a.size());

                    int nargs = 1 + static_cast<int>(args.size());

                    lua_getglobal(L, "debug");
                    lua_getfield(L, -1, "traceback");
                    lua_remove(L, -2);
                    int tracebackIndex = lua_gettop(L) - nargs - 1;
                    lua_insert(L, tracebackIndex);

                    if (lua_pcall(L, nargs, 1, tracebackIndex) != LUA_OK) {
                        handle_lua_error(L, res);
                    } else {
                        parse_lua_response(L, res);
                    }

                    lua_remove(L, tracebackIndex);
                } else {
                    res.status = 404;
                    res.body = "<h1>" + std::to_string(res.status) + " " + statusMessages.at(res.status) + "</h1>";
                    res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                }

                if (LumeniteApp::after_request_ref != LUA_NOREF) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, LumeniteApp::after_request_ref);
                    push_lua_request(L, req);
                    push_lua_response(L, res);

                    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                        std::cerr << RED "[after_request error] " << lua_tostring(L, -1) << RESET "\n";
                        lua_pop(L, 1);
                    } else if (lua_istable(L, -1)) {
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
                                res.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                                lua_pop(L, 1);
                            }
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
            }
        } catch (...) {
            res.status = 500;
            res.body = "<h1>" + std::to_string(res.status) + " " + statusMessages.at(res.status) + "</h1>";
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
        }

        sendResponse(clientSock, res.serialize());


        std::time_t now = std::time(nullptr);
        std::tm *ltm = std::localtime(&now);

        std::ostringstream dateStream, timeStream;
        dateStream << "\033[90m" << std::put_time(ltm, "%d/%b/%Y") << RESET;
        timeStream << WHITE << ":" << MAGENTA << std::put_time(ltm, "%H:%M:%S") << RESET;

        auto statusColor = getColorForStatus(res.status);


        const char *methodColor = nullptr;
        if (req.method == "GET") methodColor = CYAN;
        else if (req.method == "POST") methodColor = MAGENTA;
        else if (req.method == "DELETE") methodColor = RED;
        else methodColor = WHITE;


        std::string method = req.method;

        std::cout << BOLD << "[" << dateStream.str() << timeStream.str() << "]" << RESET " "
                << BOLD << WHITE << std::left << std::setw(16) << ip << RESET
                << statusColor << res.status << RESET " "
                << methodColor << method << RESET " "
                << BLUE << req.path << RESET << "\n";


        lua_settop(L, 0);
    }


#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}


void Server::sendResponse(int clientSocket, const std::string &out)
{
    send(clientSocket, out.c_str(), static_cast<int>(out.size()), 0);
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}
