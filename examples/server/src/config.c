#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "tiny.h"
#include "util.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#else
#error TODO Port to unix
#endif

static TINY_FOREIGN_FUNCTION(AddCommonScript) {
    Config* c = thread->userdata;

    sb_push(c->commonScripts, estrdup(Tiny_ToString(args[0])));

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(SetMaxConns) {
    Config* c = thread->userdata;

    c->maxConns = Tiny_ToInt(args[0]);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(SetCyclesPerLoop) {
    Config* c = thread->userdata;

    c->cyclesPerLoop = Tiny_ToInt(args[0]);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(SetName) {
    Config* c = thread->userdata;

    c->name = estrdup(Tiny_ToString(args[0]));

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(AddRoute) {
    Config* c = thread->userdata;

    Route r;

    r.pattern = estrdup(Tiny_ToString(args[0]));
    r.filename = estrdup(Tiny_ToString(args[1]));

    sb_push(c->routes, r);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(GetArgc) {
    Config* c = thread->userdata;

    return Tiny_NewInt(c->argc);
}

static TINY_FOREIGN_FUNCTION(GetArgv) {
    Config* c = thread->userdata;

    int i = Tiny_ToInt(args[0]);

    assert(i >= 0 && i < c->argc);

    return Tiny_NewConstString(c->argv[i]);
}

static TINY_FOREIGN_FUNCTION(SetPort) {
    Config* c = thread->userdata;

    c->port = estrdup(Tiny_ToString(args[0]));

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(SetNumThreads) {
    Config* c = thread->userdata;

    c->numThreads = Tiny_ToInt(args[0]);

    assert(c->numThreads > 0);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(Lib_LoadModule) {
    Config* c = thread->userdata;

    const char* name = Tiny_ToString(args[0]);

    ForeignModule mod = {0};

#ifdef WIN32
    mod.handle = (void*)LoadLibraryA(name);
#else
#error How to load dll?
#endif

    if (!mod.handle) {
        fprintf(stderr, "Failed to load module '%s'.\n", name);
        exit(1);
    }

    for (int i = 1; i < count; i += 2) {
        const char* procName = Tiny_ToString(args[i]);

        ForeignModuleFunction modFunc;

        modFunc.sig = estrdup(Tiny_ToString(args[i + 1]));

#ifdef _WIN32
        modFunc.func = (Tiny_ForeignFunction)GetProcAddress(mod.handle, procName);
#else
#error GetProcAddress equivalent here
#endif

        if (!modFunc.func) {
            fprintf(stderr, "Failed to load procedure '%s' from module '%s'.\n", procName, name);
            exit(1);
        }

        sb_push(mod.funcs, modFunc);
    }

    sb_push(c->modules, mod);

    return Tiny_Null;
}

void InitConfig(Config* c, const char* filename, int argc, char** argv) {
    c->name = NULL;

    c->port = NULL;
    c->numThreads = 0;

    c->argc = argc;
    c->argv = argv;

    c->routes = NULL;

    c->cyclesPerLoop = 10;

    c->maxConns = 10;

    c->commonScripts = NULL;

    c->modules = NULL;

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindFunction(state, "get_argc(): int", GetArgc);
    Tiny_BindFunction(state, "get_argv(int): str", GetArgv);

    Tiny_BindFunction(state, "add_route(str, str): void", AddRoute);

    Tiny_BindFunction(state, "set_name(str): void", SetName);
    Tiny_BindFunction(state, "set_port(str): void", SetPort);

    Tiny_BindFunction(state, "set_max_conns(int): void", SetMaxConns);

    Tiny_BindFunction(state, "set_num_threads(int): void", SetNumThreads);
    Tiny_BindFunction(state, "set_cycles_per_loop(int): void", SetCyclesPerLoop);

    Tiny_BindFunction(state, "add_common_script(str): void", AddCommonScript);

    Tiny_BindFunction(state, "load_module(str, ...): void", Lib_LoadModule);

    Tiny_CompileFile(state, filename);

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    thread.userdata = c;

    Tiny_StartThread(&thread);

    Tiny_Run(&thread);

    Tiny_DestroyThread(&thread);
    Tiny_DeleteState(state);
}

const char* GetFilenameForTarget(const Config* c, const char* target) {
    for (int i = 0; i < sb_count(c->routes); ++i) {
        const Route* r = &c->routes[i];

        const char* s = r->pattern;
        const char* t = target;

        while (*t && (*s == *t || *s == '*')) {
            if (*s != '*') ++s;
            ++t;
        }

        if (*t == 0 && (*s == 0 || *s == '*')) {
            return r->filename;
        }
    }

    return NULL;
}

void DestroyConfig(Config* c) {
    free(c->name);
    free(c->port);

    for (int i = 0; i < sb_count(c->routes); ++i) {
        free(c->routes[i].pattern);
        free(c->routes[i].filename);
    }

    sb_free(c->routes);

    for (int i = 0; i < sb_count(c->commonScripts); ++i) {
        free(c->commonScripts[i]);
    }

    sb_free(c->commonScripts);

    for (int i = 0; i < sb_count(c->modules); ++i) {
        for (int j = 0; j < sb_count(c->modules[i].funcs); ++j) {
            free(c->modules[i].funcs[j].sig);
        }

        sb_free(c->modules[i].funcs);
    }

    sb_free(c->modules);
}
