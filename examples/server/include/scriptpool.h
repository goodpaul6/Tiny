#pragma once

typedef struct Tiny_State Tiny_State;
typedef struct ScriptPool ScriptPool;

// Called before a state is compiled. You can bind all your stuff in this
typedef void (*InitStateFunction)(Tiny_State*);

typedef void (*FinalizeContextFunction)(void*);

// initState can be null, so can finalizeContext
ScriptPool* CreateScriptPool(int numThreads, InitStateFunction initState, FinalizeContextFunction finalizeContext);

// If a thread isn't available for execution, then this will queue the request
// This is thread-safe.
void StartScript(ScriptPool* pool, const char* filename, void* ctx);

// Starts a queued script if a thread is available and executes
// n cycles on all running threads. If a script is completed, then
// the finalizeContext function is called
// This is thread-safe
void UpdateScriptPool(ScriptPool* pool, int n);

void DeleteScriptPool(ScriptPool* pool);

