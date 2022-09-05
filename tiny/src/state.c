#include "state.h"

#include <string.h>

#include "map.h"
#include "opcodes.h"
#include "stringpool.h"

// These "Roots" are necessary in order for the VM to distinguish
// GC roots from other values. This information is available
// in the symbol table of the parser, but this is basically
// an optimization so that the GC does not have to iterate
// through the symbol table.
typedef struct GlobalRoots {
    // Buffer
    // Offset into globals array
    uint32_t* indices;
} GlobalRoots;

typedef struct Tiny_State {
    Tiny_Context* ctx;

    // String pool shared across all the modules so
    // we can compare strings across modules easily.
    Tiny_StringPool sp;

    // Map from module name to Parser
    Tiny_Map modules;

    // TODO We should really also have error codes.
    // Compilation error message.
    char* errorMessage;

    GlobalRoots globalRoots;

    // Buffer
    uint32_t* functionPcs;

    // Buffer
    Tiny_LocalRoots* localRoots;

    // Buffer
    uint8_t* code;
} Tiny_State;

static void InitState(Tiny_State* state, Tiny_Context* ctx) {
    state->ctx = ctx;

    Tiny_InitMap(&state->modules, ctx);

    INIT_BUF(state->globalRoots.indices, ctx);

    INIT_BUF(state->functionPcs, ctx);
    INIT_BUF(state->localRoots, ctx);

    INIT_BUF(state->code, ctx);
}

static void DestroyState(Tiny_State* state) {
    for (int i = 0; i < state->modules.cap; ++i) {
        if (state->modules.values[i]) {
            DestroyParser(state->modules.values[i]);
        }
    }

    Tiny_DestroyMap(&state->modules);

    DESTROY_BUF(state->globalRoots.indices);

    DESTROY_BUF(state->functionPcs);

    for (int i = 0; i < BUF_LEN(state->localRoots); ++i) {
        DESTROY_BUF(state->localRoots[i].indices);
    }

    DESTROY_BUF(state->localRoots);

    DESTROY_BUF(state->code);
}
