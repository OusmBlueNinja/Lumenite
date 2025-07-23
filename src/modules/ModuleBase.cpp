//
// Created by spenc on 7/23/2025.
//

#include "ModuleBase.h"

std::unordered_map<std::string, std::unique_ptr<LumeniteModule> > &LumeniteModule::registry()
{
    static std::unordered_map<std::string, std::unique_ptr<LumeniteModule> > mods;
    return mods;
}

void LumeniteModule::registerModule(std::unique_ptr<LumeniteModule> mod)
{
    registry()[mod->name()] = std::move(mod);
}

int LumeniteModule::load(const char *modname, lua_State *L)
{
    if (auto it = registry().find(modname); it != registry().end()) {
        return it->second->open(L);
    }
    return 0; // Not found
}
