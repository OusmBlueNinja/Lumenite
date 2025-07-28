#pragma once

#include <string>
#include <memory>
#include <unordered_map>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}


struct LumenitePluginMeta
{
    const char *name;
    const char *version;
    const char *engine_version;

    int (*luaopen)(lua_State *);
};

class LumeniteModule
{
public:
    virtual ~LumeniteModule() = default;

    [[nodiscard]] virtual const std::string &name() const = 0;

    virtual int open(lua_State *L) = 0;

    [[nodiscard]] virtual int (*getLuaOpen() const)(lua_State *) = 0;

    static void registerModule(std::unique_ptr<LumeniteModule> mod);

    static int load(const char *modname, lua_State *L);

    static void loadPluginsFromDirectory();

private:
    static std::unordered_map<std::string, std::unique_ptr<LumeniteModule> > &registry();
};
