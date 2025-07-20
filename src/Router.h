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
    // Add a route to the router
    static void add(const std::string &method,
                    const std::string &pattern,
                    int luaRef);

    // Match a route to a request
    // Returns true if a match was found, false otherwise
    // If a match was found, the luaRef and args are set
    // (ai)
    static bool match(const std::string &method,
                      const std::string &path,
                      int &luaRef,
                      std::vector<std::string> &args);

private:
    static std::vector<Route> routes;
};
