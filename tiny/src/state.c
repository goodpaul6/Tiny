#include <string.h>

#include "opcodes.h"
#include "stringpool.h"

#include "state.h"

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

    Parser parser;

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

    InitParser(&state->parser, ctx);

    INIT_BUF(state->globalRoots.indices, ctx);

    INIT_BUF(state->functionPcs, ctx);
    INIT_BUF(state->localRoots, ctx);

    INIT_BUF(state->code, ctx);
}

static void DestroyState(Tiny_State* state) {
    DestroyParser(&state->parser);

    DESTROY_BUF(state->globalRoots.indices);

    DESTROY_BUF(state->functionPcs);

    for (int i = 0; i < BUF_LEN(state->localRoots); ++i) {
        DESTROY_BUF(state->localRoots[i].indices);
    }

    DESTROY_BUF(state->localRoots);

    DESTROY_BUF(state->code);
}

inline static uint32_t GetGenPC(Tiny_State* state) {
    assert(BUF_LEN(state->code) <= UINT32_MAX);
    return (uint32_t)BUF_LEN(state->code);
}

inline static void AllocateFunctions(Tiny_State* state, uint32_t funcCount) {
    assert(BUF_LEN(state->functionPcs) == 0);

    BUF_ADD(state->functionPcs, funcCount);

    void* roots = BUF_ADD(state->localRoots, funcCount);
    memset(roots, 0, sizeof(Tiny_LocalRoots) * funcCount);
}

inline static void SetFuncStartHere(Tiny_State* state, uint32_t funcIndex) {
    state->functionPcs[funcIndex] = GetGenPC(state);
}

inline static void GenOp(Tiny_State* state, uint8_t op) { BUF_PUSH(state->code, op); }

inline static void GenAlign(Tiny_State* state, size_t align) {
    size_t move = ((BUF_LEN(state->code) + align) & ~(align - 1)) - BUF_LEN(state->code);

    while (move--) {
        BUF_PUSH(state->code, OP_MISALIGNED_INSTRUCTION);
    }
}

// Returns an index to start of patchable memory
inline static uint32_t GenPatch(Tiny_State* state, size_t align, size_t len) {
    GenAlign(state, align);
    void* dest = BUF_ADD(state->code, len);

    ptrdiff_t off = (ptrdiff_t)((uintptr_t)dest - (uintptr_t)state->code);
    assert(off <= UINT32_MAX);

    return (uint32_t)off;
}

inline static void FillPatch(Tiny_State* state, uint32_t pos, const void* src, size_t len) {
    memcpy(&state->code[pos], src, len);
}

inline static void GenBytes(Tiny_State* state, size_t align, const void* src, size_t len) {
    uint32_t pos = GenPatch(state, align, len);
    FillPatch(state, pos, src, len);
}

inline static void GenInt(Tiny_State* state, int i) { GenBytes(state, alignof(i), &i, sizeof(i)); }

inline static void GenUint32(Tiny_State* state, uint32_t u) {
    GenBytes(state, alignof(u), &u, sizeof(u));
}

inline static void GenStartFunc(Tiny_State* state) {
    assert(BUF_LEN(state->functionPcs) < UINT32_MAX);
    BUF_PUSH(state->functionPcs, GetGenPC(state));
}

inline static void GenPushChar(Tiny_State* state, uint32_t c) {
    GenOp(state, OP_PUSH_C);
    GenUint32(state, c);
}

inline static void GenPushInt(Tiny_State* state, int i) {
    if (i == 0) {
        GenOp(state, OP_PUSH_I_0);
    } else {
        GenOp(state, OP_PUSH_I);
        GenInt(state, i);
    }
}

inline static void GenPushFloat(Tiny_State* state, float f) {
    if (f == 0) {
        GenOp(state, OP_PUSH_F_0);
    } else {
        GenOp(state, OP_PUSH_F);
        GenBytes(state, alignof(float), &f, sizeof(float));
    }
}

inline static void GenCall(Tiny_State* state, uint8_t nargs, uint32_t functionIndex) {
    GenOp(state, OP_CALL);
    BUF_PUSH(state->code, nargs);
    GenUint32(state, functionIndex);
}

inline static uint32_t GenGotoPatch(Tiny_State* state, uint8_t op) {
    assert(op == OP_GOTO || op == OP_GOTO_FALSE);

    GenOp(state, op);
    return GenPatch(state, alignof(uint32_t), sizeof(uint32_t));
}

inline static void GenGoto(Tiny_State* state, uint8_t op, uint32_t dest) {
    uint32_t pos = GenGotoPatch(state, op);
    FillPatch(state, pos, &dest, sizeof(dest));
}

inline static void GenGetOrSet(Tiny_State* state, uint8_t op, int64_t pos) {
    assert(op == OP_GET || op == OP_GET_LOCAL || op == OP_SET || op == OP_SET_LOCAL);

    GenOp(state, op);

    switch (op) {
        case OP_GET:
        case OP_SET: {
            assert(pos >= 0 && pos < UINT32_MAX);
            GenUint32(state, (uint32_t)pos);
        } break;

        case OP_GET_LOCAL:
        case OP_SET_LOCAL: {
            assert(pos >= INT8_MIN && pos <= INT8_MAX);

            int8_t off = (int8_t)pos;

            uint8_t dest;
            memcpy(&dest, &off, sizeof(dest));

            BUF_PUSH(state->code, dest);
        } break;
    }
}

// TODO(Apaar): Once constant folding is in, add GenAddConstInt
// that makes use of ADD1_I, SUB1_I, etc
