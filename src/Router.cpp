#include "Router.h"
#include <regex>
#include <cctype>

std::vector<Route> Router::routes;

static std::string buildRegexPattern(const std::string &pattern)
{
    std::string pat = "^";
    pat.reserve(pattern.size() * 2); // avoid repeated reallocation

    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] == '<') {
            // skip until closing >
            while (i < pattern.size() && pattern[i] != '>') ++i;
            ++i; // skip '>'
            pat += R"(([^/]+))";
        } else {
            if (std::ispunct(static_cast<unsigned char>(pattern[i]))) {
                pat += '\\';
            }
            pat += pattern[i++];
        }
    }

    pat += '$';
    return pat;
}

void Router::add(const std::string &method,
                 const std::string &pattern,
                 int luaRef)
{
    std::string regexStr = buildRegexPattern(pattern);
    std::regex compiled(regexStr);
    routes.push_back({method, pattern, std::move(compiled), luaRef});
}

bool Router::match(const std::string &method,
                   const std::string &path,
                   int &luaRef,
                   std::vector<std::string> &args)
{
    for (const auto &R: routes) {
        if (R.method != method) continue;

        std::smatch m;
        if (std::regex_match(path, m, R.compiled)) {
            args.clear();
            args.reserve(m.size() - 1);
            for (size_t j = 1; j < m.size(); ++j)
                args.emplace_back(m[j].str());

            luaRef = R.luaRef;
            return true;
        }
    }
    return false;
}
