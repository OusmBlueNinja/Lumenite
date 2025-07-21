#pragma once
#include "LumeniteApp.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

extern "C"
{
#include "lua.h"
}

// A simple HTTP request representation
struct HttpRequest
{
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::vector<std::string> > query;

    std::string body;
};

// A simple HTTP response builder
struct HttpResponse
{
    int status = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string serialize() const
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " OK\r\n";
        for (auto &[k,v]: headers)
            oss << k << ": " << v << "\r\n";
        oss << "\r\n" << body;
        return oss.str();
    }
};

class Server
{
public:
    Server(int port, lua_State *L);

    void run();

private:
    int port;
    lua_State *L;


    static void sendResponse(int clientSocket, const std::string &out);
};
