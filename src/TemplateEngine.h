#pragma once
#include <string>
#include <unordered_map>

class TemplateEngine {
public:
    static std::string loadTemplate(const std::string& filename);
    static std::string render(const std::string& tmpl, const std::unordered_map<std::string, std::string>& context);
};


