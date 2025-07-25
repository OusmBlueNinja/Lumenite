#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include "utils/Version.h"
#include "ErrorHandler.h" // for colors
#include <string>
#include <iostream>

static void printHelp()
{
    std::cout << CYAN << R"(
Lumenite - Lightweight Lua+HTTP Server
)" << RESET << R"(
Usage:
  lumenite              Run app.lua
  lumenite <script>     Run specified Lua script
  lumenite new <name>   Create a new project

Options:
  -h, --help            Show this help message
  -v, --version         Print Lumenite version

Examples:
  lumenite app.lua
  lumenite new mysite
)" << std::endl;
}

int main(int argc, char *argv[])
{
    std::string scriptPath = "app.lua";

    if (argc >= 2) {
        const std::string arg1 = argv[1];

        if (arg1 == "new") {
            if (argc < 3) {
                std::cerr << RED << "[Error] Project name missing after 'new'" << RESET << "\n\n";
                printHelp();
                return 1;
            }

            ProjectScaffolder scaffolder;
            scaffolder.createWorkspace(argv[2]);
            return 0;
        }

        if (arg1 == "-h" || arg1 == "--help") {
            printHelp();
            return 0;
        }

        if (arg1 == "-v" || arg1 == "--version") {
            printVersion();
            return 0;
        }

        if (arg1.starts_with("-")) {
            std::cerr << RED << "[Error] Unknown flag: " << arg1 << RESET << "\n\n";
            printHelp();
            return 1;
        }

        // Assume it's a script
        scriptPath = arg1;
    }

    LumeniteApp app;
    return app.loadScript(scriptPath);
}
