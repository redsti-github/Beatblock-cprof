#include <time.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>

//#define DEBUG

// TODO: check for tail-calls
// TODO: properly count calls for back-to-back C calls that don't hook return

static bool failed = false;
inline static size_t getTime(){
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (1000*1000*(size_t)ts.tv_sec) + (ts.tv_nsec / 1000); // return microseconds
}

static const char registryKey = 'k';
inline static int push_regtable(lua_State* L){
	lua_pushlightuserdata(L, (void *)&registryKey);
	lua_rawget(L, LUA_REGISTRYINDEX);
	return lua_gettop(L);
}

typedef enum {
	REG_CALLSTACK = 0,
	REG_INFOROOT = 1,
	REG_INFONOW = 2,
	REG_TIMESTACK = 3,
	REG_PROFTIMESTACK = 4,
} reg_key_t;

inline static int push_registry_entry(lua_State* L, int regidx, const reg_key_t name){
	lua_rawgeti(L, regidx, (int)name);
	return lua_gettop(L);
}
inline static void push_registry_entry_single(lua_State* L, const reg_key_t name){
	int regidx = push_regtable(L);
	push_registry_entry(L, regidx, name);
	lua_remove(L, regidx);
}
inline static void pop_registry_entry(lua_State* L, int regtable, const reg_key_t name){
	lua_rawseti(L, regtable, (int)name);
}

static void initRegistry(lua_State* L){
	lua_pushlightuserdata(L, (void *)&registryKey);
	lua_newtable(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	int regtable = push_regtable(L);

	lua_newtable(L);
	pop_registry_entry(L, regtable, REG_CALLSTACK);

	lua_newtable(L);
	pop_registry_entry(L, regtable, REG_TIMESTACK);

	lua_newtable(L);
	lua_pushnumber(L, 0);
	lua_rawseti(L, -2, 0); // proftimestack[0] = 0
	pop_registry_entry(L, regtable, REG_PROFTIMESTACK);

	lua_newtable(L);
	lua_pushvalue(L, -1);
	pop_registry_entry(L, regtable, REG_INFOROOT);
	pop_registry_entry(L, regtable, REG_INFONOW);

	lua_pop(L, 1); // pop regtable
}


#ifdef DEBUG
inline static void stackDump(lua_State* L) {
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

static void printInfo(lua_Debug* ar, bool printName){
	if (ar->what) printf("[%s]",  ar->what);

	if (ar->source && ar->name && printName)
		printf("%s:%s  ", ar->source, ar->name);
	else if (ar->name && printName)
		printf("%s  ", ar->name);
	else if (ar->source)
		printf("%s  ", ar->source);
	else
		printf("??  ");
}

static int stackTrace(lua_State* L){
	lua_Debug ar;

	int depth = 0;

	// get stack depth
	while (lua_getstack(L, depth+1, &ar)) depth++;

	int count = 0;
	for (int i=depth; i>=0; i--) {
		if (lua_getstack(L, i, &ar) == 0) break;
		lua_getinfo(L, "nS", &ar);

		printInfo(&ar, false);
		count++;
	}

	printf("(%i)\n", count);
	return count;
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
			printf("%s  ", ar.name);
		else {
			printInfo(&ar, true);
		}
	}
	printf("(%lu)\n", len);
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

	if (infonowParentCount != len) puts("[cprof] ERROR! 'infonow' depth != 'callstack' depth!!");
}
#endif



// takes the function on top of the stack and returns the key to be used in infonow
static void getInfoKey(lua_State* L){
	lua_Debug ar;
	lua_getinfo(L, ">Sf", &ar);

	if ((ar.source[0] != '@') || (strcmp(ar.what, "C") == 0)){
		return; // function is already ontop
	}

	lua_pop(L, 1);
	lua_pushstring(L, ar.source);
	lua_pushstring(L, ":");
	lua_pushnumber(L, ar.linedefined);
	lua_pushstring(L, ":");
	lua_pushnumber(L, ar.lastlinedefined);
	lua_concat(L, 5);
}

// NOTE: function to be pushed MUST be on top of the stack AND it will get popped
static void callstackPush(lua_State* L, int regtable, int callstack, int timestack, int proftimestack, size_t time, bool accurate){
	size_t len = lua_objlen(L, callstack) + 1;
	// stack: ... <func>

#ifdef DEBUG
	puts("callstackPush()");
#endif

	// callstack[len] = func
	lua_pushvalue(L, -1);
	lua_rawseti(L, callstack, len);

	// timestack[len] = time
	lua_pushnumber(L, time);
	lua_rawseti(L, timestack, len);

	// proftimestack[len] = 0
	lua_pushnumber(L, 0);
	lua_rawseti(L, proftimestack, len);

	// push(infonow)
	int infonow = push_registry_entry(L, regtable, REG_INFONOW);
	// stack: ... <func> <infonow>
	lua_pushvalue(L, -2);
	getInfoKey(L);
	lua_rawget(L, infonow);

	// if (infonow[func] == nil)
	if (lua_isnil(L, -1)){
		lua_pop(L, 1);
		// stack: ... <func> <infonow>

		// new = {}
		lua_newtable(L);
		int new_ = lua_gettop(L);

		// stack: ... <func> <infonow> <new>

		// new.p = infonow
		lua_pushstring(L, "p");
		lua_pushvalue(L, infonow);
		lua_rawset(L, new_);

		// new.t = 0
		lua_pushstring(L, "t");
		lua_pushnumber(L, 0);
		lua_rawset(L, new_);

		// new.f = func
		lua_pushstring(L, "f");
		lua_pushvalue(L, -4);
		lua_rawset(L, new_);

		// new.c = 1
		lua_pushstring(L, "c");
		lua_pushnumber(L, 1);
		lua_rawset(L, new_);

		if (!accurate) {
			lua_pushstring(L, "i");
			lua_pushnumber(L, 1);
			lua_rawset(L, new_);
		}

		// stack: ... <func> <infonow> <new>

		// infonow[func] = new
		lua_pushvalue(L, -3);
		getInfoKey(L);
		lua_pushvalue(L, new_);
		lua_rawset(L, infonow);
	}else{
		// increment callcount
		// c = infonow.c
		lua_pushstring(L, "c");
		lua_rawget(L, -2);
		double c = lua_tonumber(L, -1);
		lua_pop(L, 1);

		// infonow.c = c+1
		lua_pushstring(L, "c");
		lua_pushnumber(L, c+1);
		lua_rawset(L, -3);
	}
	// stack: ... <func> <infonow> <infonow[func]>
	// infonow = infonow[func]
	pop_registry_entry(L, regtable, REG_INFONOW);
	// stack: ... <func> <old infonow>
	lua_pop(L, 2);
}

static void callstackPop(lua_State* L, int regtable, int callstack, int timestack, int proftimestack, size_t time, bool accurate){
	size_t len = lua_objlen(L, callstack);

	// t1 = timestack[len]
	lua_rawgeti(L, timestack, len);
	double t1 = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// pt = proftimestack[len]
	lua_rawgeti(L, proftimestack, len);
	double pt = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// prev_pt = proftimestack[len-1]
	lua_rawgeti(L, proftimestack, len-1);
	double prev_pt = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// proftimestack[len-1] = prev_pt + pt
	lua_pushnumber(L, prev_pt + pt);
	lua_rawseti(L, proftimestack, len-1);

	// callstack[len] = nil
	lua_pushnil(L);
	lua_rawseti(L, callstack, len);

#ifdef DEBUG // we don't need to set them to nil, since they shouldn't be read from anyway (and objlen is only called on callstack)
	// timestack[len] = nil
	lua_pushnil(L);
	lua_rawseti(L, timestack, len);

	//proftimestack[len] = nil
	lua_pushnil(L);
	lua_rawseti(L, proftimestack, len);
#endif

	// push(infonow)
	int infonow = push_registry_entry(L, regtable, REG_INFONOW);

	// tprev = infonow.t
	lua_pushstring(L, "t");
	lua_rawget(L, infonow);
	double tprev = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// infonow.t = tprev + time - t1 - pt
	lua_pushstring(L, "t");
	lua_pushnumber(L, tprev + time - t1 - pt);
	lua_rawset(L, infonow);

	if (!accurate) {
		lua_pushstring(L, "i");
		lua_pushnumber(L, 1);
		lua_rawset(L, infonow);
	}

	// infonow = infonow.p
	lua_pushstring(L, "p");
	lua_rawget(L, infonow);
	pop_registry_entry(L, regtable, REG_INFONOW);

	lua_pop(L, 1); // pop old infonow
}

inline static int getStackDepth(lua_State* L, int depth){ // initial depth is best guess
	if (depth < 0) depth = 0;
	lua_Debug ar;

	if (lua_getstack(L, depth, &ar)){
		// guess is correct or too low
		while (lua_getstack(L, depth+1, &ar)) depth++;
	}else{
		// guess is too high
		depth--;
		while (!lua_getstack(L, depth, &ar)) depth--;
	}

	return depth;
}










static void debugHook(lua_State* L, lua_Debug* ar){
	size_t time = getTime();

	bool isCall;
	switch (ar->event){
		case LUA_HOOKCALL: isCall = true; break;
		case LUA_HOOKRET: isCall = false; break;
		default:
			puts("[cprof] unknown debug event"); return;
	}

#ifdef DEBUG
	lua_getinfo(L, "nS", ar);
	printf("%s", isCall ? "Call: " : "Return: "); printInfo(ar, true);
	lua_getinfo(L, "f", ar);
	const void* fp = lua_topointer(L, -1);
	printf(" <%p>", fp);
	lua_pop(L, 1);
	printCallstack(L);
#endif

	// to whoever may be trying to read this code:
	// goodluck.
	// this is a minefield of avoiding off-by-one errors, so just trust that it works
	// i've left a large amount of comments that might hopefully convince you that there are no off-by-one errors

	// TODO: there is an edge-case when a C call doesnt trigger a return hook, and then the same function is called again
	// currently, the second call hook does nothing, since the call stacks are equal
	// but it should realise the previous call returned, call pop(), and then push() the new one (even tho its the same function)
	// alternatively, just increment the call count
	// in both cases, the function should be marked as inaccurate

	int regtable = push_regtable(L);
	int callstack = push_registry_entry(L, regtable, REG_CALLSTACK);
	int timestack = push_registry_entry(L, regtable, REG_TIMESTACK);
	int proftimestack = push_registry_entry(L, regtable, REG_PROFTIMESTACK);

	// NOTE:
	// callstack[1] ~ lua_getstack(stackDepth)
	// callstack[2] ~ lua_getstack(stackDepth-1)
	// callstack[i+1] ~ lua_getstack(stackDepth-i)
	// callstack[i] ~ lua_getstack(stackDepth-i+1)

	size_t stackDepth = getStackDepth(L, lua_objlen(L, callstack) + (isCall ? +1 : -1));
	size_t sharedDepth = 0;
	// invariant:
	//		callstack[sharedDepth].func == lua_getstack(stackDepth - sharedDepth - 1).func
	for (; sharedDepth <= stackDepth; sharedDepth++){
		// test invariant for (sharedDepth+1)
		lua_rawgeti(L, callstack, sharedDepth+1); // we index from the bottom of the stack (and index from 1)
		lua_getstack(L, stackDepth - sharedDepth, ar); // getstack indexes from the top of the stack (and indexes from 0)
		lua_getinfo(L, "f", ar); // push function

		bool eq = lua_rawequal(L, -1, -2);
		lua_pop(L, 2); // TODO: if !eq, keep the function on top of the stack since we will need it anyway?

		if (!eq) break; // invariant doesn't hold for (sharedDepth+1), so break
	}

	// our stack is "good" until sharedDepth
	// pop all "bad" functions
	while (lua_objlen(L, callstack) > sharedDepth)
		callstackPop(L, regtable, callstack, timestack, proftimestack, time, false); // NOTE: allways inaccurate, since stack in return hooks contain the returning function

	// push all functions that callstack has, but we don't (unless this is a return, the ignore the last entry (the returning function))
	for (size_t i = lua_objlen(L, callstack)+1; i <= stackDepth + (isCall ? 1 : 0); i++){
		lua_getstack(L, stackDepth-i+1, ar); // equiv for callstack[i]
		lua_getinfo(L, "f", ar); // push function
		callstackPush(L, regtable, callstack, timestack, proftimestack, time, i == stackDepth+1); // only last pushed function (in call hooks) is accurate
	}

	if (!isCall && lua_objlen(L, callstack) > stackDepth) { // make sure we pop the returning function, if we haven't already
		callstackPop(L, regtable, callstack, timestack, proftimestack, time, true);
	}

#ifdef DEBUG
	puts("after:");
	printCallstack(L);
	stackTrace(L);
	puts("");
#endif

	//// update proftimestack ////
	size_t len = lua_objlen(L, callstack);

	// prev_pt = proftimestack[len]
	lua_rawgeti(L, proftimestack, len);
	double prev_pt = lua_tonumber(L, -1);
	lua_pop(L, 1);

	// proftimestack[len] = prev_pt + time2 - time
	size_t time2 = getTime();
	lua_pushnumber(L, prev_pt + time2 - time);
	lua_rawseti(L, proftimestack, len);

	lua_settop(L, regtable-1);
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

	// start profiling!!
	lua_sethook(L, debugHook, LUA_MASKCALL | LUA_MASKRET, 0);
	return 0;
}

static int l_stop(lua_State* L){
	size_t time = getTime();
	lua_sethook(L, debugHook, 0, 0); // disable hook

	int regtable = push_regtable(L);
	int callstack = push_registry_entry(L, regtable, REG_CALLSTACK);
	int timestack = push_registry_entry(L, regtable, REG_TIMESTACK);
	int proftimestack = push_registry_entry(L, regtable, REG_PROFTIMESTACK);

	while (lua_objlen(L, callstack) > 0)
		callstackPop(L, regtable, callstack, timestack, proftimestack, time, true);

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
int luaopen_cprof(lua_State *L) {
	luaL_openlib(L, "cprof", cprof, 0);
	initRegistry(L);
	return 1;
}
