#include "TemplateEngine.h"
#include <fstream>
#include <sstream>
#include <regex>

std::string TemplateEngine::loadTemplate(const std::string& filename) {
    std::ifstream file("templates/" + filename);
    if (!file.is_open()) return "Template not found: " + filename;

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string TemplateEngine::render(const std::string& tmpl, const std::unordered_map<std::string, std::string>& context) {
    std::string result = tmpl;

    for (const auto& [key, value] : context) {
        std::regex tag("\\{\\{\\s*" + key + "\\s*\\}\\}");
        result = std::regex_replace(result, tag, value);
    }

    return result;
}
