#pragma once
#include <string>
#include <vector>
#include <regex>

struct Route
{
    std::string method;
    std::string pattern;
    std::regex compiled;
    int luaRef;
};

class Router
{
public:
    static void add(const std::string &method,
                    const std::string &pattern,
                    int luaRef);

    static bool match(const std::string &method,
                      const std::string &path,
                      int &luaRef,
                      std::vector<std::string> &args);

private:
    static std::vector<Route> routes;
};
