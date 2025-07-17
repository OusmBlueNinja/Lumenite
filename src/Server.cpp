// Server.cpp
#include "Server.h"
#include "Router.h"
#include <json/json.h>
#include <iostream>
#include <thread>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

#define BOLD   "\033[1m"
#define CYAN   "\033[36m"
#define BLUE   "\033[34m"
#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define RED    "\033[31m"
#define RESET  "\033[0m"

// default MIME type if none provided by the handler
static constexpr const char* DEFAULT_CONTENT_TYPE = "text/html";

Server::Server(int port, lua_State* L)
  : port(port), L(L) {}

void Server::run() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(sock, (sockaddr*)&addr, sizeof(addr));
    listen(sock, 5);

    std::cout << BOLD CYAN
              << "|     " BLUE "Lumenite Server" CYAN
              << " - Listening on port " YELLOW << port << CYAN
              << "     |" RESET "\n";

    while (true) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int len = sizeof(clientAddr);
#else
        socklen_t len = sizeof(clientAddr);
#endif
        int clientSock = accept(sock, (sockaddr*)&clientAddr, &len);

        std::thread([clientSock, clientAddr, this](){
            // 1) Read raw HTTP request
            std::string raw = receiveRequest(clientSock);
            std::istringstream ss(raw);

            // 2) Build HttpRequest & HttpResponse
            HttpRequest  req;
            HttpResponse res;

            // 3) Parse request line
            std::string line;
            std::getline(ss, line);
            if (!line.empty() && line.back()=='\r') line.pop_back();
            std::istringstream rl(line);
            std::string fullPath;
            rl >> req.method >> fullPath;

            // 4) Parse headers
            while (std::getline(ss, line) && line != "\r" && !line.empty()) {
                if (line.back()=='\r') line.pop_back();
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string val = line.substr(pos+1);
                    if (!val.empty() && val.front()==' ')
                        val.erase(0,1);
                    req.headers[key] = val;
                }
            }

            // 5) Parse body
            if (ss.good()) {
                std::string b;
                std::getline(ss, b, '\0');
                req.body = b;
            }

            // 6) Parse query string
            auto qm = fullPath.find('?');
            if (qm != std::string::npos) {
                req.path = fullPath.substr(0, qm);
                std::string qs = fullPath.substr(qm+1);
                std::istringstream qss(qs);
                std::string kv;
                while (std::getline(qss, kv, '&')) {
                    auto eq = kv.find('=');
                    if (eq != std::string::npos) {
                        req.query[kv.substr(0,eq)] = kv.substr(eq+1);
                    }
                }
            } else {
                req.path = fullPath;
            }
            if (req.path.empty()) req.path = "/";

            // 7) Start session
            SessionManager::start(req, res);

            // 8) Route lookup and Lua invocation
            std::vector<std::string> args;
            int luaRef = 0;
            if (Router::match(req.method, req.path, luaRef, args)) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);

                // Push `req` table
                lua_newtable(L);
                lua_pushstring(L,"method"); lua_pushstring(L,req.method.c_str()); lua_settable(L,-3);
                lua_pushstring(L,"path");   lua_pushstring(L,req.path.c_str());   lua_settable(L,-3);
                lua_pushstring(L,"body");   lua_pushstring(L,req.body.c_str());   lua_settable(L,-3);

                // JSON body → req.json
                lua_pushstring(L,"json");
                Json::Value root;
                Json::CharReaderBuilder builder;
                std::string errs;
                std::istringstream bs(req.body);
                if (Json::parseFromStream(builder, bs, &root, &errs)) {
                    lua_newtable(L);
                    for (auto& k : root.getMemberNames()) {
                        lua_pushstring(L, k.c_str());
                        const auto& v = root[k];
                        if      (v.isString()) lua_pushstring(L, v.asCString());
                        else if (v.isInt())    lua_pushinteger(L, v.asInt());
                        else if (v.isDouble()) lua_pushnumber(L, v.asDouble());
                        else if (v.isBool())   lua_pushboolean(L, v.asBool());
                        else                   lua_pushnil(L);
                        lua_settable(L, -3);
                    }
                } else {
                    lua_pushnil(L);
                }
                lua_settable(L,-3);

                // Query params → req.query
                lua_pushstring(L,"query");
                lua_newtable(L);
                for (auto& [k,v] : req.query) {
                    lua_pushstring(L, k.c_str());
                    lua_pushstring(L, v.c_str());
                    lua_settable(L, -3);
                }
                lua_settable(L,-3);

                // Push any capture groups
                for (auto& a : args)
                    lua_pushstring(L, a.c_str());

                // Call handler(req, ...captures)
                if (lua_pcall(L, 1 + args.size(), 1, 0) != LUA_OK) {
                    std::cerr << RED "[Lua Error] " << lua_tostring(L,-1) << RESET << "\n";
                    res.status = 500;
                    res.body   = "<h1>500 Internal Server Error</h1>";
                    res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                } else {
                    // We expect the handler to return a string or a table
                    if (lua_istable(L, -1)) {
                        // Table: extract status, headers, body
                        lua_getfield(L, -1, "status");
                        if (lua_isinteger(L, -1))
                            res.status = lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "headers");
                        if (lua_istable(L, -1)) {
                            lua_pushnil(L);
                            while (lua_next(L, -2)) {
                                std::string hk = lua_tostring(L, -2);
                                std::string hv = lua_tostring(L, -1);
                                res.headers[hk] = hv;
                                lua_pop(L, 1);
                            }
                        }
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "body");
                        if (lua_isstring(L, -1))
                            res.body = lua_tostring(L, -1);
                        lua_pop(L, 1);
                    } else {
                        // Anything else → string body
                        if (lua_isstring(L, -1) || lua_isnumber(L, -1))
                            res.body = lua_tostring(L, -1);
                    }

                    // If no Content-Type header was set by the handler, use default
                    if (res.headers.find("Content-Type") == res.headers.end()) {
                        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                    }
                }
            } else {
                // No matching route
                res.status = 404;
                res.body   = "<h1>404 Not Found</h1>";
                res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
            }

            // 9) Send response
            sendResponse(clientSock, res.serialize());

            // 10) Logging
            char ip[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            const char* color =
                (res.status >= 500) ? RED :
                (res.status >= 400) ? YELLOW : GREEN;
            std::cout << CYAN "[Request] " RESET
                      << ip << " "
                      << BLUE << req.path << RESET " "
                      << color << res.status << RESET "\n";
        }).detach();
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

std::string Server::receiveRequest(int clientSocket) {
    char buf[8192];
    int n = recv(clientSocket, buf, sizeof(buf), 0);
    return (n > 0) ? std::string(buf, n) : std::string();
}

void Server::sendResponse(int clientSocket, const std::string& out) {
    send(clientSocket, out.c_str(), static_cast<int>(out.size()), 0);
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}
