#include "LumeniteApp.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

void enableAnsiColors()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
#endif
}

int main(int argc, char *argv[])
{
    enableAnsiColors();

    std::string scriptPath = "app.lua"; // default

    if (argc >= 2) {
        scriptPath = argv[1];
    }

    LumeniteApp app;
    app.loadScript(scriptPath);
    return 0;
}
