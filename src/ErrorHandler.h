//
// Created by spenc on 7/19/2025.
//

#pragma once
#include <string>


// Color codes
#define RESET     "\033[0m"
#define BOLD      "\033[1m"
#define DIM       "\033[2m"
#define RED       "\033[31m"
#define GREEN     "\033[32m"
#define YELLOW    "\033[33m"
#define BLUE      "\033[34m"
#define MAGENTA   "\033[35m"
#define CYAN      "\033[36m"
#define WHITE     "\033[37m"
#define GRAY      "\033[38;5;240m"
#define PURPLE    "\033[38;5;99m"
#define LBLUE     "\033[38;5;81m"
#define LGREEN    "\033[38;5;120m"
#define MOON1     "\033[38;5;141m"
#define MOON2     "\033[38;5;135m"
#define MOON4     "\033[38;5;63m"
#define MOON5     "\033[38;5;111m"
#define MOON6     "\033[38;5;250m"

class ErrorHandler
{
public:
    static void fileMissing(const std::string &filepath);

    static void serverNotRunning();

    static void invalidScript(const std::string &error);
};
