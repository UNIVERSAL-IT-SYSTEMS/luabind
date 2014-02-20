// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef LUABIND_LUA_INCLUDE_HPP_INCLUDED
#define LUABIND_LUA_INCLUDE_HPP_INCLUDED

#ifndef LUABIND_CPLUSPLUS_LUA
extern "C"
{
#endif

    #include "lua.h"
    #include "lauxlib.h"

#ifndef LUABIND_CPLUSPLUS_LUA
}
#endif

#if LUA_VERSION_NUM < 502
# define lua_compare(L, index1, index2, fn) fn(L, index1, index2)
# define LUA_OPEQ lua_equal
# define LUA_OPLT lua_lessthan
# define lua_rawlen lua_objlen
# define lua_pushglobaltable(L) lua_pushvalue(L, LUA_GLOBALSINDEX)
# define lua_getuservalue lua_getfenv
# define lua_setuservalue lua_setfenv
# define luaL_tolstring lua_tolstring
# define LUA_OK 0

inline int lua_absindex(lua_State* L, int i)
{
    if (i < 0 && i > LUA_REGISTRYINDEX)
    i += lua_gettop(L) + 1;
    return i;
}

inline void lua_rawsetp(lua_State* L, int i, void* p)
{
    int abs_i = lua_absindex(L, i);
    luaL_checkstack(L, 1, "not enough stack slots");
    lua_pushlightuserdata(L, (void*)p);
    lua_insert(L, -2);
    lua_rawset(L, abs_i);
}

inline void lua_rawgetp(lua_State* L, int i, void* p)
{
    int abs_i = lua_absindex(L, i);
    lua_pushlightuserdata(L, (void*)p);
    lua_rawget(L, abs_i);
}


#endif

#endif // LUABIND_LUA_INCLUDE_HPP_INCLUDED
