#include "Router.h"
#include <regex>

std::vector<Route> Router::routes;

void Router::add(const std::string& pattern, int luaRef) {
    routes.push_back({ pattern, luaRef });
}

bool Router::match(const std::string& path, int& luaRef, std::vector<std::string>& args) {
    for (const auto& route : routes) {
        std::string regexPattern = "^";
        size_t i = 0;

        while (i < route.pattern.size()) {
            if (route.pattern[i] == '<') {
                while (i < route.pattern.size() && route.pattern[i] != '>') ++i;
                regexPattern += R"(([^/]+))";
                ++i;
            } else {
                if (std::ispunct(route.pattern[i])) regexPattern += '\\';
                regexPattern += route.pattern[i++];
            }
        }

        regexPattern += "$";
        std::smatch match;
        if (std::regex_match(path, match, std::regex(regexPattern))) {
            for (size_t i = 1; i < match.size(); ++i)
                args.push_back(match[i].str());
            luaRef = route.luaRef;
            return true;
        }
    }
    return false;
}
