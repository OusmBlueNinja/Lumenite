#include "TemplateEngine.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <regex>


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


#include <regex>

std::string TemplateEngine::renderFromString(const std::string &templateText,
                                             const TemplateValue &context)
{
    std::vector<std::string> includeStack;
    std::string processedContent = processIncludes(templateText, includeStack);

    std::string workingContent = processedContent;
    std::string parentFile;

    // Regex match for: {% extends "filename" %}
    std::regex extendsPattern(R"(\{\%\s*extends\s*"([^"]+)\"\s*\%\})");
    std::smatch match;

    if (std::regex_search(workingContent, match, extendsPattern)) {
        parentFile = match[1];

        // Validate template name
        if (parentFile.find('<') != std::string::npos || parentFile.find('>') != std::string::npos) {
            throw std::runtime_error("[TemplateError.SyntaxError] Invalid parent template name: " + parentFile);
        }

        // Remove the entire {% extends ... %} line
        workingContent = std::regex_replace(workingContent, extendsPattern, "");
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

    result = processLoops(result, context);
    result = processConditionals(result, context);
    result = substitute(result, context);
    return result;
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
    std::regex includePattern(R"(\{\%\s*include\s*"([^"]+)\"\s*\%\})");
    std::smatch match;
    std::string result;
    std::string::const_iterator searchStart(text.cbegin());

    while (std::regex_search(searchStart, text.cend(), match, includePattern)) {
        result.append(searchStart, match[0].first);

        std::string filename = match[1];
        if (std::find(includeStack.begin(), includeStack.end(), filename) != includeStack.end()) {
            throw std::runtime_error("Circular include detected: " + filename);
        }

        includeStack.push_back(filename);
        std::string includedContent = loadTemplate(filename);
        std::string processedInclude = processIncludes(includedContent, includeStack);
        includeStack.pop_back();

        result.append(processedInclude);
        searchStart = match.suffix().first;
    }

    result.append(searchStart, text.cend());
    return result;
}


std::string TemplateEngine::processConditionals(const std::string &text, const TemplateValue &context)
{
    std::regex ifPattern(R"(\{\%\s*if\s+([^\%]+?)\s*\%\}((.|\n)*?)\{\%\s*endif\s*\%\})");
    std::smatch match;
    std::string result = text;
    bool matched = true;

    while (matched) {
        matched = std::regex_search(result, match, ifPattern);
        if (!matched) break;

        std::string condition = trim(match[1]);
        std::string block = match[2];

        bool shouldShow = false;
        auto value = resolve(context, condition);
        if (value) {
            std::string s = value->toString();
            shouldShow = !s.empty() && s != "0" && s != "false";
        }

        result = match.prefix().str() + (shouldShow ? block : "") + match.suffix().str();
    }

    return result;
}


std::string TemplateEngine::processLoops(const std::string &text, const TemplateValue &ctx)
{
    std::regex forPattern(R"(\{\%\s*for\s+(\w+)\s+in\s+(\w+)\s*\%\}([\s\S]*?)\{\%\s*endfor\s*\%\})");
    std::smatch match;
    std::string result = text;
    bool matched = true;

    while (matched) {
        matched = std::regex_search(result, match, forPattern);

        if (!matched) break;

        std::string loopVar = match[1];
        std::string listName = match[2];
        std::string block = match[3];

        std::string loopOut;

        auto maybeList = resolve(ctx, listName);
        if (!maybeList || !std::holds_alternative<TemplateList>(maybeList->value)) {
            throw std::runtime_error("[TemplateError.ValueError] List not found or invalid: " + listName);
        }

        const TemplateList &items = std::get<TemplateList>(maybeList->value);
        for (const auto &item: items) {
            TemplateMap combinedCtx;

            if (ctx.isMap()) {
                combinedCtx = ctx.asMap(); // copy parent context
            }

            combinedCtx[loopVar] = item;
            TemplateValue loopCtx;
            loopCtx.value = combinedCtx;

            loopOut += substitute(block, loopCtx);
        }

        result = match.prefix().str() + loopOut + match.suffix().str();
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
            stripped.append(text, pos);
            break;
        }

        stripped.append(text, pos, blockStart - pos);

        size_t nameStart = text.find_first_not_of(" \t\n\r", blockStart + 8);
        if (nameStart == std::string::npos)
            throw std::runtime_error("[TemplateError.Syntax] Malformed block name");

        size_t nameEnd = text.find_first_of(" \t\r\n%}", nameStart);
        std::string blockName = text.substr(nameStart, nameEnd - nameStart);

        size_t headerEnd = text.find("%}", nameEnd);
        if (headerEnd == std::string::npos)
            throw std::runtime_error("Malformed block header for: " + blockName);
        headerEnd += 2;

        size_t blockEnd = text.find("{% endblock %}", headerEnd);
        if (blockEnd == std::string::npos)
            throw std::runtime_error("Block not closed: " + blockName);

        blocks[blockName] = text.substr(headerEnd, blockEnd - headerEnd);
        pos = blockEnd + std::string("{% endblock %}").length();
    }

    return stripped;
}


void TemplateEngine::injectBlocks(std::string &parent,
                                  const std::unordered_map<std::string, std::string> &childBlocks)
{
    size_t pos = 0;
    while ((pos = parent.find("{% block", pos)) != std::string::npos) {
        size_t nameStart = parent.find_first_not_of(" \t\n\r", pos + 8);
        if (nameStart == std::string::npos) break;

        size_t nameEnd = parent.find_first_of(" \t\r\n%}", nameStart);
        std::string blockName = parent.substr(nameStart, nameEnd - nameStart);

        size_t headerEnd = parent.find("%}", nameEnd);
        if (headerEnd == std::string::npos) break;
        headerEnd += 2;

        size_t blockEnd = parent.find("{% endblock %}", headerEnd);
        if (blockEnd == std::string::npos)
            throw std::runtime_error("Block not closed: " + blockName);

        std::string replacement = childBlocks.count(blockName)
                                      ? childBlocks.at(blockName)
                                      : parent.substr(headerEnd, blockEnd - headerEnd);

        parent.replace(pos, blockEnd + 14 - pos, replacement);
        pos += replacement.length();
    }
}


std::string TemplateEngine::substitute(const std::string &text, const TemplateValue &ctx)
{
    std::regex variablePattern(R"(\{\{\s*(.*?)\s*\}\})");
    std::smatch match;
    std::string result;
    std::string::const_iterator searchStart(text.cbegin());

    while (std::regex_search(searchStart, text.cend(), match, variablePattern)) {
        result.append(searchStart, match[0].first);

        std::string expression = match[1].str(); // e.g., name|default("John")|upper
        std::istringstream ss(expression);
        std::string segment;
        std::vector<std::string> parts;

        while (std::getline(ss, segment, '|'))
            parts.push_back(trim(segment));

        if (parts.empty())
            throw std::runtime_error("Empty {{ }} expression");

        std::string key = parts[0];
        std::string value;
        std::string fallback;

        auto resolved = resolve(ctx, key);
        if (resolved) value = resolved->toString();

        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string &filter = parts[i];

            if (filter.starts_with("default(") && filter.back() == ')') {
                fallback = filter.substr(8, filter.size() - 9);
                if (!fallback.empty() && fallback.front() == '"' && fallback.back() == '"') {
                    fallback = fallback.substr(1, fallback.size() - 2);
                }

                if (value.empty()) value = fallback;
            } else {
                auto fit = luaFilters_.find(filter);
                if (fit != luaFilters_.end() && luaState_) {
                    lua_rawgeti(luaState_, LUA_REGISTRYINDEX, fit->second);
                    lua_pushstring(luaState_, value.c_str());
                    if (lua_pcall(luaState_, 1, 1, 0) == LUA_OK) {
                        if (lua_isstring(luaState_, -1)) value = lua_tostring(luaState_, -1);
                        lua_pop(luaState_, 1);
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
        searchStart = match.suffix().first;
    }

    result.append(searchStart, text.cend());
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


TemplateValue TemplateEngine::luaToTemplateValue(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    TemplateValue result;

    if (lua_istable(L, index)) {
        // Check if table is an array
        bool isArray = true;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (!lua_isinteger(L, -2)) {
                isArray = false;
                lua_pop(L, 2);
                break;
            }
            lua_pop(L, 1);
        }

        if (isArray) {
            TemplateList list;
            size_t len = lua_rawlen(L, index);
            for (size_t i = 1; i <= len; ++i) {
                lua_rawgeti(L, index, i);
                list.push_back(luaToTemplateValue(L, -1));
                lua_pop(L, 1);
            }
            result.value = list;
        } else {
            TemplateMap map;
            lua_pushnil(L);
            while (lua_next(L, index)) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    std::string key = lua_tostring(L, -2);
                    map[key] = luaToTemplateValue(L, -1);
                }
                lua_pop(L, 1);
            }
            result.value = map;
        }
    } else if (lua_isstring(L, index)) {
        result.value = lua_tostring(L, index);
    } else if (lua_isnumber(L, index)) {
        result.value = std::to_string(lua_tonumber(L, index));
    } else if (lua_isboolean(L, index)) {
        result.value = lua_toboolean(L, index) ? "true" : "false";
    } else {
        result.value = "[object]";
    }

    return result;
}


std::pair<bool, std::string> TemplateEngine::safeRenderFromString(
    const std::string &templateText,
    const TemplateValue &context)
{
    try {
        std::string result = renderFromString(templateText, context);
        return {true, result};
    } catch (const std::exception &e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Unknown rendering error."};
    }
}

std::optional<TemplateValue> TemplateEngine::resolve(const TemplateValue &ctx, const std::string &keyPath)
{
    if (!ctx.isMap()) return std::nullopt;

    TemplateValue current = ctx;
    std::istringstream ss(keyPath);
    std::string part;

    while (std::getline(ss, part, '.')) {
        if (!current.isMap()) return std::nullopt;
        auto it = current.asMap().find(part);
        if (it == current.asMap().end()) return std::nullopt;
        current = it->second;
    }

    return current;
}
