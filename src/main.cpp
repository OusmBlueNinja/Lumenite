#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include <string>
#include <iostream>
#include "utils/Version.h"

static void printHelp()
{
    std::cout << R"(Lumenite - Lightweight Lua+HTTP Server

Usage:
  lumenite             Run app.lua
  lumenite <script>   Run specified Lua script
  lumenite -n <name>  Create a new project (alias: --new, --init)

Options:
  -h, --help           Show this help message
  -v, --version        Print Lumenite version

Examples:
  lumenite app.lua
  lumenite -n mysite
)";
}

static void printVersion()
{
    std::cout << "Lumenite version: " << LUMENITE_RELEASE_VERSION << "\n";
    std::cout << "Build: " << getLumeniteVersion() << "\n";
}

int main(int argc, char *argv[])
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

        if (arg1 == "-v" || arg1 == "--version") {
            printVersion();
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
