#pragma once

enum
{
    // Moves the stack by the number of values that follows this (uint8_t)
    // For creating room on the stack
    TINY_OP_ADD_SP,

    // Value pushing ops
    TINY_OP_PUSH_NULL,
    TINY_OP_PUSH_TRUE,
    TINY_OP_PUSH_FALSE,
    TINY_OP_PUSH_C,
	TINY_OP_PUSH_I,
    TINY_OP_PUSH_I_0,
	TINY_OP_PUSH_F,
    TINY_OP_PUSH_F_0,

    // uintptr_t after this instruction is pointer to the pooled string in the
    // Tiny_State owner. 
    // TODO(Apaar): This makes serializing the bytecode more difficult; before
    // serializing, iterate through the StringPool map and convert the pointers to
    // indices. 
    TINY_OP_PUSH_S,

    // TODO(Apaar): Structs; should probably just be an TINY_OP_ALLOC followed by the
    // struct size. All allocations should end up in the GC heap

    // Integer ops
    TINY_OP_ADD_I,
    TINY_OP_SUB_I,
    TINY_OP_MUL_I,
    TINY_OP_DIV_I,
    TINY_OP_MOD_I,
    TINY_OP_OR_I,
    TINY_OP_AND_I,

    TINY_OP_ADD1_I,
    TINY_OP_SUB1_I,

    TINY_OP_LT_I,
    TINY_OP_LTE_I,
    TINY_OP_GT_I,
    TINY_OP_GTE_I,

    // Float ops
    TINY_OP_ADD_F,
    TINY_OP_SUB_F,
    TINY_OP_MUL_F,
    TINY_OP_DIV_F,
    
    TINY_OP_LT_F,
    TINY_OP_LTE_F,
    TINY_OP_GT_F,
    TINY_OP_GTE_F,

    // Equality ops
    TINY_OP_EQU_B,
    TINY_OP_EQU_C,
    TINY_OP_EQU_I,
    TINY_OP_EQU_F,
    TINY_OP_EQU_S,

    // Bool ops
    TINY_OP_LOG_AND,
    TINY_OP_LOG_OR,
    TINY_OP_LOG_NOT,
    
    // Jumps 
    TINY_OP_GOTO,
    TINY_OP_GOTO_FALSE,

    // Calls
    TINY_OP_CALL,       

    // TODO(Apaar): TINY_OP_CALL_F

    // Return ops
    TINY_OP_RET,
    TINY_OP_RETVAL,
    TINY_OP_GET_RETVAL,

    TINY_OP_HALT,

    TINY_OP_FILE,
    TINY_OP_LINE,

    TINY_OP_MISALIGNED_INSTRUCTION
};
