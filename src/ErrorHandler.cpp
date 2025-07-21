//
// Created by spenc on 7/19/2025.
//

#include "ErrorHandler.h"
#include <iostream>

void ErrorHandler::fileMissing(const std::string &filepath)
{
    std::cout << "\033[1;31m[Error]\033[0m Lua script not found: \033[1m" << filepath << "\033[0m\n"
            << "Make sure the file path is correct and the script exists.\n"
            << "Example usage:\n\n"
            << "  \033[36m./lumenite app.lua\033[0m\n\n";
}

void ErrorHandler::serverNotRunning()
{
    std::cout << "\n\033[1;33m[Lumenite Warning]\033[0m You haven't started the server yet.\n\n"
            << "\033[1mTo start the server and define routes, add the following:\033[0m\n\n"
            << "\033[36m  app:get(\"/\", function(req)\n"
            << "      return \"Hello, world!\"\n"
            << "  end)\n\n"
            << "  app:listen(8080)\033[0m\n\n"
            << "This will bind the server to \033[1;32mhttp://localhost:8080\033[0m and begin handling requests.\n\n"
            << "\033[90mTip: You can also use app:post, app:put, app:delete for other methods.\033[0m\n";
}

void ErrorHandler::invalidScript(const std::string &error)
{
    std::cout << RED << "[Lua Error] " << error << RESET << "\n";
}
