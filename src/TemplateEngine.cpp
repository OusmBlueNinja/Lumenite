// TemplateEngine.cpp
#include "TemplateEngine.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

TemplateEngine::TemplateEngine(const Config& config) : config_(config) {}

std::string TemplateEngine::render(const std::string& templateName,
                                  const std::unordered_map<std::string, std::string>& context) {
    try {
        // Check compiled cache first
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = compiledCache_.find(templateName);
            if (it != compiledCache_.end() && it->second.isValid) {
                if (!config_.enableFileWatching ||
                    !isFileNewer(templateName, it->second.lastModified)) {
                    return substitute(it->second.content, context);
                }
            }
        }

        // Load and process template
        std::string templateContent = loadTemplateInstance(templateName);
        std::vector<std::string> includeStack;

        // Process includes with cycle detection
        std::string processedContent = processIncludes(templateContent, includeStack);

        // Handle extends
        std::string parentFile;
        std::string workingContent = processedContent;

        if (workingContent.substr(0, 10) == "{% extends") {
            size_t quoteStart = workingContent.find('"');
            if (quoteStart != std::string::npos) {
                size_t quoteEnd = workingContent.find('"', quoteStart + 1);
                if (quoteEnd != std::string::npos) {
                    parentFile = workingContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    size_t extendsEnd = workingContent.find("%}", quoteEnd);
                    if (extendsEnd != std::string::npos) {
                        workingContent = workingContent.substr(extendsEnd + 2);
                    }
                }
            }
        }

        // Extract blocks from child template
        std::unordered_map<std::string, std::string> childBlocks;
        std::string childBody = extractAndProcessBlocks(workingContent, childBlocks);

        std::string result;
        if (!parentFile.empty()) {
            // Load parent template
            std::string parentContent = loadTemplateInstance(parentFile);
            std::vector<std::string> parentIncludeStack;
            std::string processedParent = processIncludes(parentContent, parentIncludeStack);

            // Inject child blocks into parent
            injectBlocks(processedParent, childBlocks);
            result = processedParent;
        } else {
            result = childBody;
        }

        // Cache the compiled template
        if (config_.enableCache) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (compiledCache_.size() >= config_.maxCacheSize) {
                evictLRU();
            }
            compiledCache_[templateName] = {
                result,
                std::chrono::steady_clock::now(),
                true
            };
        }

        // Final variable substitution
        return substitute(result, context);

    } catch (const std::exception& e) {
        std::cerr << "Template rendering error: " << e.what() << std::endl;
        return "";
    }
}

std::string TemplateEngine::loadTemplate(const std::string& filename) {
    std::string fullPath = getFullPath(filename);

    // Check cache first
    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = templateCache_.find(fullPath);
        if (it != templateCache_.end() && it->second.isValid) {
            if (!config_.enableFileWatching ||
                !isFileNewer(fullPath, it->second.lastModified)) {
                return it->second.content;
            }
        }
    }

    // Load from file
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Template not found: " + filename);
    }

    // More efficient file reading
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content;
    content.reserve(fileSize);
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());

    // Cache the loaded template
    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (templateCache_.size() >= config_.maxCacheSize) {
            evictLRU();
        }
        templateCache_[fullPath] = {
            content,
            std::chrono::steady_clock::now(),
            true
        };
    }

    return content;
}

// Static methods for backward compatibility
std::string TemplateEngine::render(const std::string& templateText,
                                   const std::unordered_map<std::string, std::string>& context) {
    // Static method for backward compatibility - creates a temporary engine
    TemplateEngine tempEngine(Config{});

    // Process the template text directly (simplified for static use)
    std::vector<std::string> includeStack;
    std::string processedContent = templateText;

    // Extract blocks
    std::unordered_map<std::string, std::string> childBlocks;
    std::string result = tempEngine.extractAndProcessBlocks(processedContent, childBlocks);

    // Final variable substitution
    return tempEngine.substitute(result, context);
}

std::string TemplateEngine::loadTemplate(const std::string& filename) {
    // Static method for backward compatibility
    TemplateEngine tempEngine(Config{});
    return tempEngine.loadTemplateInstance(filename);
}

// Instance method (renamed to avoid conflict)
std::string TemplateEngine::loadTemplateInstance(const std::string& filename) {
    std::string fullPath = getFullPath(filename);

    // Check cache first
    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = templateCache_.find(fullPath);
        if (it != templateCache_.end() && it->second.isValid) {
            if (!config_.enableFileWatching ||
                !isFileNewer(fullPath, it->second.lastModified)) {
                return it->second.content;
            }
        }
    }

    // Load from file
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Template not found: " + filename);
    }

    // More efficient file reading
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content;
    content.reserve(fileSize);
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());

    // Cache the loaded template
    if (config_.enableCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (templateCache_.size() >= config_.maxCacheSize) {
            evictLRU();
        }
        templateCache_[fullPath] = {
            content,
            std::chrono::steady_clock::now(),
            true
        };
    }

    return content;
}

std::string TemplateEngine::processIncludes(const std::string& text,
                                           std::vector<std::string>& includeStack) {
    std::string result;
    result.reserve(text.length() * 2); // Reserve space to avoid reallocations

    size_t pos = 0;
    while (pos < text.length()) {
        size_t includePos = text.find("{% include", pos);
        if (includePos == std::string::npos) {
            result.append(text, pos, text.length() - pos);
            break;
        }

        // Add text before include
        result.append(text, pos, includePos - pos);

        // Find the filename
        size_t quoteStart = text.find('"', includePos);
        if (quoteStart == std::string::npos) {
            throw std::runtime_error("Malformed include directive");
        }

        size_t quoteEnd = text.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos) {
            throw std::runtime_error("Malformed include directive");
        }

        std::string filename = text.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        // Check for circular includes
        if (std::find(includeStack.begin(), includeStack.end(), filename) != includeStack.end()) {
            throw std::runtime_error("Circular include detected: " + filename);
        }

        // Load and process included template
        includeStack.push_back(filename);
        std::string includedContent = loadTemplateInstance(filename);
        std::string processedInclude = processIncludes(includedContent, includeStack);
        includeStack.pop_back();

        result.append(processedInclude);

        // Find end of include directive
        size_t endPos = text.find("%}", quoteEnd);
        if (endPos == std::string::npos) {
            throw std::runtime_error("Malformed include directive");
        }

        pos = endPos + 2;
    }

    return result;
}

std::string TemplateEngine::extractAndProcessBlocks(
    const std::string& text,
    std::unordered_map<std::string, std::string>& blocks) {

    std::string stripped;
    stripped.reserve(text.length());

    size_t pos = 0;
    while (pos < text.length()) {
        size_t blockStart = text.find("{% block", pos);
        if (blockStart == std::string::npos) {
            stripped.append(text, pos, text.length() - pos);
            break;
        }

        // Add text before block
        stripped.append(text, pos, blockStart - pos);

        // Extract block name
        size_t nameStart = blockStart + 8;
        while (nameStart < text.length() && (text[nameStart] == ' ' || text[nameStart] == '\t')) {
            nameStart++;
        }

        size_t nameEnd = nameStart;
        while (nameEnd < text.length() &&
               text[nameEnd] != ' ' && text[nameEnd] != '\t' &&
               text[nameEnd] != '%' && text[nameEnd] != '}') {
            nameEnd++;
        }

        if (nameStart >= nameEnd) {
            throw std::runtime_error("Invalid block name");
        }

        std::string blockName = text.substr(nameStart, nameEnd - nameStart);

        // Find end of block header
        size_t headerEnd = text.find("%}", nameEnd);
        if (headerEnd == std::string::npos) {
            throw std::runtime_error("Malformed block header");
        }
        headerEnd += 2;

        // Find endblock
        size_t blockEnd = text.find("{% endblock %}", headerEnd);
        if (blockEnd == std::string::npos) {
            throw std::runtime_error("Block not closed: " + blockName);
        }

        // Extract block content
        std::string blockContent = text.substr(headerEnd, blockEnd - headerEnd);
        blocks[blockName] = blockContent;

        pos = blockEnd + 14; // Length of "{% endblock %}"
    }

    return stripped;
}

void TemplateEngine::injectBlocks(
    std::string& parent,
    const std::unordered_map<std::string, std::string>& childBlocks) {

    size_t pos = 0;
    while (pos < parent.length()) {
        size_t blockStart = parent.find("{% block", pos);
        if (blockStart == std::string::npos) {
            break;
        }

        // Extract block name (similar to extractAndProcessBlocks)
        size_t nameStart = blockStart + 8;
        while (nameStart < parent.length() &&
               (parent[nameStart] == ' ' || parent[nameStart] == '\t')) {
            nameStart++;
        }

        size_t nameEnd = nameStart;
        while (nameEnd < parent.length() &&
               parent[nameEnd] != ' ' && parent[nameEnd] != '\t' &&
               parent[nameEnd] != '%' && parent[nameEnd] != '}') {
            nameEnd++;
        }

        std::string blockName = parent.substr(nameStart, nameEnd - nameStart);

        // Find block boundaries
        size_t headerEnd = parent.find("%}", nameEnd) + 2;
        size_t blockEnd = parent.find("{% endblock %}", headerEnd);

        if (blockEnd == std::string::npos) {
            throw std::runtime_error("Block not closed in parent: " + blockName);
        }

        // Choose replacement content
        std::string replacement;
        auto it = childBlocks.find(blockName);
        if (it != childBlocks.end()) {
            replacement = it->second;
        } else {
            // Keep parent content
            replacement = parent.substr(headerEnd, blockEnd - headerEnd);
        }

        // Replace the entire block
        parent.replace(blockStart, blockEnd + 14 - blockStart, replacement);

        // Update position
        pos = blockStart + replacement.length();
    }
}

std::string TemplateEngine::substitute(
    const std::string& text,
    const std::unordered_map<std::string, std::string>& context) {

    std::string result;
    result.reserve(text.length() * 1.5); // Reserve space for expansions

    size_t pos = 0;
    while (pos < text.length()) {
        size_t varStart = text.find("{{", pos);
        if (varStart == std::string::npos) {
            result.append(text, pos, text.length() - pos);
            break;
        }

        // Add text before variable
        result.append(text, pos, varStart - pos);

        size_t varEnd = text.find("}}", varStart);
        if (varEnd == std::string::npos) {
            throw std::runtime_error("Malformed variable syntax");
        }

        // Extract and trim variable name
        std::string varName = text.substr(varStart + 2, varEnd - varStart - 2);
        varName = trim(varName);

        // Substitute variable
        auto it = context.find(varName);
        if (it != context.end()) {
            result.append(it->second);
        }
        // If variable not found, replace with empty string (Django behavior)

        pos = varEnd + 2;
    }

    return result;
}

std::string TemplateEngine::render(const std::string& templateText,
                                   const std::unordered_map<std::string, std::string>& context) {
    // Static method for backward compatibility - creates a temporary engine
    TemplateEngine tempEngine(Config{});

    // Process the template text directly (simplified for static use)
    std::vector<std::string> includeStack;
    std::string processedContent = templateText;

    // Extract blocks
    std::unordered_map<std::string, std::string> childBlocks;
    std::string result = tempEngine.extractAndProcessBlocks(processedContent, childBlocks);

    // Final variable substitution
    return tempEngine.substitute(result, context);
}

std::string TemplateEngine::loadTemplate(const std::string& filename) {
    // Static method for backward compatibility
    TemplateEngine tempEngine(Config{});
    return tempEngine.loadTemplate(filename);
}

std::string TemplateEngine::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

bool TemplateEngine::isFileNewer(const std::string& filename,
                                const std::chrono::steady_clock::time_point& cacheTime) {
    try {
        auto fileTime = std::filesystem::last_write_time(filename);
        auto systemTime = std::chrono::file_clock::to_sys(fileTime);
        auto steadyTime = std::chrono::steady_clock::now() -
                         (std::chrono::system_clock::now() - systemTime);
        return steadyTime > cacheTime;
    } catch (...) {
        return true; // Assume file is newer if we can't check
    }
}

void TemplateEngine::evictLRU() {
    // Simple LRU eviction - remove oldest half of cache
    size_t targetSize = config_.maxCacheSize / 2;

    if (templateCache_.size() > targetSize) {
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> items;
        for (const auto& [key, entry] : templateCache_) {
            items.emplace_back(key, entry.lastModified);
        }

        std::sort(items.begin(), items.end(),
                 [](const auto& a, const auto& b) {
                     return a.second < b.second;
                 });

        for (size_t i = 0; i < items.size() - targetSize; ++i) {
            templateCache_.erase(items[i].first);
        }
    }

    // Same for compiled cache
    if (compiledCache_.size() > targetSize) {
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> items;
        for (const auto& [key, entry] : compiledCache_) {
            items.emplace_back(key, entry.lastModified);
        }

        std::sort(items.begin(), items.end(),
                 [](const auto& a, const auto& b) {
                     return a.second < b.second;
                 });

        for (size_t i = 0; i < items.size() - targetSize; ++i) {
            compiledCache_.erase(items[i].first);
        }
    }
}

std::string TemplateEngine::getFullPath(const std::string& filename) {
    return config_.templateDir + filename;
}

void TemplateEngine::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    templateCache_.clear();
    compiledCache_.clear();
}

void TemplateEngine::setTemplateDir(const std::string& dir) {
    config_.templateDir = dir;
    if (!config_.templateDir.empty() && config_.templateDir.back() != '/') {
        config_.templateDir += '/';
    }
    clearCache(); // Clear cache when template directory changes
}