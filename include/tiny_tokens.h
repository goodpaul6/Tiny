#pragma once

typedef enum
{
    TINY_TOK_OPENPAREN,
    TINY_TOK_CLOSEPAREN,
    TINY_TOK_OPENCURLY,
    TINY_TOK_CLOSECURLY,

    TINY_TOK_PLUS,
    TINY_TOK_MINUS,
    TINY_TOK_STAR,
    TINY_TOK_SLASH,
    TINY_TOK_PERCENT,
    TINY_TOK_GT,
    TINY_TOK_LT,
    TINY_TOK_EQUAL,
    TINY_TOK_BANG,
    TINY_TOK_AND,
    TINY_TOK_OR,
    TINY_TOK_COMMA,
    TINY_TOK_SEMI,
    TINY_TOK_COLON,

    TINY_TOK_LOG_AND,                   // &&
    TINY_TOK_LOG_OR,                    // ||

    TINY_TOK_DECLARE,               // :=
    TINY_TOK_DECLARECONST,          // ::

    TINY_TOK_PLUSEQUAL,             // +=
    TINY_TOK_MINUSEQUAL,            // -=
    TINY_TOK_STAREQUAL,              // *=
    TINY_TOK_SLASHEQUAL,              // /=
    TINY_TOK_PERCENTEQUAL,              // %=
    TINY_TOK_OREQUAL,               // |=
    TINY_TOK_ANDEQUAL,              // &=

    TINY_TOK_EQUALS,
    TINY_TOK_NOTEQUALS,
    TINY_TOK_LTE,
    TINY_TOK_GTE,

    TINY_TOK_NULL,
    TINY_TOK_BOOL,
    TINY_TOK_CHAR,
    TINY_TOK_INT,
    TINY_TOK_FLOAT,
    TINY_TOK_STRING,

    TINY_TOK_IDENT,

    TINY_TOK_IF,
    TINY_TOK_ELSE,
    TINY_TOK_WHILE,
    TINY_TOK_FOR,
    TINY_TOK_RETURN,
    TINY_TOK_FUNC,
    TINY_TOK_FOREIGN,

    TINY_TOK_EOF,
} Tiny_TokenKind;

typedef int Tiny_TokenPos;