// TemplateEngine.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

class TemplateEngine {
public:
    struct CacheEntry {
        std::string content;
        std::chrono::steady_clock::time_point lastModified;
        bool isValid = true;
    };

    // Configuration
    struct Config {
        std::string templateDir = "templates/";
        bool enableCache = true;
        size_t maxCacheSize = 1000;
        std::chrono::seconds cacheTimeout{300}; // 5 minutes
        bool enableFileWatching = false;
    };

    explicit TemplateEngine(const Config& config);

    std::string render(const std::string& templateName,
                      const std::unordered_map<std::string, std::string>& context);

    void clearCache();
    void setTemplateDir(const std::string& dir);

    // Static methods for backward compatibility with Lua bindings
    static std::string render(const std::string& templateText,
                             const std::unordered_map<std::string, std::string>& context);
    static std::string loadTemplate(const std::string& filename);

private:
    Config config_;
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, CacheEntry> templateCache_;
    std::unordered_map<std::string, CacheEntry> compiledCache_;

    // Core processing methods
    std::string loadTemplate(const std::string& filename);
    std::string processIncludes(const std::string& text, std::vector<std::string>& includeStack);
    std::string extractAndProcessBlocks(const std::string& text,
                                       std::unordered_map<std::string, std::string>& blocks);
    void injectBlocks(std::string& parent,
                     const std::unordered_map<std::string, std::string>& childBlocks);
    std::string substitute(const std::string& text,
                          const std::unordered_map<std::string, std::string>& context);

    // Utility methods
    std::string trim(const std::string& str);
    bool isFileNewer(const std::string& filename,
                    const std::chrono::steady_clock::time_point& cacheTime);
    void evictLRU();
    std::string getFullPath(const std::string& filename);
};