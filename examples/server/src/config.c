#include <stdlib.h>
#include <assert.h>

#include "tiny.h"
#include "config.h"
#include "util.h"

struct Route
{
    char* pattern;
    char* filename;
};

static TINY_FOREIGN_FUNCTION(SetName)
{
    Config* c = thread->userdata;

    c->name = estrdup(Tiny_ToString(args[0]));

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(AddRoute)
{
    Config* c = thread->userdata;

    Route r;

    r.pattern = estrdup(Tiny_ToString(args[0]));
    r.filename = estrdup(Tiny_ToString(args[1]));

    sb_push(c->routes, r);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(GetArgc)
{
    Config* c = thread->userdata;

    return Tiny_NewInt(c->argc);
}

static TINY_FOREIGN_FUNCTION(GetArgv)
{
    Config* c = thread->userdata;

    int i = Tiny_ToInt(args[0]);

    assert(i >= 0 && i < c->argc);

    return Tiny_NewConstString(c->argv[i]);
}

static TINY_FOREIGN_FUNCTION(SetPort)
{
    Config* c = thread->userdata;

    c->port = estrdup(Tiny_ToString(args[0]));

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(SetNumThreads)
{
    Config* c = thread->userdata;

    c->numThreads = Tiny_ToInt(args[0]);

    assert(c->numThreads > 0);

    return Tiny_Null;
}

void InitConfig(Config* c, const char* filename, int argc, char** argv)
{
    c->name = NULL;

    c->port = NULL;
    c->numThreads = 0;

    c->argc = argc;
    c->argv = argv;

    c->routes = NULL;

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindFunction(state, "get_argc(): int", GetArgc);
    Tiny_BindFunction(state, "get_argv(int): str", GetArgv);

    Tiny_BindFunction(state, "add_route(str, str): void", AddRoute);

    Tiny_BindFunction(state, "set_name(str): void", SetName);
    Tiny_BindFunction(state, "set_port(str): void", SetPort);
    Tiny_BindFunction(state, "set_num_threads(int): void", SetNumThreads);

    Tiny_CompileFile(state, filename);

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    thread.userdata = c;

    Tiny_StartThread(&thread);

    while(Tiny_ExecuteCycle(&thread));

    Tiny_DestroyThread(&thread);
    Tiny_DeleteState(state);
}

const char* GetFilenameForTarget(const Config* c, const char* target)
{
    for(int i = 0; i < sb_count(c->routes); ++i) {
        const Route* r = &c->routes[i];
        
        const char* s = r->pattern;
        const char* t = target;

        while(*t && (*s == *t || *s == '*')) {
            if(*s != '*') ++s;
            ++t;
        }

        if(*t == 0 && (*s == 0 || *s == '*')) {
            return r->filename;
        }
    }

    return NULL;
}

void DestroyConfig(Config* c)
{
    free(c->name);
    free(c->port);

    for(int i = 0; i < sb_count(c->routes); ++i) {
        free(c->routes[i].pattern);
        free(c->routes[i].filename);
    }

    sb_free(c->routes);
}
