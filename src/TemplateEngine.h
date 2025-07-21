#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <variant>
#include <optional>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

// Forward declarations
struct TemplateValue;

static std::ostream &operator<<(std::ostream &os, const TemplateValue &val);


using TemplateMap = std::unordered_map<std::string, TemplateValue>;
using TemplateList = std::vector<TemplateValue>;


struct TemplateValue
{
    using Value = std::variant<std::string, TemplateMap, TemplateList>;
    Value value;

    bool isString() const { return std::holds_alternative<std::string>(value); }
    bool isMap() const { return std::holds_alternative<TemplateMap>(value); }
    bool isList() const { return std::holds_alternative<TemplateList>(value); }

    const std::string &asString() const { return std::get<std::string>(value); }
    const TemplateMap &asMap() const { return std::get<TemplateMap>(value); }
    const TemplateList &asList() const { return std::get<TemplateList>(value); }

    std::string toString() const
    {
        if (isString()) {
            return asString();
        } else if (isMap()) {
            std::ostringstream oss;
            oss << *this;
            return oss.str();
        } else if (isList()) {
            std::ostringstream oss;
            oss << *this;
            return oss.str();
        }
        return "[unknown]";
    }
};

static std::ostream &operator<<(std::ostream &os, const TemplateValue &val)
{
    if (val.isString()) {
        os << val.asString();
    } else if (val.isMap()) {
        os << "{";
        const auto &map = val.asMap();
        bool first = true;
        for (const auto &[k, v]: map) {
            if (!first) os << ", ";
            os << '"' << k << "\": " << v;
            first = false;
        }
        os << "}";
    } else if (val.isList()) {
        os << "[";
        const auto &list = val.asList();
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) os << ", ";
            os << list[i];
        }
        os << "]";
    } else {
        os << "[unknown]";
    }
    return os;
}


class TemplateEngine
{
public:
    struct CacheEntry
    {
        std::string content;
        std::chrono::steady_clock::time_point lastModified;
        bool isValid = true;
    };

    struct Config
    {
        std::string templateDir = "./templates/";
        bool enableCache = true;
        size_t maxCacheSize = 1000;
        std::chrono::seconds cacheTimeout{300};
        bool enableFileWatching = false;
    };

    // Init and config
    static void initialize(const Config &config);

    static void clearCache();

    static void setTemplateDir(const std::string &dir);


    static std::string renderFromString(const std::string &templateText,
                                        const TemplateValue &context);

    static std::pair<bool, std::string> safeRenderFromString(const std::string &templateText,
                                                             const TemplateValue &context);

    static std::string loadTemplate(const std::string &filename);

    // Lua integration
    static void registerLuaFilter(const std::string &name, lua_State *L, int funcIndex);

    static TemplateValue luaToTemplateValue(lua_State *L, int index);

    // Helpers
    static std::optional<TemplateValue> resolve(const TemplateValue &ctx, const std::string &keyPath);

private:
    // Lua filters
    static lua_State *luaState_;
    static std::unordered_map<std::string, int> luaFilters_;

    // Template cache
    static Config config_;
    static std::mutex cacheMutex_;
    static std::unordered_map<std::string, CacheEntry> templateCache_;
    static std::unordered_map<std::string, CacheEntry> compiledCache_;

    // Internal processing
    static std::string processIncludes(const std::string &text, std::vector<std::string> &includeStack);

    static std::string processLoops(const std::string &text, const TemplateValue &context);

    static std::string processConditionals(const std::string &text, const TemplateValue &context);

    static std::string substitute(const std::string &text, const TemplateValue &context);

    static std::string extractAndProcessBlocks(const std::string &text,
                                               std::unordered_map<std::string, std::string> &blocks);

    static void injectBlocks(std::string &parent,
                             const std::unordered_map<std::string, std::string> &childBlocks);

    // Utilities
    static std::string trim(const std::string &str);

    static bool isFileNewer(const std::string &filename,
                            const std::chrono::steady_clock::time_point &cacheTime);

    static void evictLRU();

    static std::string getFullPath(const std::string &filename);
};
