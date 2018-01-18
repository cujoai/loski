#include "loskiaux.h"
#include "lprocaux.h"

#include <string.h>
#include <lua.h>


#ifdef LOSKI_DISABLE_PROCDRV
#define DRVUPV	0
#define todrv(L)	NULL
#else
#define DRVUPV	1
#define todrv(L)	((loski_ProcDriver *)lua_touserdata(L, \
                                       lua_upvalueindex(DRVUPV)))
#endif


static const char* getstrfield(lua_State *L, const char *field, int required)
{
	const char* value = NULL;
	lua_getfield(L, 1, field);
	if (required || !lua_isnil(L, -1)) {
		value = lua_tostring(L, -1);
		if (value == NULL)
			luaL_error(L, "bad field "LUA_QS" (must be a string)", field);
	}
	lua_pop(L, 1);
	return value;
}

static int optstreamfield(lua_State *L,
                          const char *field,
                          loski_ProcStream *stream)
{
	int res = 0;
	lua_getfield(L, 1, field);
	if (!lua_isnil(L, -1)) {
		loski_ProcStreamConv tostream = loski_getprocstreamconv(L);
		if (tostream == NULL || !tostream(L, -1, stream))
			luaL_error(L, "bad field "LUA_QS" (must be a stream)", field);
		res = 1;
	}
	lua_pop(L, 1); /* remove value */
	return res;
}

static const char *getstackarg (void *data, int index)
{
	return lua_tostring((lua_State *)data, index+1);
}

static const char *gettablearg (void *data, int index)
{
	lua_State *L = (lua_State *)data;
	const char *argv;
	if (index == 0) lua_getfield(L, 1, "execfile");
	else lua_rawgeti(L, 2, index);
	argv = lua_tostring(L, -1);
	lua_pop(L, 1); /* pop an argument string */
	return argv;
}

static const char *getenvvar (void *data, const char **name)
{
	lua_State *L = (lua_State *)data;
	while (lua_next(L, 3) != 0) {
		const char *value = lua_tostring(L, -1);
		*name = lua_tostring(L, -2);
		lua_pop(L, 1);
		if (*name) return value;
	}
	return NULL;
}

/* proc = process.create (cmd [, ...]) */
static int lp_create(lua_State *L)
{
	loski_ProcDriver *drv = todrv(L);
	loski_Process *proc;
	const char *exec;
	const char *path = NULL;
	int hasinput = 0;
	int hasoutput = 0;
	int haserror = 0;
	loski_ProcStream stdinput;
	loski_ProcStream stdoutput;
	loski_ProcStream stderror;
	void *argv = NULL;
	void *envl = NULL;
	size_t argsz = 0;
	size_t envsz = 0;
	loski_ProcArgInfo arginf;
	loski_ProcEnvInfo envinf;
	int err;
	
	if (lua_isstring(L, 1)) {

		int i;
		int argc = lua_gettop(L);
		exec = luaL_checkstring(L, 1);
		for (i = 2; i <= argc; ++i) luaL_checkstring(L, i);
		err = loskiP_checkargs(drv, &arginf, getstackarg, L, argc, &argsz);
		if (err) { luaL_pusherrmsg(L, err); lua_error(L); }
		proc = (loski_Process *)lua_newuserdata(L, sizeof(loski_Process));
		argv = luaL_alloctemporary(L, argsz);
		loskiP_initargs(drv, &arginf, getstackarg, L, argc, argv, argsz);

	} else if (lua_istable(L, 1)) {
		int argc = 0;

		lua_settop(L, 1);
		exec = getstrfield(L, "execfile", 1);
		path = getstrfield(L, "runpath", 0);
		hasinput = optstreamfield(L, "stdin", &stdinput);
		hasoutput = optstreamfield(L, "stdout", &stdoutput);
		haserror = optstreamfield(L, "stderr", &stderror);

		lua_getfield(L, 1, "arguments");
		if (lua_istable(L, 2)) {
			int i;
			argc = 1+lua_rawlen(L, 2);  /* execfile + arguments */
			/* check arguments are strings */
			for (i = 1; i < argc; ++i) {
				lua_rawgeti(L, 2, i);
				if (!lua_isstring(L, -1)) luaL_error(L,
					"bad value #%d in field "LUA_QL("arguments")" (must be a string)", i);
				lua_pop(L, 1); /* pop an argument string */
			}
			/* check arguments are valid for the driver */
			err = loskiP_checkargs(drv, &arginf, gettablearg, L, argc, &argsz);
			if (err) { luaL_pusherrmsg(L, err); lua_error(L); }
		} else if (!lua_isnil(L, 2)) {
			luaL_argerror(L, 1, "field "LUA_QL("arguments")" must be a table");
		}

		lua_getfield(L, 1, "environment");
		if (lua_istable(L, 3)) {
			/* check environment variables are strings */
			lua_pushnil(L);  /* first key */
			if (lua_next(L, 3) != 0) {
				const char *name = lua_tostring(L, -2);
				if (name && (!lua_isstring(L, -1) || strchr(name, '=')))
					luaL_error(L, "bad name "LUA_QS" in field "LUA_QL("environment")
					              " (must be a string without "LUA_QL("=")")", name);
				lua_pop(L, 1);
			}
			/* check environment variables are valid for the driver */
			lua_pushnil(L);  /* first key */
			err = loskiP_checkenv(drv, &envinf, getenvvar, L, &envsz);
			if (err) { luaL_pusherrmsg(L, err); lua_error(L); }
			lua_settop(L, 3);  /* discard any left over values */
		} else if (!lua_isnil(L, -1)) {
			luaL_argerror(L, 1, "field "LUA_QL("environment")" must be a table");
		}

		proc = (loski_Process *)lua_newuserdata(L, sizeof(loski_Process));

		if (lua_istable(L, 3)) {
			envl = luaL_alloctemporary(L, envsz);
			if (envl == NULL) luaL_error(L, "insuffient memory");
			lua_pushnil(L);  /* first key */
			loskiP_initenv(drv, &envinf, getenvvar, L, envl, envsz);
			lua_settop(L, 4);  /* discard any left over values */
		}

		if (lua_istable(L, 2)) {
			argv = luaL_alloctemporary(L, argsz);
			if (argv == NULL) {
				if (envl != NULL) luaL_freetemporary(L, envl, envsz);
				luaL_error(L, "insuffient memory");
			}
			loskiP_initargs(drv, &arginf, gettablearg, L, argc, argv, argsz);
		}

	} else {
		return luaL_argerror(L, 1, "table or string expected");
	}
	err = loskiP_initproc(drv, proc, exec, path, argv, envl,
	                      (hasinput ? &stdinput : NULL),
	                      (hasoutput ? &stdoutput : NULL),
	                      (haserror ? &stderror : NULL));
	if (argv != NULL) luaL_freetemporary(L, argv, argsz);
	if (envl != NULL) luaL_freetemporary(L, envl, envsz);
	if (err == 0) luaL_setmetatable(L, LOSKI_PROCESSCLS);
	return luaL_doresults(L, 1, err); /* return process */
}

#define toproc(L)	((loski_Process *)luaL_checkudata(L, 1, LOSKI_PROCESSCLS))

/* string = tostring(process) */
static int lp_tostring (lua_State *L)
{
	lua_pushfstring(L, "process (%p)", toproc(L));
	return 1;
}

/* getmetatable(process).__gc */
static int lp_gc(lua_State *L)
{
	loski_ProcDriver *drv = todrv(L);
	loski_Process* proc = toproc(L);
	loskiP_freeproc(drv, proc);
	return 0;
}

/* status = process.status (proc) */
static int lp_status(lua_State *L)
{
	static const char *names[] = { "running", "suspended", "dead" };
	loski_ProcDriver *drv = todrv(L);
	loski_Process* proc = toproc(L);
	loski_ProcStatus status;
	int err = loskiP_getprocstat(drv, proc, &status);
	if (err == 0) lua_pushstring(L, names[status]);
	return luaL_doresults(L, 1, err);
}

/* number = process.exitval (proc) */
static int lp_exitval(lua_State *L)
{
	loski_ProcDriver *drv = todrv(L);
	loski_Process* proc = toproc(L);
	int code;
	int err = loskiP_getprocexit(drv, proc, &code);
	if (err == 0) lua_pushinteger(L, code);
	return luaL_doresults(L, 1, err);
}

/* succ [, errmsg] = process.kill (proc) */
static int lp_kill(lua_State *L)
{
	loski_ProcDriver *drv = todrv(L);
	loski_Process* proc = toproc(L);
	int err = loskiP_killproc(drv, proc);
	return luaL_doresults(L, 0, err);
}


static const luaL_Reg cls[] = {
	{"__gc", lp_gc},
	{"__tostring", lp_tostring},
	{NULL, NULL}
};

static const luaL_Reg lib[] = {
	{"create", lp_create},
	{"status", lp_status},
	{"exitval", lp_exitval},
	{"kill", lp_kill},
	{NULL, NULL}
};

#ifndef LOSKI_DISABLE_PROCDRV
static int lfreedrv (lua_State *L)
{
	loski_ProcDriver *drv = (loski_ProcDriver *)lua_touserdata(L, 1);
	loskiP_freedrv(drv);
	return 0;
}
#endif

LUAMOD_API int luaopen_process(lua_State *L)
{
#ifndef LOSKI_DISABLE_PROCDRV
	/* create sentinel */
	loski_ProcDriver *drv;
	int err;
	lua_settop(L, 0);  /* dicard any arguments */
	drv = (loski_ProcDriver *)luaL_newsentinel(L, sizeof(loski_ProcDriver),
	                                              lfreedrv);
#define pushsentinel(L)	lua_pushvalue(L, 1)
#define cancelsentinel(L)	luaL_cancelsentinel(L)
#else
#define pushsentinel(L)	((void)L)
#define cancelsentinel(L)	((void)L)
#endif
	/* initialize library */
	err = loskiP_initdrv(drv);
	if (err) {
		cancelsentinel(L);
		luaL_pusherrmsg(L, err);
		return lua_error(L);
	}
	/* create process class */
	pushsentinel(L);
	luaL_newclass(L, LOSKI_PROCESSCLS, NULL, cls, DRVUPV);
	lua_pop(L, 1);  /* remove new class */
	/* create library table */
	luaL_newlibtable(L, lib);
	pushsentinel(L);
	luaL_setfuncs(L, lib, DRVUPV);
#ifdef LOSKI_ENABLE_PROCFILESTREAM
	loski_setprocstreamconv(L, LUA_FILEHANDLE, loskiP_luafilestream);
#endif
	return 1;
}
