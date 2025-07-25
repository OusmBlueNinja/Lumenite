//
// Created by spenc on 7/21/2025.
//

#pragma once
#include <string>
#include <filesystem>

class ProjectScaffolder
{
public:
    void createWorkspace(const std::string &name);

private:
    void createDir(const std::filesystem::path &path);

    void writeFile(const std::filesystem::path &path, const std::string &content);

    void log(const std::string &message, const std::string &prefix = "[+] ");
};
