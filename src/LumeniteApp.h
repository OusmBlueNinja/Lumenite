#pragma once
#include <string>
#include "Router.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

class LumeniteApp {
public:
    LumeniteApp();
    ~LumeniteApp();
    void loadScript(const std::string& path);

private:
    lua_State* L;

    void exposeBindings();
    static int lua_get(lua_State* L);
    static int lua_listen(lua_State* L);
};
