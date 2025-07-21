//
// Created by spenc on 7/19/2025.
//

#pragma once
#include <string>

#define BOLD    "\033[1m"
#define WHITE   "\033[37m"
#define CYAN    "\033[36m"
#define MAGENTA "\033[35m"
#define BLUE    "\033[34m"
#define YELLOW  "\033[33m"
#define GREEN   "\033[32m"
#define RED     "\033[31m"
#define RESET   "\033[0m"


class ErrorHandler
{
public:
    static void fileMissing(const std::string &filepath);

    static void serverNotRunning();

    static void invalidScript(const std::string &error);
};
