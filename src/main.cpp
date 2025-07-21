#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include <string>
#include <iostream>

int main(const int argc, char *argv[])
{
    std::string scriptPath = "app.lua";

    if (argc >= 3) {
        if (const std::string flag = argv[1]; flag == "-n" || flag == "--new" || flag == "--init") {
            ProjectScaffolder::createWorkspace(argv[2]);
            return 0;
        }
    }

    if (argc >= 2) {
        if (const std::string arg1 = argv[1]; arg1.rfind('-', 0) != 0) {
            scriptPath = arg1;
        }
    }

    LumeniteApp app;
    return app.loadScript(scriptPath);
}
