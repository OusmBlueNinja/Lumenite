#include "TemplateEngine.h"
#include <fstream>
#include <sstream>
#include <regex>

std::string TemplateEngine::loadTemplate(const std::string &filename)
{
    std::ifstream file("templates/" + filename);
    if (!file.is_open())
        return "Template not found: " + filename;
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

std::string TemplateEngine::render(
    const std::string &tmpl,
    const std::unordered_map<std::string, std::string> &context)
{
    std::string result = tmpl;
    for (auto &[key,val]: context) {
        std::regex tag("\\{\\{\\s*" + key + "\\s*\\}\\}");
        result = std::regex_replace(result, tag, val);
    }
    return result;
}
