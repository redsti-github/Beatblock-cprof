#include <iostream>
#include <cmath>
#include <cstring>
#include <lua5.1/lua.hpp>
#include <lua5.1/lualib.h>
#include <lua5.1/lauxlib.h>
#include <vector>
#include <chrono>

//#define DEBUG
//#define PROFILE_C_FUNCTIONS

// TODO: proftime stack is broken
static double proftime_total = 0;

static int l_start(lua_State* L);

static void stackDump(lua_State* L) {
	int i;
	int top = lua_gettop(L);
	for (i = 1; i <= top; i++) {
		int t = lua_type(L, i);
		switch (t) {
			case LUA_TSTRING:
				//printf("`%s'", lua_tostring(L, i));
				printf("<str>");
				break;

			case LUA_TBOOLEAN:
				printf(lua_toboolean(L, i) ? "true" : "false");
				break;

			case LUA_TNUMBER:
				printf("%g", lua_tonumber(L, i));
				break;

			default:
				printf("%s", lua_typename(L, t));
				break;
		}
		printf("  ");
	}
	printf("\n");
}

static int stackTrace(lua_State* L){
	lua_getglobal(L, "debug");
	std::cout << " --- STACK TRACE ---\n";

	int level = -1;
	while (true) {
		level++;
		lua_pushstring(L, "getinfo");
		lua_gettable(L, -2);
		lua_pushnumber(L, level);
		lua_pushstring(L, "nfSl");
		lua_call(L, 2, 1);
		if (lua_isnil(L, -1)){
			lua_pop(L, 2);
			break;
		}
		std::cout << "\t" << level << " ";

		lua_pushstring(L, "source");
		lua_gettable(L, -2);
		const char* source = lua_tostring(L, -1);
		std::cout << "in: " << source << " ";
		lua_pop(L, 1);

		lua_pushstring(L, "name");
		lua_gettable(L, -2);
		if (!lua_isnil(L, -1)) {
			const char* name = lua_tostring(L, -1);
			std::cout << "name: " << name << " ";
		}
		lua_pop(L, 1);

		lua_pushstring(L, "func");
		lua_gettable(L, -2);
		if (!lua_isnil(L, -1)) {
			const void* f = (void*)lua_topointer(L, -1);
			std::cout << "f: " << f << " ";
		}
		lua_pop(L, 1);

		lua_pushstring(L, "currentline");
		lua_gettable(L, -2);
		double line = luaL_checknumber(L, -1);
		std::cout << "line: " << line << " ";
		lua_pop(L, 1);

		lua_pop(L, 1);
		std::cout << "\n";
	}

	std::cout << "\n";
	return level;
}

static const char registryKey = 'k';
inline static int push_regtable(lua_State* L){
	lua_pushlightuserdata(L, (void *)&registryKey);
	lua_rawget(L, LUA_REGISTRYINDEX);
	return lua_gettop(L);
}
inline static int push_registry_entry(lua_State* L, int regidx, const char* name){
	lua_pushstring(L, name);
	lua_rawget(L, regidx);
	return lua_gettop(L);
}
static void push_registry_entry_single(lua_State* L, const char* name){
	int regidx = push_regtable(L);
	push_registry_entry(L, regidx, name);
	lua_remove(L, regidx);
}
static void pop_registry_entry(lua_State* L, const char* name, int idx){
	push_regtable(L);
	lua_pushstring(L, name);
	lua_pushvalue(L, idx<0 ? idx-2 : idx);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}
#define REG_CALLSTACK "callstack"
#define REG_INFOROOT "inforoot"
#define REG_INFONOW "infonow"
#define REG_TIMESTACK "timestack"
#define REG_PROFTIMESTACK "proftimestack"
static void initRegistry(lua_State* L){
	lua_pushlightuserdata(L, (void *)&registryKey);
	lua_newtable(L); // TODO: use lua_createtable instead
	lua_rawset(L, LUA_REGISTRYINDEX);

	lua_newtable(L);
	pop_registry_entry(L, REG_CALLSTACK, -1);
	lua_pop(L, 1);

	lua_newtable(L);
	pop_registry_entry(L, REG_TIMESTACK, -1);
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushnumber(L, 0);
	lua_rawseti(L, -2, 0); // proftimestack[0] = 0
	pop_registry_entry(L, REG_PROFTIMESTACK, -1);
	lua_pop(L, 1);

	lua_newtable(L);
	pop_registry_entry(L, REG_INFOROOT, -1);
	pop_registry_entry(L, REG_INFONOW, -1);
	lua_pop(L, 1);
}

static void printInfo(lua_Debug* ar){
	if (ar->what) std::cout << "[" << ar->what << "]";

	if (ar->source && ar->name)
		std::cout << ar->source << ":" << ar->name << "  ";
	else if (ar->name)
		std::cout << ar->name << "  ";
	else if (ar->source)
		std::cout << ar->source << "  ";
	else
		std::cout << "??  ";
}

static void printCallstack(lua_State* L){
	int regidx = push_regtable(L);

	lua_Debug ar;
	push_registry_entry(L, regidx, REG_CALLSTACK);
	size_t len = lua_objlen(L, -1);
	for (size_t i=1; i <= len; i++){
		lua_rawgeti(L, -1, i);
		if (lua_getinfo(L, ">nS", &ar) == 0){
			lua_pushstring(L, "[cprof] call to 'lua_getinfo' failed!");
			lua_error(L);
		}
		if (ar.name)
			std::cout << ar.name << "  ";
		else {
			printInfo(&ar);
		}
	}
	std::cout << "(" << len << ")\n";
	lua_pop(L, 1);

	// count infonow upstack
	push_registry_entry(L, regidx, REG_INFONOW);
	size_t infonowParentCount = -1;
	while (!lua_isnil(L, -1)){
		lua_pushstring(L, "p");
		lua_rawget(L, -2);
		lua_remove(L, -2);
		infonowParentCount++;
	}
	lua_pop(L, 1);

	lua_settop(L, regidx-1);

	if (infonowParentCount != len) std::cout << "[cprof] ERROR! 'infonow' depth != 'callstack' depth!!";
}

static void onCall(lua_State* L, lua_Debug* ar){
	size_t time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	if (lua_getinfo(L, "nS", ar) == 0){
		lua_pushstring(L, "[cprof] call to 'lua_getinfo' failed!");
		lua_error(L);
	}
#ifndef PROFILE_C_FUNCTIONS
	if (strcmp(ar->what, "C") == 0) return;
#endif

	int regidx = push_regtable(L);

#ifdef DEBUG
	std::cout << "onCall: "; printInfo(ar);
	lua_getinfo(L, "f", ar);
	const void* fp = lua_topointer(L, -1);
	std::cout << " <" << fp << ">\n";
	lua_pop(L, 1);
#endif

	//// add function to callstack ////
	// len++
	// callstack[len] = func
	int callstack = push_registry_entry(L, regidx, REG_CALLSTACK);
	size_t len = lua_objlen(L, callstack); len++;
	lua_getinfo(L, "f", ar);
	lua_rawseti(L, callstack, len);
	lua_pop(L, 1); // pop callstack


	//// add timestamp ////
	// timestack[len] = time
	int timestack = push_registry_entry(L, regidx, REG_TIMESTACK);
	lua_pushnumber(L, time);
	lua_rawseti(L, timestack, len);
	lua_pop(L, 1);


	//// update infonow ////
	// push(infonow[func])
	int infonow = push_registry_entry(L, regidx, REG_INFONOW);
	lua_getinfo(L, "f", ar);
	lua_rawget(L, infonow);

	// if (infonow[func] == nil)
	if (lua_isnil(L, -1)){
		lua_pop(L, 1);

		// new = {}
		lua_newtable(L);
		int new_ = lua_gettop(L);

		// new.p = infonow
		lua_pushstring(L, "p");
		lua_pushvalue(L, infonow);
		lua_rawset(L, new_);

		// new.t = 0
		lua_pushstring(L, "t");
		lua_pushnumber(L, 0);
		lua_rawset(L, new_);

		// infonow[func] = new
		lua_getinfo(L, "f", ar);
		lua_pushvalue(L, new_);
		lua_rawset(L, infonow);
	}
	// stack: <infonow> <infonow[func]>
	pop_registry_entry(L, REG_INFONOW, -1);
	lua_pop(L, 2);

#ifdef DEBUG
	std::cout << "after onCall: ";
	printCallstack(L);
	std::cout << "\n";
#endif

	//// update proftimestack ////
	// proftimestack[len] = time2 - time
	int proftimestack = push_registry_entry(L, regidx, REG_PROFTIMESTACK);
	size_t time2 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	lua_pushnumber(L, time2 - time);
	lua_rawseti(L, proftimestack, len);

	proftime_total += time2 - time;

	lua_settop(L, regidx-1);
}

static void onReturn(lua_State* L, lua_Debug* ar, bool functionOnStack = false){
	size_t time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); // TODO: subtract time spent in `onCall` and `onReturn`

	if (lua_getinfo(L, functionOnStack ? ">nSf" : "nSf", ar) == 0){
		lua_pushstring(L, "[cprof] call to 'lua_getinfo' failed!");
		lua_error(L);
	}
	int func = lua_gettop(L);

#ifndef PROFILE_C_FUNCTIONS
	if (strcmp(ar->what, "C") == 0) {lua_pop(L, 1); return;}
#endif
	int regidx = push_regtable(L);
	int timestack = push_registry_entry(L, regidx, REG_TIMESTACK);
	int callstack = push_registry_entry(L, regidx, REG_CALLSTACK);
	int proftimestack = push_registry_entry(L, regidx, REG_PROFTIMESTACK);

#ifdef DEBUG
	std::cout << "onReturn: "; printInfo(ar);
	const void* fp = lua_topointer(L, -1);
	std::cout << " <" << fp << ">\n";
#endif

	// pop callstack until 'func'
	size_t len = lua_objlen(L, callstack);
	for (; len > 0; len--) {
		// func2 = callstack[len]
		lua_rawgeti(L, callstack, len);
		int func2 = lua_gettop(L);

	 	// callstack[len] = nil
		lua_pushnil(L);
		lua_rawseti(L, callstack, len);

		//// update timestack ////
		// delta = time - timestack[len]
		lua_rawgeti(L, timestack, len);
		double delta = time - lua_tonumber(L, -1);
		lua_pop(L, 1);

		// timestack[len] = nil
		lua_pushnil(L);
		lua_rawseti(L, timestack, len);

		//// update infonow ////
		int infonow = push_registry_entry(L, regidx, REG_INFONOW);

		// t = infonow.t
		lua_pushstring(L, "t");
		lua_rawget(L, infonow);
		double t = lua_tonumber(L, -1);
		lua_pop(L, 1);

		// proftime = proftimestack[len]
		lua_rawgeti(L, infonow, len);
		double proftime = lua_tonumber(L, -1);
		lua_pop(L, 1);

		// infonow.t = infonow.t + delta - proftime
		lua_pushstring(L, "t");
		lua_pushnumber(L, t + delta - proftime);
		lua_rawset(L, infonow);

		// infonow = infonow.p
		lua_pushstring(L, "p");
		lua_rawget(L, infonow);
		pop_registry_entry(L, REG_INFONOW, -1);
		lua_pop(L, 2); // pop infonow.p, infonow

		//// update proftimestack ////
		// proftimestack[len] = nil
		lua_pushnil(L);
		lua_rawseti(L, proftimestack, len);

		// prev = proftimestack[len-1]
		lua_rawgeti(L, proftimestack, len-1);
		double prev = lua_tonumber(L, -1);
		lua_pop(L, 1);

		// proftimestack[len-1] = prev + proftime
		lua_pushnumber(L, prev + proftime);
		lua_rawseti(L, proftimestack, len-1);

		// if (func == func2) break;
		int eq = lua_rawequal(L, func, func2);
		lua_pop(L, 1);

		// stack: <f> <regtable> <timestack> <callstack>
		if (eq) {len--; break;}

		if (len == 1) {
			std::cout << "[cprof] ERROR! hit bottom of callstack without finding returned function!\n";
			l_start(L); // reset
			len = lua_objlen(L, callstack);
			break;
		}
	}

#ifdef DEBUG
	std::cout << "after onReturn: ";
	printCallstack(L);
	std::cout << "\n";
#endif

	//// update proftimestack ////
	// prev = proftimestack[len]
	lua_rawgeti(L, proftimestack, len);
	double prev = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// proftimestack[len] = prev + time2 - time
	size_t time2 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	lua_pushnumber(L, prev + (time2 - time));
	lua_rawseti(L, proftimestack, len);

	proftime_total += time2 - time;

	lua_settop(L, func-1);
}

static void debugHook(lua_State* L, lua_Debug* ar){
	switch (ar->event){
		case LUA_HOOKCALL: onCall(L, ar); break;
		case LUA_HOOKRET: onReturn(L, ar); break;
		default: std::cout << "unknown debug event"; break;
	}
}

static int l_start(lua_State* L){
	// check if we're already running
	push_registry_entry_single(L, REG_CALLSTACK);
	size_t len = lua_objlen(L, -1);
	lua_pop(L, 1);
	if (len) {
		lua_pushstring(L, "[cprof] Cannot start when already running!");
		lua_error(L);
	}

	// init tables with current call stack
	// find stack depth
	lua_Debug ar;
	int depth = 0;
	while (lua_getstack(L, depth+1, &ar)) depth++;

	// simulate the calls
	for (; depth > 0; depth--){
		lua_getstack(L, depth, &ar);
		onCall(L, &ar);
	}

	// start profiling!!
	lua_sethook(L, debugHook, LUA_MASKCALL | LUA_MASKRET, 0);
	return 0;
}

static int l_stop(lua_State* L){
	lua_sethook(L, debugHook, 0, 0); // disable hook

	// TODO: call onReturn with just callstack[1] ?
	push_registry_entry_single(L, REG_CALLSTACK);
	while (size_t len = lua_objlen(L, -1)){
		lua_rawgeti(L, -1, len);
		lua_Debug ar;
		onReturn(L, &ar, true);
	}
	lua_pop(L, 1);

	std::cout << "total proftime = " << proftime_total/1000 << " ms\n";

	return 0;
}

static int l_reset(lua_State* L){
	l_stop(L);
	push_registry_entry_single(L, REG_INFOROOT);
	initRegistry(L);
	return 1; // return previous inforoot
}

static int l_getProfTable(lua_State* L){
	push_registry_entry_single(L, REG_INFOROOT);
	return 1;
}

static int l_getproftime(lua_State* L){
	push_registry_entry_single(L, REG_PROFTIMESTACK);
	lua_rawgeti(L, -1, 0);
	return 1;
}

static const struct luaL_reg cprof [] = {
	{"reset", l_reset},
	{"start", l_start},
	{"stop", l_stop},
	{"getProfTime", l_getproftime},
	{"getProfTable", l_getProfTable},
	{NULL, NULL}  /* sentinel */
};
extern "C" int luaopen_cprof(lua_State *L) {
	luaL_openlib(L, "cprof", cprof, 0);
	initRegistry(L);
	return 1;
}
