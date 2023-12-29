#pragma once

#include "tiny.h"

// Struct for the Tiny AST. Placed into its own header for
// exposition.

typedef enum {
    EXP_ID,
    EXP_CALL,
    EXP_NULL,
    EXP_BOOL,
    EXP_CHAR,
    EXP_INT,
    EXP_FLOAT,
    EXP_STRING,
    EXP_BINARY,
    EXP_PAREN,
    EXP_BLOCK,
    EXP_PROC,
    EXP_IF,
    EXP_UNARY,
    EXP_RETURN,
    EXP_WHILE,
    EXP_FOR,
    EXP_DOT,
    EXP_CONSTRUCTOR,
    EXP_CAST,
    EXP_BREAK,
    EXP_CONTINUE,
    EXP_USE,
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
