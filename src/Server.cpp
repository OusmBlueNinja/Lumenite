#include "Server.h"
#include "Router.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RED     "\033[31m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define RESET   "\033[0m"
#define BOLD    "\033[1m"

Server::Server(int port, lua_State* L) : port(port), L(L) {}

void Server::run() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    std::cout << BOLD CYAN
              << "|     " << BLUE << "Lumenite Server" << CYAN << " - Listening on port "
              << YELLOW << port << CYAN << "     |\n"
              << RESET;

    while (true) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int addrSize = sizeof(clientAddr);
#else
        socklen_t addrSize = sizeof(clientAddr);
#endif
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &addrSize);

        std::thread([clientSocket, clientAddr, this]() {
            std::string request = receiveRequest(clientSocket);
            std::string path = "/";
            if (request.find("GET /") == 0) {
                size_t end = request.find(' ', 4);
                path = request.substr(4, end - 4);
                size_t q = path.find('?');
                if (q != std::string::npos)
                    path = path.substr(0, q);
                if (path.empty()) path = "/";
            }

            int statusCode = 200;
            std::string response;
            std::string contentType = "text/html";

            std::vector<std::string> args;
            int luaRef = 0;

            if (Router::match(path, luaRef, args)) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
                lua_pushstring(L, request.c_str());
                for (const auto& arg : args)
                    lua_pushstring(L, arg.c_str());

                if (lua_pcall(L, 1 + args.size(), 1, 0) != LUA_OK) {
                    std::cerr << RED "[Lua Error] " << lua_tostring(L, -1) << RESET "\n";
                    response = "<h1>500 Internal Server Error</h1>";
                    contentType = "text/html";
                    statusCode = 500;
                } else {
                    const char* luaResp = lua_tostring(L, -1);
                    response = luaResp ? luaResp : "";

                    if (response.rfind("content-type:json;", 0) == 0) {
                        response = response.substr(strlen("content-type:json;"));
                        contentType = "application/json";
                    } else if (path.find("/api/") == 0 || path.ends_with(".json")) {
                        contentType = "application/json";
                    }
                }
            } else {
                response = "<h1>404 Not Found</h1>";
                contentType = "text/html";
                statusCode = 404;
            }

            sendResponse(clientSocket, statusCode, contentType, response);

            char ip[INET6_ADDRSTRLEN] = "unknown";
            inet_ntop(AF_INET, &(clientAddr.sin_addr), ip, sizeof(ip));

            std::string statusColor = (statusCode >= 500) ? RED :
                                      (statusCode >= 400) ? YELLOW :
                                      GREEN;

            std::cout << CYAN << "[Request] " << RESET
                      << ip << " "
                      << BLUE << path << RESET " "
                      << statusColor << statusCode << RESET "\n";

#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
        }).detach();
    }

#ifdef _WIN32
    closesocket(serverSocket);
    WSACleanup();
#else
    close(serverSocket);
#endif
}

std::string Server::receiveRequest(int clientSocket) {
    char buffer[4096]{};
    recv(clientSocket, buffer, sizeof(buffer), 0);
    return std::string(buffer);
}

void Server::sendResponse(int clientSocket, int code, const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    std::string response = oss.str();
    send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
}
