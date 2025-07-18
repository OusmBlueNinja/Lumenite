#include "TemplateEngine.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

lua_State *TemplateEngine::luaState_ = nullptr;
std::unordered_map<std::string, int> TemplateEngine::luaFilters_;


TemplateEngine::Config TemplateEngine::config_;
std::mutex TemplateEngine::cacheMutex_;
std::unordered_map<std::string, TemplateEngine::CacheEntry> TemplateEngine::templateCache_;
std::unordered_map<std::string, TemplateEngine::CacheEntry> TemplateEngine::compiledCache_;

void TemplateEngine::initialize(const Config &config)
{
    config_ = config;
    if (!config_.templateDir.empty() && config_.templateDir.back() != '/')
        config_.templateDir += '/';
    clearCache();
}

void TemplateEngine::setTemplateDir(const std::string &dir)
{
    config_.templateDir = dir;
    if (!config_.templateDir.empty() && config_.templateDir.back() != '/')
        config_.templateDir += '/';
    clearCache();
}

void TemplateEngine::clearCache()
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    templateCache_.clear();
    compiledCache_.clear();
}


void TemplateEngine::registerLuaFilter(const std::string &name, lua_State *L, int funcIndex)
{
    if (!lua_isfunction(L, funcIndex)) {
        throw std::runtime_error("Expected a function for filter: " + name);
    }

    lua_pushvalue(L, funcIndex); // copy the function
    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // store in Lua registry
    luaFilters_[name] = ref;

    if (!luaState_) {
        luaState_ = L;
    }
}


std::string TemplateEngine::render(const std::string &templateName,
                                   const std::unordered_map<std::string, std::string> &context)
{ {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = compiledCache_.find(templateName);
        if (it != compiledCache_.end() && it->second.isValid) {
            if (!config_.enableFileWatching || !isFileNewer(templateName, it->second.lastModified)) {
                return substitute(it->second.content, context);
            }
        }
    }

    std::string templateContent = loadTemplate(templateName);
    return renderFromString(templateContent, context);
}


std::string TemplateEngine::renderFromString(const std::string &templateText,
                                             const std::unordered_map<std::string, std::string> &context)
{
    std::vector<std::string> includeStack;
    std::string processedContent = processIncludes(templateText, includeStack);

    std::string parentFile;
    std::string workingContent = processedContent;

    // Look for a proper {% extends "..." %} directive
    size_t extendsPos = workingContent.find("{% extends");
    if (extendsPos != std::string::npos) {
        size_t quoteStart = workingContent.find('"', extendsPos);
        size_t quoteEnd = (quoteStart != std::string::npos)
                              ? workingContent.find('"', quoteStart + 1)
                              : std::string::npos;
        size_t endTag = (quoteEnd != std::string::npos) ? workingContent.find("%}", quoteEnd) : std::string::npos;

        // Validate structure
        if (quoteStart != std::string::npos &&
            quoteEnd != std::string::npos &&
            endTag != std::string::npos &&
            quoteStart < quoteEnd && quoteEnd < endTag) {
            parentFile = workingContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            workingContent.erase(extendsPos, endTag + 2 - extendsPos);

            // sanity check: must look like a filename, not HTML
            if (parentFile.find('<') != std::string::npos || parentFile.find('>') != std::string::npos) {
                throw std::runtime_error("Invalid parent template name: " + parentFile);
            }
        } else {
            std::cerr << "Warning: Malformed {% extends %} directive ignored.\n";
        }
    }

    std::unordered_map<std::string, std::string> childBlocks;
    std::string childBody = extractAndProcessBlocks(workingContent, childBlocks);

    std::string result;

    if (!parentFile.empty()) {
        std::string parentContent = loadTemplate(parentFile);
        std::vector<std::string> parentIncludeStack;
        std::string processedParent = processIncludes(parentContent, parentIncludeStack);
        injectBlocks(processedParent, childBlocks);
        result = processedParent;
    } else if (!childBlocks.empty()) {
        result = childBody;
    } else {
        result = workingContent;
    }

    result = processConditionals(result, context);
    return substitute(result, context);
}


std::string TemplateEngine::loadTemplate(const std::string &filename)
{
    std::string fullPath = getFullPath(filename);

    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = templateCache_.find(fullPath);
        if (it != templateCache_.end() && it->second.isValid) {
            if (!config_.enableFileWatching || !isFileNewer(fullPath, it->second.lastModified)) {
                return it->second.content;
            }
        }
    }

    std::ifstream file(fullPath, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Template not found: " + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content(size, '\0');
    file.read(&content[0], size);

    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (templateCache_.size() >= config_.maxCacheSize) {
            evictLRU();
        }
        templateCache_[fullPath] = {std::move(content), std::chrono::steady_clock::now(), true};
    }

    return content;
}


std::string TemplateEngine::processIncludes(const std::string &text, std::vector<std::string> &includeStack)
{
    std::string result;
    result.reserve(text.length() * 2);

    size_t pos = 0;
    while (pos < text.length()) {
        size_t includePos = text.find("{% include", pos);
        if (includePos == std::string::npos) {
            result.append(text, pos, text.length() - pos);
            break;
        }

        result.append(text, pos, includePos - pos);
        size_t quoteStart = text.find('"', includePos);
        if (quoteStart == std::string::npos) throw std::runtime_error("Malformed include directive");
        size_t quoteEnd = text.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos) throw std::runtime_error("Malformed include directive");

        std::string filename = text.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        if (std::find(includeStack.begin(), includeStack.end(), filename) != includeStack.end())
            throw std::runtime_error("Circular include detected: " + filename);

        includeStack.push_back(filename);
        std::string includedContent = loadTemplate(filename);
        std::string processedInclude = processIncludes(includedContent, includeStack);
        includeStack.pop_back();

        result.append(processedInclude);
        size_t endPos = text.find("%}", quoteEnd);
        if (endPos == std::string::npos) throw std::runtime_error("Malformed include directive");
        pos = endPos + 2;
    }

    return result;
}

std::string TemplateEngine::extractAndProcessBlocks(
    const std::string &text,
    std::unordered_map<std::string, std::string> &blocks)
{
    std::string stripped;
    stripped.reserve(text.length());

    size_t pos = 0;
    while (pos < text.length()) {
        size_t blockStart = text.find("{% block", pos);
        if (blockStart == std::string::npos) {
            stripped.append(text, pos, text.length() - pos);
            break;
        }

        stripped.append(text, pos, blockStart - pos);

        size_t nameStart = blockStart + 8;
        while (nameStart < text.length() && isspace(text[nameStart])) nameStart++;
        size_t nameEnd = nameStart;
        while (nameEnd < text.length() && !isspace(text[nameEnd]) && text[nameEnd] != '%' && text[nameEnd] != '}')
            nameEnd++;

        std::string blockName = text.substr(nameStart, nameEnd - nameStart);
        size_t headerEnd = text.find("%}", nameEnd);
        if (headerEnd == std::string::npos) throw std::runtime_error("Malformed block header");
        headerEnd += 2;

        size_t blockEnd = text.find("{% endblock %}", headerEnd);
        if (blockEnd == std::string::npos) throw std::runtime_error("Block not closed: " + blockName);

        blocks[blockName] = text.substr(headerEnd, blockEnd - headerEnd);
        pos = blockEnd + 14;
    }

    return stripped;
}

void TemplateEngine::injectBlocks(std::string &parent,
                                  const std::unordered_map<std::string, std::string> &childBlocks)
{
    size_t pos = 0;
    while (pos < parent.length()) {
        size_t blockStart = parent.find("{% block", pos);
        if (blockStart == std::string::npos) break;

        size_t nameStart = blockStart + 8;
        while (nameStart < parent.length() && isspace(parent[nameStart])) nameStart++;
        size_t nameEnd = nameStart;
        while (nameEnd < parent.length() && !isspace(parent[nameEnd]) && parent[nameEnd] != '%' && parent[nameEnd] !=
               '}')
            nameEnd++;

        std::string blockName = parent.substr(nameStart, nameEnd - nameStart);
        size_t headerEnd = parent.find("%}", nameEnd) + 2;
        size_t blockEnd = parent.find("{% endblock %}", headerEnd);
        if (blockEnd == std::string::npos) throw std::runtime_error("Block not closed: " + blockName);

        std::string replacement = childBlocks.count(blockName)
                                      ? childBlocks.at(blockName)
                                      : parent.substr(headerEnd, blockEnd - headerEnd);

        parent.replace(blockStart, blockEnd + 14 - blockStart, replacement);
        pos = blockStart + replacement.length();
    }
}


std::string TemplateEngine::processConditionals(const std::string &text,
                                                const std::unordered_map<std::string, std::string> &context)
{
    std::string result;
    size_t pos = 0;
    while (pos < text.length()) {
        size_t ifStart = text.find("{% if ", pos);
        if (ifStart == std::string::npos) {
            result.append(text, pos, text.size() - pos);
            break;
        }

        result.append(text, pos, ifStart - pos);
        size_t condStart = ifStart + 6;
        size_t condEnd = text.find("%}", condStart);
        if (condEnd == std::string::npos) throw std::runtime_error("Malformed if statement");

        std::string condition = trim(text.substr(condStart, condEnd - condStart));
        size_t endifPos = text.find("{% endif %}", condEnd);
        if (endifPos == std::string::npos) throw std::runtime_error("Missing {% endif %}");

        std::string block = text.substr(condEnd + 2, endifPos - condEnd - 2);

        bool show = context.count(condition) &&
                    context.at(condition) != "" &&
                    context.at(condition) != "false" &&
                    context.at(condition) != "0";

        if (show) {
            result.append(block);
        }

        pos = endifPos + 12;
    }

    return result;
}

std::string TemplateEngine::substitute(const std::string &text,
                                       const std::unordered_map<std::string, std::string> &context)
{
    std::string result;
    result.reserve(text.size() + (text.size() >> 2));

    size_t pos = 0;
    while (true) {
        size_t varStart = text.find("{{", pos);
        if (varStart == std::string::npos) {
            result.append(text, pos, text.size() - pos);
            break;
        }

        result.append(text, pos, varStart - pos);
        size_t varEnd = text.find("}}", varStart);
        if (varEnd == std::string::npos)
            throw std::runtime_error("Unclosed {{ ... }} in template");

        std::string inner = trim(text.substr(varStart + 2, varEnd - varStart - 2));

        // Split by '|'
        std::istringstream ss(inner);
        std::string segment;
        std::vector<std::string> parts;

        while (std::getline(ss, segment, '|'))
            parts.push_back(trim(segment));

        if (parts.empty())
            throw std::runtime_error("Empty expression inside {{ }}");

        std::string key = parts[0];
        std::string value;
        std::string fallback;

        // Lookup value
        auto it = context.find(key);
        if (it != context.end()) {
            value = it->second;
        }

        for (size_t i = 1; i < parts.size(); ++i) {
            std::string &filter = parts[i];

            // Built-in default("fallback")
            if (filter.starts_with("default(") && filter.back() == ')') {
                fallback = filter.substr(8, filter.length() - 9);
                if (!fallback.empty() && fallback.front() == '"' && fallback.back() == '"') {
                    fallback = fallback.substr(1, fallback.size() - 2);
                }

                if (value.empty())
                    value = fallback;
            } else {
                // Lua-defined filter
                auto fit = luaFilters_.find(filter);
                if (fit != luaFilters_.end() && luaState_) {
                    lua_rawgeti(luaState_, LUA_REGISTRYINDEX, fit->second); // push function
                    lua_pushstring(luaState_, value.c_str());
                    if (lua_pcall(luaState_, 1, 1, 0) == LUA_OK) {
                        if (lua_isstring(luaState_, -1)) {
                            value = lua_tostring(luaState_, -1);
                        }
                        lua_pop(luaState_, 1); // pop result
                    } else {
                        std::string err = lua_tostring(luaState_, -1);
                        lua_pop(luaState_, 1);
                        throw std::runtime_error("Lua filter error: " + err);
                    }
                } else {
                    throw std::runtime_error("Unknown filter: " + filter);
                }
            }
        }

        if (value.empty())
            throw std::runtime_error("Missing template variable: " + key);

        result.append(value);
        pos = varEnd + 2;
    }

    return result;
}


std::string TemplateEngine::trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

bool TemplateEngine::isFileNewer(const std::string &filename,
                                 const std::chrono::steady_clock::time_point &cacheTime)
{
    try {
        auto fileTime = std::filesystem::last_write_time(filename);
        auto systemTime = std::chrono::file_clock::to_sys(fileTime);
        auto steadyTime = std::chrono::steady_clock::now() -
                          (std::chrono::system_clock::now() - systemTime);
        return steadyTime > cacheTime;
    } catch (...) {
        return true;
    }
}

void TemplateEngine::evictLRU()
{
    size_t targetSize = config_.maxCacheSize / 2;

    if (templateCache_.size() > targetSize) {
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point> > items;
        for (const auto &[key, entry]: templateCache_)
            items.emplace_back(key, entry.lastModified);

        std::sort(items.begin(), items.end(), [](const auto &a, const auto &b)
        {
            return a.second < b.second;
        });

        for (size_t i = 0; i < items.size() - targetSize; ++i)
            templateCache_.erase(items[i].first);
    }

    if (compiledCache_.size() > targetSize) {
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point> > items;
        for (const auto &[key, entry]: compiledCache_)
            items.emplace_back(key, entry.lastModified);

        std::sort(items.begin(), items.end(), [](const auto &a, const auto &b)
        {
            return a.second < b.second;
        });

        for (size_t i = 0; i < items.size() - targetSize; ++i)
            compiledCache_.erase(items[i].first);
    }
}

std::string TemplateEngine::getFullPath(const std::string &filename)
{
    return config_.templateDir + filename;
}
