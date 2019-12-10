#ifndef LUA_DEMO_HPP
#define LUA_DEMO_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#  pragma once
#endif  // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <sol.hpp>

#include <export_define.h>

#define SOL_ALL_SAFETIES_ON 1
// forward declare as a C struct
// so a pointer to lua_State can be part of a signature
extern "C" {
struct lua_State;
LUAMOD_API int luaopen_luapb(lua_State* L);
}

#endif