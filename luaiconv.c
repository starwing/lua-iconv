/*
 * luaiconv - Performs character set conversions in Lua
 * (c) 2005-11 Alexandre Erwin Ittner <alexandre@ittner.com.br>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * If you use this package in a product, an acknowledgment in the product
 * documentation would be greatly appreciated (but it is not required).
 *
 *
 */

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>

#include <iconv.h>
#include <errno.h>
#include <string.h> /* for strerror */

#define LIB_NAME                "iconv"
#define LIB_VERSION             LIB_NAME " 7"
#define ICONV_TYPENAME          "iconv_t"

#if LUA_VERSION_NUM < 501
#  error "Unsuported Lua version. You must use Lua >= 5.1"
#endif

#if LUA_VERSION_NUM < 502 
#  define luaL_setfuncs(L,l,nups)       luaI_openlib((L),NULL,(l),(nups))
#  define luaL_newlibtable(L,l)	\
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)
#  define luaL_newlib(L,l) \
    (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))
#endif

static int Liconv_new(lua_State *L) {
    const char *tocode = luaL_checkstring(L, 1);
    const char *fromcode = luaL_checkstring(L, 2);
    iconv_t cd = iconv_open(tocode, fromcode);
    if (cd != (iconv_t)(-1)) {
        *(iconv_t*)lua_newuserdata(L, sizeof(iconv_t*)) = cd;
        luaL_getmetatable(L, ICONV_TYPENAME);
        lua_setmetatable(L, -2);
        return 1;
    }
    lua_pushnil(L);         /* error */
    lua_pushstring(L, strerror(errno));
    return 2;
}

static int Liconv_close(lua_State *L) {
    iconv_t *cd = (iconv_t*)luaL_checkudata(L, 1, ICONV_TYPENAME);
    if (*cd != NULL && iconv_close(*cd) == 0) {
        /* Mark the pointer as freed, preventing interpreter crashes
           if the user forces __gc to be called twice. */
        *cd = NULL;
    }
    return 0;
}

static int Liconv_convert(lua_State *L, iconv_t cd, const char *inbuf, size_t ibleft) {
    luaL_Buffer B;
    size_t obleft, ret;
    char *outbuf;

    if (cd == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid iconv handle");
        return 2;
    }

    luaL_buffinit(L, &B);
    outbuf = luaL_prepbuffer(&B);
    obleft = LUAL_BUFFERSIZE;
    do {
        size_t size = obleft;
        ret = iconv(cd, &inbuf, &ibleft, &outbuf, &obleft);
        luaL_addsize(&B, size - obleft);
        if (ret == (size_t)(-1)) {
            if (errno == E2BIG) {
                outbuf = luaL_prepbuffer(&B);
                obleft = LUAL_BUFFERSIZE;
            } else {
                luaL_pushresult(&B);
                lua_pushnil(L);
                lua_pushstring(L, strerror(errno));
                lua_pushvalue(L, -3);
                lua_pushinteger(L, errno);
                return 4;
            }
        }
    } while (ret != (size_t) 0);

    iconv(cd, NULL, NULL, NULL, NULL); /* set iconv to initial state */
    luaL_pushresult(&B);
    return 1;   /* Done */
}

static int Liconv(lua_State *L) {
    iconv_t cd = *(iconv_t*)luaL_checkudata(L, 1, ICONV_TYPENAME);
    size_t ibleft;
    const char *inbuf = luaL_checklstring(L, 2, &ibleft);
    return Liconv_convert(L, cd, inbuf, ibleft);
}

static int Liconv_closure(lua_State *L) {
    iconv_t cd = *(iconv_t*)lua_touserdata(L, lua_upvalueindex(1));
    size_t ibleft;
    const char *inbuf = luaL_checklstring(L, 1, &ibleft);
    return Liconv_convert(L, cd, inbuf, ibleft);
}

static int Liconv_open(lua_State *L) {
    int rets;
    if (lua_type(L, 1) == LUA_TTABLE)
        lua_remove(L, 1);
    if ((rets = Liconv_new(L)) == 1)
        lua_pushcclosure(L, Liconv_closure, 1);
    return rets;
}

#if defined(iconvlist) && !defined(HAS_ICONVLIST)
#  define HAS_ICONVLIST
#endif
#ifdef HAS_ICONVLIST /* a GNU extension? */

static int push_one(unsigned int cnt, const char *const *names, void *data) {
    lua_State *L = (lua_State*) data;
    int n = (int) lua_tonumber(L, -1);
    int i;

    /* Stack: <tbl> n */
    lua_remove(L, -1);    
    for (i = 0; i < cnt; i++) {
        /* Stack> <tbl> */
        lua_pushnumber(L, n++);
        lua_pushstring(L, names[i]);
        /* Stack: <tbl> n <str> */
        lua_settable(L, -3);
    }
    lua_pushnumber(L, n);
    /* Stack: <tbl> n */
    return 0;   
}

static int Liconvlist(lua_State *L) {
    lua_newtable(L);
    lua_pushnumber(L, 1);

    /* Stack:   <tbl> 1 */
    iconvlist(push_one, (void*) L);

    /* Stack:   <tbl> n */
    lua_remove(L, -1);
    return 1;
}

#endif

static const luaL_Reg iconv_funcs[] = {
    { "new",    Liconv_new },
    { "iconv",  Liconv },
    { "delete", Liconv_close },
#ifdef HAS_ICONVLIST
    { "list",   Liconvlist },
#endif
    { NULL, NULL }
};

LUALIB_API int luaopen_iconv(lua_State *L) {
    luaL_newlib(L, iconv_funcs);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, Liconv_open);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);

    lua_pushinteger(L, EILSEQ);
    lua_setfield(L, -2, "ERROR_INCOMPLETE");
    lua_pushinteger(L, EINVAL);
    lua_setfield(L, -2, "ERROR_INVALID");

    lua_pushliteral(L, "VERSION");
    lua_pushstring(L, LIB_VERSION);
    lua_settable(L, -3);

    luaL_newmetatable(L, ICONV_TYPENAME);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, Liconv_close);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    return 1;
}
/*
 * cc: lua='lua52' flags+='-s -O2 -Wall -pedantic -mdll -Id:/$lua/include' libs+='d:/$lua/$lua.dll -liconv'
 * cc: flags+='-DLUA_BUILD_AS_DLL -DLUA_LIB' output='iconv.dll'
 * cc: run='$lua test_iconv.lua'
 */
