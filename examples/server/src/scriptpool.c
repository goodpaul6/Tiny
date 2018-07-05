#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "tiny.h"
#include "scriptpool.h"
#include "util.h"
#include "tinycthread.h"

typedef struct
{
    long long writeTime;
    char* filename;
    Tiny_State* state;
} State;

typedef struct StartRequest
{
    const Tiny_State* state;
    void* ctx;

    struct StartRequest* next;
} StartRequest;

struct ScriptPool
{
    InitStateFunction initState;
    FinalizeContextFunction finalizeContext;

    State* states;  // array

    mtx_t threadsMutex;

    int numThreads;
    Tiny_StateThread* threads;

    mtx_t requestsMutex;

    StartRequest* firstRequest;
    StartRequest* lastRequest;
};

static Tiny_State* CreateState(ScriptPool* pool, const char* filename)
{
    Tiny_State* state = Tiny_CreateState();

    Tiny_BindStandardLib(state);
    Tiny_BindStandardIO(state);

    if(pool->initState) pool->initState(state);

    Tiny_CompileFile(state, filename);

	return state;
}

// Does nothing if the state is already loaded
static Tiny_State* LoadState(ScriptPool* pool, const char* filename)
{
    for(int i = 0; i < sb_count(pool->states); ++i) {
        if(strcmp(pool->states[i].filename, filename) == 0) {
            long long writeTime;

            if(GetLastWriteTime(filename, &writeTime)) {
                if(writeTime > pool->states[i].writeTime) {
                    bool inUse = false;

					mtx_lock(&pool->threadsMutex);

                    for(int i = 0; i < pool->numThreads; ++i) {
                        if(pool->threads[i].pc >= 0 && pool->threads[i].state == pool->states[i].state) {
                            inUse = true;
                            break;
                        }
                    }

                    if(!inUse) {
                        Tiny_DeleteState(pool->states[i].state);

						pool->states[i].state = CreateState(pool, filename);
                        pool->states[i].writeTime = writeTime;

                        printf("Reloaded script %s.\n", filename);
                    }

					mtx_unlock(&pool->threadsMutex);
                }
            }

            return pool->states[i].state;
        }
    }

    State s;

    s.filename = estrdup(filename);
	s.state = CreateState(pool, filename);
    s.writeTime = 0;

    GetLastWriteTime(filename, &s.writeTime);

    sb_push(pool->states, s);

    return s.state;
}

ScriptPool* CreateScriptPool(int numThreads, InitStateFunction initState, FinalizeContextFunction finalizeContext)
{
    assert(numThreads > 0);

    ScriptPool* pool = malloc(sizeof(ScriptPool));

	pool->initState = initState;
	pool->finalizeContext = finalizeContext;

    pool->states = NULL;

    if(mtx_init(&pool->threadsMutex, mtx_plain) != thrd_success) {
        fprintf(stderr, "Could not initialize ScriptPool threads mutex.\n");

        free(pool);
        return NULL;
    }

    pool->numThreads = numThreads;
    pool->threads = malloc(sizeof(Tiny_StateThread) * numThreads);

    for(int i = 0; i < numThreads; ++i) {
        pool->threads[i].pc = -1;
    }

    if(mtx_init(&pool->requestsMutex, mtx_plain) != thrd_success) {
        fprintf(stderr, "Could not initialize ScriptPool requests mutex.\n");

        free(pool);
        return NULL;
    }

    pool->firstRequest = pool->lastRequest = NULL;

    return pool;
}

static bool StartThread(ScriptPool* pool, const Tiny_State* state, void* ctx)
{
    // TODO(Apaar): Check if lock/unlock succeed

    // We lock this out here instead of inside the if because 
    // StartThread can be called by both the listening thread
    // and the main thread: 
    //
    // if there was a request queued
    // up, for example, and UpdateScriptPool was happening,
    // it could call StartThread. At the same time, a request
    // comes in and StartThread is called from the listening
    // thread. They can both find a thread with pc < 0 at the 
    // same time and init the thread with two different states/contexts
    // for example.
    if(mtx_trylock(&pool->threadsMutex) == thrd_busy) {
        return false;
    }

    for(int i = 0; i < pool->numThreads; ++i) {
        if(pool->threads[i].pc < 0) {

            Tiny_InitThread(&pool->threads[i], state);
        
            pool->threads[i].userdata = ctx;
            Tiny_StartThread(&pool->threads[i]);

            mtx_unlock(&pool->threadsMutex);
            
            return true;
        }
    }

    mtx_unlock(&pool->threadsMutex);

    return false;
}

void StartScript(ScriptPool* pool, const char* filename, void* ctx)
{
    Tiny_State* s = LoadState(pool, filename);

    if(!StartThread(pool, s, ctx)) {
        mtx_lock(&pool->requestsMutex);

        StartRequest* r = malloc(sizeof(StartRequest));

        r->state = s;
        r->ctx = ctx;

        r->next = NULL;

        if(!pool->lastRequest) {
            pool->firstRequest = pool->lastRequest = r;
        } else {
            pool->lastRequest->next = r;
            pool->lastRequest = r;
        }

        mtx_unlock(&pool->requestsMutex);
    }
}

void UpdateScriptPool(ScriptPool* pool, int n)
{
    mtx_lock(&pool->threadsMutex);

    for(int i = 0; i < pool->numThreads; ++i) {
        if(pool->threads[i].pc < 0) continue;

        for(int k = 0; k < n; ++k) {
            Tiny_ExecuteCycle(&pool->threads[i]);

            if(pool->threads[i].pc < 0) {
                // Just finished
                if(pool->finalizeContext) pool->finalizeContext(pool->threads[i].userdata);
                Tiny_DestroyThread(&pool->threads[i]);

                break;
            }
        }
    }

    mtx_unlock(&pool->threadsMutex);

    mtx_lock(&pool->requestsMutex);

	if (!pool->firstRequest) {
		mtx_unlock(&pool->requestsMutex);
		return;
	}

    // If we successfully start a queued up script, then we remove it from the queue
    if(StartThread(pool, pool->firstRequest->state, pool->firstRequest->ctx)) {
        StartRequest* next = pool->firstRequest->next;

        free(pool->firstRequest);

        if(pool->firstRequest == pool->lastRequest) {
            pool->lastRequest = NULL;
        }

        pool->firstRequest = next;
    }

    mtx_unlock(&pool->requestsMutex);
}

void DeleteScriptPool(ScriptPool* pool)
{
    // Destroy all running threads
    for(int i = 0; i < pool->numThreads; ++i) {
        if(pool->threads[i].pc < 0) continue;

        if(pool->finalizeContext) pool->finalizeContext(pool->threads[i].userdata);
        Tiny_DestroyThread(&pool->threads[i]);
    }

    for(int i = 0; i < sb_count(pool->states); ++i) {
        free(pool->states[i].filename);
        Tiny_DeleteState(pool->states[i].state);
    }

    sb_free(pool->states);

    free(pool->threads);

    free(pool);

    mtx_destroy(&pool->threadsMutex);
    mtx_destroy(&pool->requestsMutex);
}
