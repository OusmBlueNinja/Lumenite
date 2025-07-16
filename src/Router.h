#pragma once
#include <string>
#include <vector>
#include <functional>

struct Route {
    std::string pattern;
    int luaRef;
};

namespace Router {
    extern std::vector<Route> routes;

    void add(const std::string& pattern, int luaRef);
    bool match(const std::string& path, int& luaRef, std::vector<std::string>& args);
}
