//
// Created by spenc on 7/19/2025.
//

#pragma once
#include <string>

class ErrorHandler
{
public:
    static void fileMissing(const std::string &filepath);

    static void serverNotRunning();

    static void invalidScript(const std::string &error);
};
