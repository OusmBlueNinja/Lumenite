#pragma once
#include <string>

extern "C" {
#include "lua.h"
}

class Server {
public:
    Server(int port, lua_State* L);
    void run();

private:
    int port;
    lua_State* L;
    static std::string receiveRequest(int clientSocket);
    static void sendResponse(int clientSocket, int code, const std::string& contentType, const std::string& body);
};
