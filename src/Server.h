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

struct HttpRequest
{
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::vector<std::string> > query;
    std::unordered_map<std::string, std::vector<std::string> > form;
    std::string body;
    std::string remote_ip;
};

inline std::ostream &operator<<(std::ostream &os, const HttpRequest &req)
{
    for (const auto &[key, value]: req.headers) {
        os << key << ": " << value << "\n";
    }
    return os;
}

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


    static std::string getHeaderValue(const std::unordered_map<std::string, std::string> &headers,
                                      const std::string &key);

    [[noreturn]] void run() const;

private:
    int port;
    lua_State *L;


    static void sendResponse(int clientSocket, const std::string &out);
};


