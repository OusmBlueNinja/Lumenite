#pragma once
#include <string>
#include <vector>

struct Route
{
    std::string method;
    std::string pattern;
    int luaRef;
};

namespace Router
{
    extern std::vector<Route> routes;

    void add(const std::string &method,
             const std::string &pattern,
             int luaRef);

    bool match(const std::string &method,
               const std::string &path,
               int &luaRef,
               std::vector<std::string> &args);
}
