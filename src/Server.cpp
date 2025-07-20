// Server.cpp
#include "Server.h"
#include "Router.h"
#include <json/json.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>
#include "LumeniteApp.h"
#include "SessionManager.h"


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

static constexpr auto DEFAULT_CONTENT_TYPE = "text/html";


static auto ERROR_MSG_500 = "<h1>500 Internal Server Error</h1>";
static auto ERROR_MSG_400 = "<h1>404 Not Found</h1>";


Server::Server(int port, lua_State *L)
    : port(port), L(L)
{
}


#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
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
                ipStr.rfind("169.254.", 0) != 0 &&  // Ignore link-local
                ipStr != "0.0.0.0"
            ) {
                addresses.emplace_back(ipStr);
            }
        }
    }
    freeifaddrs(ifaddr);
#endif

    std::cout << "\033[1;36m *\033[0m Lumenite Server \033[1mrunning at:\033[0m\n";

    for (const auto &ip: addresses) {
        std::cout << "   ->\033[1;33m http://" << ip << ":" << port << "\033[0m\n";
    }

    std::cout << "\033[1;36m *\033[0m Press \033[1mCTRL+C\033[0m to quit\n";
}


// Push full request table to Lua
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
    for (const auto &[k, v]: req.query) {
        lua_pushstring(L, k.c_str());
        lua_pushstring(L, v.c_str());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);
}

// Push full response table to Lua
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


void Server::run()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    listen(sock, 10);

    printLocalIPs(port);

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(sock, reinterpret_cast<sockaddr *>(&clientAddr), &len);
        if (clientSock < 0) continue;

        std::vector<char> buf(8192);
        int n = recv(clientSock, buf.data(), (int) buf.size(), 0);
        if (n <= 0) {
#ifdef _WIN32
            closesocket(clientSock);
#else
            close(clientSock);
#endif
            continue;
        }

        HttpRequest req;
        HttpResponse res;

        try {
            std::string_view raw(buf.data(), n);
            size_t headerEnd = raw.find("\r\n\r\n");
            std::string_view header = (headerEnd == std::string_view::npos)
                                          ? raw
                                          : raw.substr(0, headerEnd);
            std::string_view body = (headerEnd == std::string_view::npos)
                                        ? std::string_view{}
                                        : raw.substr(headerEnd + 4);

            // Parse request line
            size_t lineEnd = header.find("\r\n");
            std::string_view reqLine = (lineEnd == std::string_view::npos) ? header : header.substr(0, lineEnd);
            size_t sp1 = reqLine.find(' '), sp2 = reqLine.rfind(' ');
            if (sp1 != std::string_view::npos && sp2 != sp1) {
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

            // Query parsing
            if (auto qm = req.path.find('?'); qm != std::string::npos) {
                std::string qs = req.path.substr(qm + 1);
                req.path.resize(qm);
                size_t p = 0;
                while ((qm = qs.find('&', p)) != std::string::npos) {
                    auto kv = qs.substr(p, qm - p);
                    if (auto eq = kv.find('='); eq != std::string::npos)
                        req.query[kv.substr(0, eq)] = kv.substr(eq + 1);
                    p = qm + 1;
                }
                auto kv = qs.substr(p);
                if (auto eq = kv.find('='); eq != std::string::npos)
                    req.query[kv.substr(0, eq)] = kv.substr(eq + 1);
            }

            SessionManager::start(req, res);

            bool earlyExit = false;

            // ——— BEFORE REQUEST HOOK ———
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

            std::vector<std::string> args;
            int luaRef = 0;

            if (!earlyExit) {
                if (req.method == "GET" && req.path == "/") {
                    Router::match("GET", "/", luaRef, args);
                } else {
                    Router::match(req.method, req.path, luaRef, args);
                }

                if (luaRef) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
                    push_lua_request(L, req);

                    for (auto &a: args)
                        lua_pushlstring(L, a.data(), a.size());

                    if (lua_pcall(L, 1 + static_cast<int>(args.size()), 1, 0) != LUA_OK) {
                        std::cerr << RED "[Lua Error] " << lua_tostring(L, -1) << RESET "\n";
                        res.status = 500;
                        res.body = ERROR_MSG_500;
                    } else {
                        try {
                            if (lua_istable(L, -1)) {
                                lua_getfield(L, -1, "status");
                                if (lua_isinteger(L, -1)) res.status = lua_tointeger(L, -1);
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
                            res.body = ERROR_MSG_500;
                            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                        }
                    }
                } else {
                    res.status = 404;
                    res.body = ERROR_MSG_400;
                    res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                }

                // ——— AFTER REQUEST HOOK ———
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
            res.body = ERROR_MSG_500;
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
        }

        sendResponse(clientSock, res.serialize());

        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        const char *color = res.status >= 500 ? RED : res.status >= 400 ? YELLOW : GREEN;
        std::cout << CYAN "[Request] " RESET << ip << " " << BLUE << req.path << RESET " " << color << res.status <<
                RESET "\n";

        lua_settop(L, 0);
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}


std::string Server::receiveRequest(int clientSocket) { return ""; }

void Server::sendResponse(int clientSocket, const std::string &out)
{
    send(clientSocket, out.c_str(), static_cast<int>(out.size()), 0);
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}
