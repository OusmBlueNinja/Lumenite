#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include "utils/Version.h"
#include "ErrorHandler.h" // for colors
#include <string>
#include <iostream>
#include "utils/LumenitePackageManager.h"


static void printHelp()
{
    std::cout << CYAN << R"(
Lumenite - Lightweight Lua+HTTP Server
)" << RESET << R"(
Usage:
  lumenite                  Run app.lua
  lumenite <script>         Run specified Lua script
  lumenite new <name>       Create a new project
  lumenite package <cmd>    Manage plugin packages

Options:
  -h, --help                Show this help message
  -v, --version             Print Lumenite version

Package Commands:
  lumenite package get <name>       Download a plugin from the registry
  lumenite package remove <name>    Uninstall a plugin
  lumenite package update <name>    Update a plugin

Examples:
  lumenite app.lua
  lumenite new mysite
  lumenite package get HelloPlugin
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

            std::string projectName = argv[2];
            std::vector<std::string> scaffoldArgs;
            for (int i = 3; i < argc; ++i)
                scaffoldArgs.emplace_back(argv[i]);

            ProjectScaffolder scaffolder;
            scaffolder.createWorkspace(projectName, scaffoldArgs);
            return 0;
        }

        if (arg1 == "package") {
            if (argc < 3) {
                std::cout << CYAN << "[~] Usage  : " << RESET << "lumenite package <command> <name>\n"
                        << "Available commands:\n"
                        << "  " << BOLD << "get <name>    " << RESET <<
                        "Download and install a plugin from the registry\n"
                        << "  " << BOLD << "remove <name> " << RESET << "Uninstall a plugin\n"
                        << "  " << BOLD << "update <name> " << RESET << "Update a plugin to the latest version\n";
                return 0;
            }

            std::vector<std::string> pkgArgs;
            for (int i = 2; i < argc; ++i)
                pkgArgs.emplace_back(argv[i]);

            LumenitePackageManager::run(pkgArgs);
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
