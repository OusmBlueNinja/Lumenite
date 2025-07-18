// Server.cpp
#include "Server.h"
#include "Router.h"
#include <json/json.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>

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

static constexpr const char *DEFAULT_CONTENT_TYPE = "text/html";


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
    PIP_ADAPTER_ADDRESSES adapterAddrs = (IP_ADAPTER_ADDRESSES *) malloc(bufferSize);
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, nullptr, adapterAddrs, &bufferSize) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_ADDRESSES adapter = adapterAddrs; adapter != nullptr; adapter = adapter->Next) {
            for (PIP_ADAPTER_UNICAST_ADDRESS addr = adapter->FirstUnicastAddress; addr != nullptr; addr = addr->Next) {
                SOCKADDR *sa = addr->Address.lpSockaddr;
                char ip[INET6_ADDRSTRLEN];
                if (sa->sa_family == AF_INET) {
                    getnameinfo(sa, sizeof(sockaddr_in), ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST);
                    std::string ipStr = ip;

                    if (
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
        std::cout << "   -> \033[1;33mhttp://" << ip << ":" << port << "\033[0m\n";
    }

    std::cout << "\033[1;36m *\033[0m Press \033[1mCTRL+C\033[0m to quit\n";
}


void Server::run()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr *) &addr, sizeof(addr));
    listen(sock, 10);

    printLocalIPs(port);


    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(sock, (sockaddr *) &clientAddr, &len);
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
                size_t colon = line.find(':');
                if (colon != std::string_view::npos) {
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

            std::vector<std::string> args;
            int luaRef = 0;

            if (req.method == "GET" && req.path == "/") {
                Router::match("GET", "/", luaRef, args);
            } else {
                Router::match(req.method, req.path, luaRef, args);
            }

            if (luaRef) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);

                // Request table
                lua_newtable(L);
                auto pushKV = [&](const char *k, const std::string &v)
                {
                    lua_pushstring(L, k);
                    lua_pushlstring(L, v.data(), v.size());
                    lua_settable(L, -3);
                };
                pushKV("method", req.method);
                pushKV("path", req.path);
                pushKV("body", req.body);

                bool isJson = false;
                if (auto it = req.headers.find("Content-Type"); it != req.headers.end())
                    isJson = (it->second == "application/json");
                lua_pushstring(L, "json");
                if (isJson) {
                    Json::Value root;
                    Json::CharReaderBuilder builder;
                    std::string errs;
                    std::istringstream bs(req.body);
                    if (Json::parseFromStream(builder, bs, &root, &errs)) {
                        lua_newtable(L);
                        for (auto &k: root.getMemberNames()) {
                            const auto &v = root[k];
                            lua_pushstring(L, k.c_str());
                            if (v.isString()) lua_pushstring(L, v.asCString());
                            else if (v.isInt()) lua_pushinteger(L, v.asInt());
                            else if (v.isDouble()) lua_pushnumber(L, v.asDouble());
                            else if (v.isBool()) lua_pushboolean(L, v.asBool());
                            else lua_pushnil(L);
                            lua_settable(L, -3);
                        }
                    } else lua_pushnil(L);
                } else lua_pushnil(L);
                lua_settable(L, -3);

                lua_pushstring(L, "query");
                lua_newtable(L);
                for (auto &[k, v]: req.query) pushKV(k.c_str(), v);
                lua_settable(L, -3);

                for (auto &a: args)
                    lua_pushlstring(L, a.data(), a.size());

                if (lua_pcall(L, 1 + (int)args.size(), 1, 0) != LUA_OK) {
                    std::cerr << RED "[Lua Error] " << lua_tostring(L, -1) << RESET "\n";
                    res.status = 500;
                    res.body = "<h1>500 Internal Server Error</h1>";
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

                        if (res.headers.find("Content-Type") == res.headers.end())
                            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                    } catch (const std::exception &e) {
                        std::cerr << RED "[Template Error] " << e.what() << RESET "\n";
                        res.status = 500;
                        res.body = "<h1>500 Internal Server Error</h1>";
                        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                    } catch (...) {
                        std::cerr << RED "[Unknown Error] Rendering failed." RESET "\n";
                        res.status = 500;
                        res.body = "<h1>500 Internal Server Error</h1>";
                        res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
                    }
                }
            } else {
                res.status = 404;
                res.body = "<h1>404 Not Found</h1>";
                res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
            }
        } catch (const std::exception &e) {
            std::cerr << RED "[Fatal Error] " << e.what() << RESET "\n";
            res.status = 500;
            res.body = "<h1>500 Internal Server Error</h1>";
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
        } catch (...) {
            std::cerr << RED "[Fatal Error] Unknown exception" RESET "\n";
            res.status = 500;
            res.body = "<h1>500 Internal Server Error</h1>";
            res.headers["Content-Type"] = DEFAULT_CONTENT_TYPE;
        }

        sendResponse(clientSock, res.serialize());

        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        const char *color = res.status >= 500 ? RED : res.status >= 400 ? YELLOW : GREEN;
        std::cout << CYAN "[Request] " RESET
                << ip << " "
                << BLUE << req.path << RESET " "
                << color << res.status << RESET "\n";

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
    send(clientSocket, out.c_str(), (int) out.size(), 0);
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}
