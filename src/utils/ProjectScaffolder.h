//
// Created by spenc on 7/21/2025.
//

#pragma once
#include <string>
#include <filesystem>
#include <vector>


namespace fs = std::filesystem;

class ProjectScaffolder
{
public:
    void createWorkspace(const std::string &name, const std::vector<std::string> &args = {});


    std::string projectName;
    fs::path rootPath;

private:
    bool force = false;
    bool deleteExisting = false;


    void createDir(const std::filesystem::path &path) const;

    void writeFile(const std::filesystem::path &path, const std::string &content) const;

    [[nodiscard]] std::string colorizePath(const std::string &pathStr, const std::string &projectName) const;

    void log(const std::string &action, const std::string &text) const;
};
