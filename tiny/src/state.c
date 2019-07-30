#include <string.h>

#include "stringpool.h"
#include "state.h"

enum
{
    // Moves the stack by the number of values that follows this (uint8_t)
    // For creating room on the stack
    OP_ADD_SP,

    // Value pushing ops
    OP_PUSH_NULL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_PUSH_C,
	OP_PUSH_I,
    OP_PUSH_I_0,
	OP_PUSH_F,
    OP_PUSH_F_0,

    // uintptr_t after this instruction is pointer to the pooled string in the
    // Tiny_State owner. 
    // TODO(Apaar): This makes serializing the bytecode more difficult; before
    // serializing, iterate through the StringPool map and convert the pointers to
    // indices. 
    OP_PUSH_S,

    // TODO(Apaar): Structs; should probably just be an TINY_OP_ALLOC followed by the
    // struct size. All allocations should end up in the GC heap

    // Integer ops
    OP_ADD_I,
    OP_SUB_I,
    OP_MUL_I,
    OP_DIV_I,
    OP_MOD_I,
    OP_OR_I,
    OP_AND_I,

    OP_ADD1_I,
    OP_SUB1_I,

    OP_LT_I,
    OP_LTE_I,
    OP_GT_I,
    OP_GTE_I,

    // Float ops
    OP_ADD_F,
    OP_SUB_F,
    OP_MUL_F,
    OP_DIV_F,
    
    OP_LT_F,
    OP_LTE_F,
    OP_GT_F,
    OP_GTE_F,

    // Equality ops
    OP_EQU_B,
    OP_EQU_C,
    OP_EQU_I,
    OP_EQU_F,
    OP_EQU_S,

    // Bool ops
    OP_LOG_AND,
    OP_LOG_OR,
    OP_LOG_NOT,
    
    // Jumps 
    OP_GOTO,
    OP_GOTO_FALSE,

    // Calls
    OP_CALL,       

    // TODO(Apaar): TINY_OP_CALL_F

    // Return ops
    OP_RET,
    OP_RETVAL,
    OP_GET_RETVAL,

    OP_HALT,

    OP_FILE,
    OP_LINE,

    OP_MISALIGNED_INSTRUCTION
};

typedef struct Tiny_State 
{
    Tiny_Context* ctx;

    Tiny_StringPool sp;

    // Buffer
    float* numbers;

    // Buffer
    uint8_t* code;
} Tiny_State;

static void InitState(Tiny_State* state, Tiny_Context* ctx)
{
    state->ctx = ctx;

    Tiny_InitStringPool(&state->sp, ctx);

    INIT_BUF(state->numbers, ctx);
    INIT_BUF(state->code, ctx);
}

static void DestroyState(Tiny_State* state)
{
    DESTROY_BUF(state->code);
    DESTROY_BUF(state->numbers);

    Tiny_DestroyStringPool(&state->sp);
}
