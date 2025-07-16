//
// Created by spenc on 7/16/2025.
//
#include "LumeniteApp.h"
#ifdef _WIN32
#include <windows.h>
#endif

void enableAnsiColors() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
#endif
}


int main() {
    enableAnsiColors();
    LumeniteApp app;
    app.loadScript("app.lua");
    return 0;
}
