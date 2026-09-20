/* 用内联 lua54 源码构建的最小 Lua 执行器；手动设置 arg 表 */
#include <stdio.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main(int argc, char** argv) {
    if (argc < 2) { printf("用法: runlua <file.lua> [args...]\n"); return 1; }
    lua_State* L = luaL_newstate();
    if (!L) { printf("lua 初始化失败\n"); return 1; }
    luaL_openlibs(L);
    /* Lua 约定: arg[0]=脚本名, arg[1..]=实参; 本程序 argv[1] 是脚本 */
    if (argc >= 2) {
        lua_newtable(L);
        lua_pushstring(L, argv[1]);
        lua_seti(L, -2, 0);
        for (int i = 2; i < argc; i++) { lua_pushstring(L, argv[i]); lua_seti(L, -2, i - 1); }
        lua_setglobal(L, "arg");
    }
    int rc = luaL_dofile(L, argv[1]);
    if (rc != LUA_OK) {
        printf("Lua 执行错误(%d): %s\n", rc, lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
