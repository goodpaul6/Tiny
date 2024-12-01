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
    TINY_EXP_INDEX,
    TINY_EXP_IF_TERNARY,
} Tiny_ExprType;

// Node for a singly-linked list of strings.
//
// Occasionally used for individual strings too so that we don't have to bother with
// having a separate special case for individual strings (e.g. moduleName below).
typedef struct Tiny_StringNode {
    struct Tiny_StringNode *next;
    char *value;
} Tiny_StringNode;

typedef struct Tiny_Expr {
    Tiny_ExprType type;

    Tiny_TokenPos pos;
    int lineNumber;

    Tiny_Symbol *tag;

    // Arrays not worth it; return to intrusive linked list.
    // We make use of memory arenas to eliminate having to
    // manage this memory ourselves.
    struct Tiny_Expr *next;

    union {
        bool boolean;

        int iValue;
        float fValue;
        int sIndex;

        struct {
            Tiny_StringNode *name;
            Tiny_Symbol *sym;
        } id;

        struct {
            Tiny_StringNode *calleeName;
            struct Tiny_Expr *argsHead;
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

        struct Tiny_Expr *blockHead;

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
            Tiny_StringNode *field;
        } dot;

        struct {
            Tiny_Symbol *structTag;

            // This is only used for if the constructor has named arguments. Otherwise this is
            // NULL.
            //
            // If specified, these will get re-ordered (alongside the args) during the
            // `ResolveTypes` phase to ensure they're in the order that the struct wants.
            Tiny_StringNode *argNamesHead;
            struct Tiny_Expr *argsHead;
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
            Tiny_StringNode *moduleName;
            Tiny_StringNode *argsHead;
            Tiny_StringNode *asName;
        } use;

        // Indexing into an array-like thing. This will get compiled as if you're calling a function
        // {type}_get_index(arr, elem).
        //
        // If it's the left hand side of an assignment expression, then it will be compiled as
        // {type}_set_index(arr, elem, rhs).
        struct {
            struct Tiny_Expr *arr;
            struct Tiny_Expr *elem;

            // Once the types are resolved, this is set to {type(arr)}_get_index. The
            // overall type of this expression will be the return type of the get index func.
            // This makes the type checking for the assignment (set_index) trivial assuming
            // set_index receives the type returned by get_index (which it should in any reasonable
            // interface).
            const Tiny_Symbol *getIndexFunc;
            const Tiny_Symbol *setIndexFunc;
        } index;
    };
} Tiny_Expr;
