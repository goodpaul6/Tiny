#pragma once

#include <cstdint>

namespace tiny {

enum class Opcode : std::uint8_t {
    OP_PUSH_NULL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_PUSH_CHAR,
    OP_PUSH_INT,
    OP_PUSH_FLOAT,
    OP_PUSH_STRING,

    OP_ADD_I,
    OP_ADD_F,
    OP_ADD_S,

    OP_SUB_I,
    OP_SUB_F,

    OP_MUL_I,
    OP_MUL_F,

    OP_DIV_I,
    OP_DIV_F,

    OP_GET_LOCAL,
    OP_SET_LOCAL,

    OP_GET_GLOBAL,
    OP_SET_GLOBAL,

    OP_CALL,
    OP_RETURN,
};

}
