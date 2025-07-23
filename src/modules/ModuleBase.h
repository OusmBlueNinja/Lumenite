//
// Created by spenc on 7/23/2025.
//

#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <lua.hpp>

class LumeniteModule {
public:
    virtual ~LumeniteModule() = default;

    // Return the full module name, e.g. "lumenite.db"
    virtual const char* name() const = 0;

    // Register function (like luaopen_LumeniteXYZ)
    virtual int open(lua_State* L) = 0;

    // Registry system
    static void registerModule(std::unique_ptr<LumeniteModule> mod);
    static int load(const char* modname, lua_State* L);

private:
    static std::unordered_map<std::string, std::unique_ptr<LumeniteModule>>& registry();
};
