#pragma once

#include <stdbool.h>

#include "tokens.h"

typedef struct {
    const char *fileName;
    const char *src;

    int lineNumber;
    Tiny_TokenPos pos;

    int last;
    char *lexeme;  // array

    union {
        bool bValue;
        int iValue;
        float fValue;
    };
} Tiny_Lexer;

void Tiny_InitLexer(Tiny_Lexer *l, const char *fileName, const char *src);

Tiny_TokenKind Tiny_GetToken(Tiny_Lexer *l);

void Tiny_DestroyLexer(Tiny_Lexer *l);
