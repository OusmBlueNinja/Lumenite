//
// Created by spenc on 7/21/2025.
//

#ifndef LUMENITESAFE_H
#define LUMENITESAFE_H

extern "C"
{
#include <lua.h>
}

namespace LumeniteSafe
{
    int luaopen(lua_State *L);
};

#endif // LUMENITESAFE_H
