#pragma once

#include <cstdint>

namespace tiny {

enum class TokenType : std::uint8_t {
    OPENPAREN,
    CLOSEPAREN,
    OPENCURLY,
    CLOSECURLY,
    OPENSQUARE,
    CLOSESQUARE,

    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    GT,
    LT,
    EQUAL,
    BANG,
    AND,
    OR,
    COMMA,
    SEMI,
    COLON,
    DOT,

    LOG_AND,
    LOG_OR,

    DECLARE,
    DECLARE_CONST,

    PLUS_EQUAL,
    MINUS_EQUAL,
    STAR_EQUAL,
    SLASH_EQUAL,
    PERCENT_EQUAL,
    OR_EQUAL,
    AND_EQUAL,

    EQUALS,
    NOT_EQUALS,
    LT_EQUALS,
    GT_EQUALS,

    NULL_VALUE,
    BOOL_VALUE,
    CHAR_VALUE,
    INT_VALUE,
    FLOAT_VALUE,
    STRING_VALUE,

    IDENT,

    IF,
    ELSE,
    WHILE,
    FOR,
    RETURN,
    FUNC,
    STRUCT,
    NEW,
    CAST,

    SUB
};
}
