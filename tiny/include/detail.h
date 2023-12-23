#pragma once

#include "lexer.h"
#include "tiny.h"

#define MAX_NUMBERS 512
#define MAX_STRINGS 1024

typedef unsigned char Word;

typedef struct Tiny_Object {
    bool marked;

    Tiny_ValueType type;
    struct Tiny_Object *next;

    union {
        struct {
            size_t len;
            // NOTE(Apaar): If the user calls "NewStringWithLen" then this is just
            // a pointer to the same allocation as this object!
            //
            // Otherwise (if calling NewString)
            char *ptr;
        } string;

        struct {
            void *addr;
            const Tiny_NativeProp *prop;  // Can be used to check type of native (ex. obj->nat.prop
                                          // == &ArrayProp // this is an Array)
        } nat;

        struct {
            Word n;
            Tiny_Value fields[];
        } ostruct;
    };
} Tiny_Object;

typedef struct Tiny_State {
    Tiny_Context ctx;

    // Program info
    Word *program;  // array

    int numStrings;
    char *strings[MAX_STRINGS];

    int numGlobalVars;

    int numFunctions;
    int *functionPcs;

    int numForeignFunctions;
    Tiny_ForeignFunction *foreignFunctions;

    // Compiler Info
    int currScope;
    Tiny_Symbol *currFunc;

    Tiny_Symbol **globalSymbols;  // array

    Tiny_Lexer l;
} Tiny_State;
