#pragma once

#include "tiny.h"

// Struct for the Tiny AST. Placed into its own header for
// exposition.

typedef enum {
    TINY_EXP_ID,
    TINY_EXP_CALL,
    TINY_EXP_NULL,
    TINY_EXP_BOOL,
    TINY_EXP_CHAR,
    TINY_EXP_INT,
    TINY_EXP_FLOAT,
    TINY_EXP_STRING,
    TINY_EXP_BINARY,
    TINY_EXP_PAREN,
    TINY_EXP_BLOCK,
    TINY_EXP_PROC,
    TINY_EXP_IF,
    TINY_EXP_UNARY,
    TINY_EXP_RETURN,
    TINY_EXP_WHILE,
    TINY_EXP_FOR,
    TINY_EXP_DOT,
    TINY_EXP_CONSTRUCTOR,
    TINY_EXP_CAST,
    TINY_EXP_BREAK,
    TINY_EXP_CONTINUE,
    TINY_EXP_USE,
} Tiny_ExprType;

typedef struct Tiny_Expr {
    Tiny_ExprType type;

    Tiny_TokenPos pos;
    int lineNumber;

    Tiny_Symbol *tag;

    union {
        bool boolean;

        int iValue;
        float fValue;
        int sIndex;

        struct {
            char *name;
            Tiny_Symbol *sym;
        } id;

        struct {
            char *calleeName;
            struct Tiny_Expr **args;  // array
        } call;

        struct {
            struct Tiny_Expr *lhs;
            struct Tiny_Expr *rhs;
            int op;
        } binary;

        struct Tiny_Expr *paren;

        struct {
            int op;
            struct Tiny_Expr *exp;
        } unary;

        struct Tiny_Expr **block;  // array

        struct {
            Tiny_Symbol *decl;
            struct Tiny_Expr *body;
        } proc;

        struct {
            struct Tiny_Expr *cond;
            struct Tiny_Expr *body;
            struct Tiny_Expr *alt;
        } ifx;

        struct {
            struct Tiny_Expr *cond;
            struct Tiny_Expr *body;
        } whilex;

        struct {
            struct Tiny_Expr *init;
            struct Tiny_Expr *cond;
            struct Tiny_Expr *step;
            struct Tiny_Expr *body;
        } forx;

        struct {
            struct Tiny_Expr *lhs;
            char *field;
        } dot;

        struct {
            Tiny_Symbol *structTag;

            // This is only used for if the constructor has named arguments. Otherwise this is
            // NULL.
            //
            // If specified, these will get re-ordered (alongside the args) during the
            // `ResolveTypes` phase to ensure they're in the order that the struct wants.
            char **argNames;

            struct Tiny_Expr **args;
        } constructor;

        struct {
            struct Tiny_Expr *value;
            Tiny_Symbol *tag;
        } cast;

        struct Tiny_Expr *retExpr;

        struct {
            // For a break, this index in the bytecode should be patched with the pc at the exit of
            // the loop. For a continue, this index should be patched with the PC before the
            // conditional.
            int patchLoc;
        } breakContinue;

        struct {
            char *moduleName;
            char **args;  // array
            char *asName;
        } use;
    };
} Tiny_Expr;
