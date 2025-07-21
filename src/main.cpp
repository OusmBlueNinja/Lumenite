#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include <string>
#include <iostream>

static void printHelp()
{
    std::cout << R"(Lumenite - Lightweight Lua+HTTP Server

Usage:
  lumenite             Run app.lua
  lumenite <script>   Run specified Lua script
  lumenite -n <name>  Create a new project (alias: --new, --init)

Examples:
  lumenite app.lua
  lumenite -n mysite
)";
}

int main(const int argc, char *argv[])
{
    std::string scriptPath = "app.lua";

    if (argc >= 2) {
        const std::string arg1 = argv[1];

        if (arg1 == "-n" || arg1 == "--new" || arg1 == "--init") {
            if (argc < 3) {
                std::cerr << "[Error] Project name missing after '" << arg1 << "'\n\n";
                printHelp();
                return 1;
            }
            ProjectScaffolder::createWorkspace(argv[2]);
            return 0;
        }

        if (arg1 == "-h" || arg1 == "--help") {
            printHelp();
            return 0;
        }

        if (arg1.rfind("-", 0) != 0) {
            scriptPath = arg1;
        } else {
            std::cerr << "[Error] Unknown flag: " << arg1 << "\n\n";
            printHelp();
            return 1;
        }
    }

    LumeniteApp app;
    return app.loadScript(scriptPath);
}
