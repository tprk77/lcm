

#ifndef TPRK77_LUA_REF_HELPER_H
#define TPRK77_LUA_REF_HELPER_H

#include "lua.h"

LUALIB_API int luaX_ref(lua_State *L, int t, int l);
LUALIB_API void luaX_unref(lua_State *L, int t, int l, int ref);

#endif
