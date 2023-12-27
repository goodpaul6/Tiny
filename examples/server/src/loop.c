#include "lib.h"
#include "request.h"
#include "server.h"
#include "util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#define AtomicLoad(x) InterlockedCompareExchange(&(x), 0, 0)
#define AtomicInc(x) InterlockedIncrement(&(x))
#define AtomicDec(x) InterlockedDecrement(&(x))
#else

#error Potential race condition because I don't know how to do atomic load on anything other than Windows.

#define AtomicLoad(x) (x)
#define AtomicInc(x) (x += 1)
#define AtomicDec(x) (x -= 1)
#endif

#define Waiting(thread) (AtomicLoad(((Context*)((thread)->userdata))->waiting))

typedef struct {
    Request req;
    Sock client;
    Server* serv;

    int waiting;

    // This is the worker thread created for the waiting calls
    thrd_t worker;
} Context;

typedef struct {
    Tiny_StateThread* thread;
    const Tiny_Value* args;
    int count;
} WorkerData;

extern const Tiny_NativeProp BufProp;

static int DoCallWait(void* pData) {
    WorkerData* data = pData;

    const char* funcName = Tiny_ToString(data->args[0]);

    int funcIndex = Tiny_GetFunctionIndex(data->thread->state, funcName);

    if (funcIndex < 0) {
        fprintf(stderr, "Attempted to call_wait a non-existent function '%s'.\n", funcName);
        data->thread->retVal = Tiny_Null;
        free(data);
        return 1;
    }

    data->thread->retVal =
        Tiny_CallFunction(data->thread, funcIndex, &data->args[1], data->count - 1);

    Context* ctx = data->thread->userdata;

    AtomicDec(ctx->waiting);

    free(data);

    cnd_signal(&ctx->serv->updateLoop);

    return 0;
}

static TINY_FOREIGN_FUNCTION(CallWait) {
    Context* ctx = thread->userdata;

    WorkerData* data = malloc(sizeof(WorkerData));

    data->thread = thread;
    data->args = args;
    data->count = count;

    thrd_create(&ctx->worker, DoCallWait, data);

    AtomicInc(ctx->waiting);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(Sends) {
    Context* ctx = thread->userdata;

    const char* s = Tiny_ToString(args[0]);

    return Tiny_NewInt(SockSend(&ctx->client, s, (int)strlen(s)));
}

static TINY_FOREIGN_FUNCTION(Sendf) {
    Context* ctx = thread->userdata;

    const char* s = Tiny_ToString(args[0]);

    int arg = 1;

    char* buf = NULL;

    while (*s) {
        if (*s == '%') {
            ++s;
            if (*s == '%') {
                sb_push(buf, '%');
            } else {
                if (arg >= count) {
                    fprintf(stderr, "Insufficient arguments to sendf.");
                    thread->pc = -1;
                    return Tiny_Null;
                }
            }

            switch (*s) {
                case 'i': {
                    char is[32];
                    sprintf(is, "%i", Tiny_ToInt(args[arg]));

                    for (int i = 0; i < 32; ++i) {
                        if (is[i] == 0) break;
                        sb_push(buf, is[i]);
                    }
                } break;

                case 's': {
                    const char* ss = Tiny_ToString(args[arg]);

                    while (*ss) {
                        sb_push(buf, *ss++);
                    }
                } break;

                default: {
                    const char* fileName = NULL;
                    int line = 0;

                    Tiny_GetExecutingFileLine(thread, &fileName, &line);

                    fprintf(stderr, "%s(%i): Invalid format specifier '%%%c' in sendf.\n",
                            fileName, line, *s);
                } break;
            }

            s++;
            arg += 1;
        } else {
            sb_push(buf, *s++);
        }
    }

    int i = SockSend(&ctx->client, buf, sb_count(buf));

    sb_free(buf);

    return Tiny_NewInt(i);
}

static TINY_FOREIGN_FUNCTION(Sendb) {
    Context* ctx = thread->userdata;

    unsigned char** buf = Tiny_ToAddr(args[0]);

    return Tiny_NewInt(SockSend(&ctx->client, *(char**)buf, sb_count(*buf)));
}

static TINY_FOREIGN_FUNCTION(GetRequestMethod) {
    Context* ctx = thread->userdata;
    return Tiny_NewConstString(ctx->req.method);
}

static TINY_FOREIGN_FUNCTION(GetRequestTarget) {
    Context* ctx = thread->userdata;
    return Tiny_NewConstString(ctx->req.target);
}

static TINY_FOREIGN_FUNCTION(GetRequestBody) {
    Context* ctx = thread->userdata;

    char** pBuf = malloc(sizeof(char*));
    *pBuf = NULL;

    size_t len = strlen(ctx->req.body);

    char* start = sb_add(*pBuf, len);

    for (int i = 0; i < len; ++i) {
        start[i] = ctx->req.body[i];
    }

    return Tiny_NewNative(thread, pBuf, &BufProp);
}

static TINY_FOREIGN_FUNCTION(Lib_GetHeaderValue) {
    Context* ctx = thread->userdata;

    const char* name = Tiny_ToString(args[0]);

    const char* s = GetHeaderValue(&ctx->req, name);

    if (!s) return Tiny_Null;

    return Tiny_NewConstString(s);
}

static TINY_FOREIGN_FUNCTION(Stop) {
    thread->pc = -1;
    return Tiny_Null;
}

static Tiny_State* CreateState(const Config* c, const char* filename) {
    Tiny_State* state = Tiny_CreateState();

    Tiny_BindStandardLib(state);
    Tiny_BindStandardArray(state);
    Tiny_BindStandardDict(state);
    Tiny_BindStandardIO(state);

    BindBuffer(state);
    BindIO(state);
    BindTemplateUtils(state);
    BindHttpUtils(state);

    Tiny_BindFunction(state, "call_wait(str, ...): any", CallWait);

    Tiny_BindFunction(state, "sends(str): int", Sends);
    Tiny_BindFunction(state, "sendb(buf): int", Sendb);
    Tiny_BindFunction(state, "sendf(str, ...): int", Sendf);

    Tiny_BindFunction(state, "get_request_method(): str", GetRequestMethod);
    Tiny_BindFunction(state, "get_request_target(): str", GetRequestTarget);
    Tiny_BindFunction(state, "get_request_body(): buf", GetRequestBody);
    Tiny_BindFunction(state, "get_header_value(str): str", Lib_GetHeaderValue);

    Tiny_BindFunction(state, "stop(): void", Stop);

    for (int i = 0; i < sb_count(c->modules); ++i) {
        for (int j = 0; j < sb_count(c->modules[i].funcs); ++j) {
            Tiny_BindFunction(state, c->modules[i].funcs[j].sig, c->modules[i].funcs[j].func);
        }
    }

    for (int i = 0; i < sb_count(c->commonScripts); ++i) {
        Tiny_CompileFile(state, c->commonScripts[i]);
    }

    Tiny_CompileFile(state, filename);

    return state;
}

// Does nothing if the state is already loaded
static Tiny_State* LoadState(Server* serv, const char* filename) {
    LoopData* loop = &serv->loop;

    for (int i = 0; i < sb_count(loop->states); ++i) {
        if (strcmp(loop->states[i].filename, filename) == 0) {
            long long writeTime;

            if (GetLastWriteTime(filename, &writeTime)) {
                if (writeTime > loop->states[i].writeTime) {
                    bool inUse = false;

                    for (int i = 0; i < serv->conf.numThreads; ++i) {
                        if (loop->threads[i].pc >= 0 &&
                            loop->threads[i].state == loop->states[i].state) {
                            inUse = true;
                            break;
                        }
                    }

                    if (!inUse) {
                        Tiny_DeleteState(loop->states[i].state);

                        loop->states[i].state = CreateState(&serv->conf, filename);
                        loop->states[i].writeTime = writeTime;

                        printf("Reloaded script %s.\n", filename);
                    }
                }
            }

            return loop->states[i].state;
        }
    }

    StateFilename s;

    s.filename = estrdup(filename);
    s.state = CreateState(&serv->conf, filename);
    s.writeTime = 0;

    GetLastWriteTime(filename, &s.writeTime);

    sb_push(loop->states, s);

    return s.state;
}

static void DeleteContext(Context* ctx) {
    ReleaseSock(&ctx->client);
    DestroyRequest(&ctx->req);

    free(ctx);
}

static void LoopBody(Server* serv) {
    bool isActive = false;

    for (int i = 0; i < serv->conf.numThreads; ++i) {
        Tiny_StateThread* thread = &serv->loop.threads[i];

        if (thread->pc < 0 || Waiting(thread)) {
            continue;
        }

        bool willHaveRunningThread = true;

        for (int k = 0; k < serv->conf.cyclesPerLoop; ++k) {
            if (!Tiny_ExecuteCycle(thread)) {
                break;
            }

            if (Waiting(thread)) {
                willHaveRunningThread = false;
                break;
            }
        }

        if (thread->pc < 0) {
            printf("Completed job on StateThread %d (%s).\n", i,
                   ((Context*)thread->userdata)->req.target);

            DeleteContext(thread->userdata);
            Tiny_DestroyThread(thread);

            willHaveRunningThread = false;
        }

        if (willHaveRunningThread) {
            isActive = true;
        }
    }

    ClientRequest req;

    if (!ListPopFront(&serv->requestQueue, &req)) {
        if (!isActive) {
            mtx_lock(&serv->requestQueue.mutex);

            while (!serv->requestQueue.head && !isActive) {
                cnd_wait(&serv->updateLoop, &serv->requestQueue.mutex);

                // Re-check the threads to see if anyone is running because
                // we could've been woken up as a result of a waiting call
                // being completed
                for (int i = 0; i < serv->conf.numThreads; ++i) {
                    if (serv->loop.threads[i].pc >= 0 && !Waiting(&serv->loop.threads[i])) {
                        isActive = true;
                        break;
                    }
                }
            }

            mtx_unlock(&serv->requestQueue.mutex);
        }
    } else {
        // Look for a thread that's available, if there isn't one
        // requeue the request

        bool found = false;

        for (int i = 0; i < serv->conf.numThreads; ++i) {
            Tiny_StateThread* thread = &serv->loop.threads[i];

            if (serv->loop.threads[i].pc >= 0) {
                continue;
            }

            found = true;

            const char* filename = GetFilenameForTarget(&serv->conf, req.r.target);

            if (!filename) {
                printf("Failed to match target '%s'\n", req.r.target);

                const char* response =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 30\r\n"
                    "\r\n"
                    "That webpage does not exist.\r\n";

                SockSend(&req.client, response, strlen(response));
                break;
            }

            Tiny_State* state = LoadState(serv, filename);

            if (!state) {
                fprintf(stderr, "Missing script '%s'\n", filename);

                const char* response =
                    "HTTP/1.1 500 Internal Server Error\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 45\r\n"
                    "\r\n"
                    "I'm sorry but someone messed up the server.\r\n";

                SockSend(&req.client, response, strlen(response));
                break;
            }

            Context* ctx = malloc(sizeof(Context));

            ctx->req = req.r;

            ctx->client = req.client;
            RetainSock(&ctx->client);

            ctx->serv = serv;

            ctx->waiting = 0;

            Tiny_InitThread(thread, state);

            thread->userdata = ctx;
            Tiny_StartThread(thread);

            printf("Started StateThread %d (%s) to handle %s %s\n", i, filename, req.r.method,
                   req.r.target);
            break;
        }

        if (!found) {
            ListPushBack(&serv->requestQueue, &req);
        }
    }

    // We loop through all StateThreads, executing cycles on them.
    // If any of them finish, we finalize their contexts (decrement
    // socket refcount and destroy the parsed request).
    //
    // we then acquire a lock on the requests list, and check if
    // there are any requests pending. If there are no requests
    // pending and all StateThreads are idle (i.e. pc <= 0), we wait
    // on the condition variable notEmpty in the list. This means the
    // thread will idle until there's some processing to do (which makes sense).
    // If there is at least one executing state, then we just unlock
    // the mutex as soon as we see there are no immediate requests and then
    // the loop restarts.
    //
    // Once a request comes in (whether we see it when we're checking immediately
    // or the condition variable wakes this thread up), if there is a StateThread
    // available, it is parsed, its context is allocated/setup (the parsed request,
    // the socket (with a refcount), and the server), and the StateThread is
    // started. It will be responsible for carrying out the request.
    //
    // For long, blocking operations, I can expose a function like:
    //
    // call_suspend("function_name", args...)
    //
    // and what that will do is it will set a flag in the StateThread
    // context that it is blocking and spin up an OS thread, passing
    // in a struct with all the args supplied to call_suspend and
    // a pointer to the StateThread (note that the StateThread doesn't
    // get copied). This thread will do a Tiny_CallFunction to
    // the given "function_name" passing in the args. Once the CallFunction
    // returns, it will set the retval to whatever the function returned,
    // it will atomically set the blocking flag to false and
    // exit the thread.
    //
    // Meanwhile, CallSuspend, after setting the thread to being blocked, will
    // return immediately. No further cycles would be executed on the thread.
    // Let's say the next instruction was an OP_GET_RETVAL. Once the thread is
    // done with the blocking call, whatever function_name returned is going to
    // be pushed onto the stack (because the thread sets the retVal of the
    // StateThread to whatever function_name returned) once that GET_RETVAL
    // is executed.
    //
    // Basically, this will allow scripts to do things like:
    //
    // all_users: rows = call_suspend("db_run", "SELECT * FROM USERS;")
    //
    // Doing this without suspending would block this loop for a while, but we don't want
    // that, and we don't want many threads going at the same time either
    // which is why the fast stuff happens in the loop and the slow stuff
    // happens in the call_suspend threads.
}

extern volatile bool KeepRunning;

int MainLoop(void* pServ) {
    Server* serv = pServ;

    serv->loop.states = NULL;

    // Preload all states
    for (int i = 0; i < sb_count(serv->conf.routes); ++i) {
        LoadState(serv, serv->conf.routes[i].filename);
    }

    serv->loop.threads = malloc(sizeof(Tiny_StateThread) * serv->conf.numThreads);

    for (int i = 0; i < serv->conf.numThreads; ++i) {
        serv->loop.threads[i].pc = -1;
    }

    while (KeepRunning) {
        LoopBody(serv);
    }

    // Finish up every StateThread
    for (int i = 0; i < serv->conf.numThreads; ++i) {
        Tiny_StateThread* thread = &serv->loop.threads[i];

        bool deleteContext = false;

        if (Waiting(thread)) {
            Context* ctx = thread->userdata;
            thrd_join(&ctx->worker, NULL);

            deleteContext = true;
        }

        deleteContext = deleteContext || thread->pc >= 0;

        while (Tiny_ExecuteCycle(thread))
            ;

        if (deleteContext) {
            DeleteContext(thread->userdata);
            Tiny_DestroyThread(thread);
        }
    }

    for (int i = 0; i < sb_count(serv->loop.states); ++i) {
        Tiny_DeleteState(serv->loop.states[i].state);
    }

    return 0;
}
