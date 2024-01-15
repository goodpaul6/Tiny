#ifndef TINY_OPCODES_H
#define TINY_OPCODES_H

enum {
    TINY_OP_PUSH_NULL,

    // Pushes N null values onto the stack (where n <= 0xff)
    TINY_OP_PUSH_NULL_N,

    TINY_OP_PUSH_TRUE,
    TINY_OP_PUSH_FALSE,

    TINY_OP_PUSH_INT,

    // Fast integer ops
    TINY_OP_PUSH_0,
    TINY_OP_PUSH_1,
    TINY_OP_PUSH_CHAR,

    TINY_OP_PUSH_FLOAT,

    TINY_OP_PUSH_STRING,
    // If the string is in the first 0xff constants, use this opcode
    TINY_OP_PUSH_STRING_FF,

    TINY_OP_PUSH_STRUCT,

    TINY_OP_STRUCT_GET,
    TINY_OP_STRUCT_SET,

    TINY_OP_ADD,
    TINY_OP_SUB,
    TINY_OP_MUL,
    TINY_OP_DIV,
    TINY_OP_MOD,
    TINY_OP_OR,
    TINY_OP_AND,
    TINY_OP_LT,
    TINY_OP_LTE,
    TINY_OP_GT,
    TINY_OP_GTE,

    TINY_OP_ADD1,
    TINY_OP_SUB1,

    TINY_OP_EQU,

    TINY_OP_LOG_NOT,

    TINY_OP_SET,
    TINY_OP_GET,

    TINY_OP_GOTO,
    TINY_OP_GOTOZ,

    TINY_OP_CALL,
    TINY_OP_RETURN,
    TINY_OP_RETURN_VALUE,

    TINY_OP_CALLF,

    TINY_OP_GETLOCAL,
    // If the local index >= 0 and <= 0xff
    TINY_OP_GETLOCAL_W,

    TINY_OP_SETLOCAL,

    TINY_OP_GET_RETVAL,

    TINY_OP_HALT,

    TINY_OP_MISALIGNED_INSTRUCTION
} Tiny_Opcode;

#endif

