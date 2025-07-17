#include "Router.h"
#include <regex>
#include <cctype>

std::vector<Route> Router::routes;

void Router::add(const std::string &method,
                 const std::string &pattern,
                 int luaRef)
{
    routes.push_back({method, pattern, luaRef});
}

bool Router::match(const std::string &method,
                   const std::string &path,
                   int &luaRef,
                   std::vector<std::string> &args)
{
    for (auto &R: routes) {
        if (R.method != method) continue;
        std::string pat = "^";
        size_t i = 0;
        while (i < R.pattern.size()) {
            if (R.pattern[i] == '<') {
                while (i < R.pattern.size() && R.pattern[i] != '>') ++i;
                pat += R"(([^/]+))";
                ++i;
            } else {
                if (std::ispunct(static_cast<unsigned char>(R.pattern[i])))
                    pat += '\\';
                pat += R.pattern[i++];
            }
        }
        pat += "$";
        std::smatch m;
        if (std::regex_match(path, m, std::regex(pat))) {
            for (size_t j = 1; j < m.size(); ++j)
                args.push_back(m[j].str());
            luaRef = R.luaRef;
            return true;
        }
    }
    return false;
}
