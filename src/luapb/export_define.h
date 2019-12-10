#pragma once

#ifndef LUAMOD_API
#  ifdef WIN32
#    define LUAMOD_API __declspec(dllexport)
#  else
#    define LUAMOD_API
#  endif
#endif  // LUAMOD_API