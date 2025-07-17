#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>


extern "C" {
#include "lua.h"
#include "lauxlib.h"
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

    // Initialization
    static void initialize(const Config &config);

    static void clearCache();

    static void setTemplateDir(const std::string &dir);

    // Public API
    static std::string render(const std::string &templateName,
                              const std::unordered_map<std::string, std::string> &context);

    static std::string loadTemplate(const std::string &filename);

    static std::string renderFromString(const std::string &templateText,
                                        const std::unordered_map<std::string, std::string> &context);

    static void registerLuaFilter(const std::string &name, lua_State *L, int funcIndex);

private:
    static lua_State *luaState_;
    static std::unordered_map<std::string, int> luaFilters_;

    static Config config_;
    static std::mutex cacheMutex_;
    static std::unordered_map<std::string, CacheEntry> templateCache_;
    static std::unordered_map<std::string, CacheEntry> compiledCache_;

    // Processing
    static std::string processIncludes(const std::string &text, std::vector<std::string> &includeStack);

    static std::string extractAndProcessBlocks(const std::string &text,
                                               std::unordered_map<std::string, std::string> &blocks);

    static void injectBlocks(std::string &parent,
                             const std::unordered_map<std::string, std::string> &childBlocks);

    static std::string processConditionals(const std::string &text,
                                           const std::unordered_map<std::string, std::string> &context);

    static std::string substitute(const std::string &text,
                                  const std::unordered_map<std::string, std::string> &context);

    // Utilities
    static std::string trim(const std::string &str);

    static bool isFileNewer(const std::string &filename,
                            const std::chrono::steady_clock::time_point &cacheTime);

    static void evictLRU();

    static std::string getFullPath(const std::string &filename);
};
