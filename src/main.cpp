#include "LumeniteApp.h"
#include "utils/ProjectScaffolder.h"
#include <string>


int main(int argc, char *argv[])
{
    std::string scriptPath = "app.lua";

    if (argc >= 2) {
        scriptPath = argv[1];
    }

    if (argc >= 3) {
        std::string flag = argv[1];

        if (flag == "-n" || flag == "--new" || flag == "--init") {
            ProjectScaffolder::createWorkspace(argv[2]);
            return 0;
        }
    }

    LumeniteApp app;
    return app.loadScript(scriptPath);
}
