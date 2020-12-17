#include <setjmp.h>

typedef struct Resolver {
    Tiny_Context* ctx;

    Tiny_StringPool* sp;
    Symbols* sym;
    TypetagPool* tp;

    jmp_buf topLevelEnv;

    AST* errorAST;
    char* errorMessage;
} Resolver;

#define RESOLVER_ERROR_LONGJMP(r)     \
    do {                              \
        longjmp((r)->topLevelEnv, 1); \
    } while (0)

#define RESOLVER_ERROR(r, fmt, ...)                                  \
    do {                                                             \
        (r)->errorMessage = MemPrintf(r->ctx, (fmt), ##__VA_ARGS__); \
        RESOLVER_ERROR_LONGJMP(r);                                   \
    } while (0)

#define RESOLVER_ERROR_AST(r, ast, fmt, ...)   \
    do {                                       \
        (r)->errorAST = (ast);                 \
        RESOLVER_ERROR(r, fmt, ##__VA_ARGS__); \
    } while (0)

static void InitResolver(Resolver* r, Tiny_Context* ctx, Tiny_StringPool* sp, Symbols* sym,
                         TypetagPool* tp) {
    r->ctx = ctx;

    r->sp = sp;
    r->sym = sym;
    r->tp = tp;

    r->errorAST = NULL;
    r->errorMessage = NULL;
}

static void ResolveTypes(Resolver* r, AST* ast) {
    if (ast->tag) {
        return;
    }

    switch (ast->type) {
        case AST_NULL: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_ANY);
        } break;

        case AST_BOOL: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_BOOL);
        } break;

        case AST_CHAR: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_CHAR);
        } break;

        case AST_INT: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_INT);
        } break;

        case AST_FLOAT: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_FLOAT);
        } break;

        case AST_STRING: {
            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_STR);
        } break;

        case AST_ID: {
            if (!ast->id.sym) {
                // The symbol declaration phase (parsing) is over; we shouldn't
                // be inside a function.
                assert(!r->sym->func);

                // Try and find a global variable that matches this name
                // This would be for when the global is declared _after_
                // being referenced.
                ast->id.sym = ReferenceVar(r->sym, ast->id.name);
            }

            if (!ast->id.sym) {
                RESOLVER_ERROR_AST(r, ast, "Referencing undeclared identifier '%s'.", ast->id.name);
            }

            assert(ast->id.sym->type == SYM_VAR || ast->id.sym->type == SYM_CONST);

            if (ast->id.sym->type != SYM_CONST) {
                assert(ast->id.sym->var.type);
                ast->tag = ast->id.sym->var.type;
            } else {
                ast->tag = ast->id.sym->constant.type;
            }
        } break;

        case AST_CALL: {
            Sym* func = ReferenceFunc(r->sym, ast->call.calleeName);

            if (!func) {
                RESOLVER_ERROR_AST(r, ast, "Calling undeclared function '%s'.",
                                   ast->call.calleeName);
            }

            assert(func->type == SYM_FUNC || func->type == SYM_FOREIGN_FUNC);

            for (int i = 0; i < BUF_LEN(ast->call.args); ++i) {
                ResolveTypes(r, ast->call.args[i]);
            }

            Typetag* funcType = NULL;

            if (func->type == SYM_FUNC) {
                funcType = func->func.type;
            } else {
                funcType = func->foreignFunc.type;
            }

            if (!funcType->func.varargs && BUF_LEN(ast->call.args) > BUF_LEN(funcType->func.args)) {
                RESOLVER_ERROR_AST(r, ast, "Too many arguments to function '%s'; expected %d.",
                                   ast->call.calleeName, BUF_LEN(funcType->func.args));
            }

            for (int i = 0; i < BUF_LEN(funcType->func.args); ++i) {
                if (!CompareTypes(ast->call.args[i]->tag, funcType->func.args[i])) {
                    RESOLVER_ERROR_AST(
                        r, ast->call.args[i],
                        "Argument %i is supposed to be a %s but you supplied a %s.", i + 1,
                        GetTypeName(r->sym, funcType->func.args[i]),
                        GetTypeName(r->sym, ast->call.args[i]->tag));
                }
            }

            ast->tag = funcType->func.ret;
        } break;

        case AST_PAREN: {
            ResolveTypes(r, ast->paren);

            ast->tag = ast->paren->tag;
        } break;

        case AST_BINARY: {
            switch (ast->binary.op) {
                case TOK_PLUS:
                case TOK_MINUS:
                case TOK_STAR:
                case TOK_SLASH: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    bool iLhs = ast->binary.lhs->tag->type == TYPETAG_INT;
                    bool iRhs = ast->binary.rhs->tag->type == TYPETAG_INT;

                    bool fLhs = !iLhs && ast->binary.lhs->tag->type == TYPETAG_FLOAT;
                    bool fRhs = !iRhs && ast->binary.rhs->tag->type == TYPETAG_FLOAT;

                    if ((iLhs && fRhs) || (fLhs && iRhs) || (!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        RESOLVER_ERROR_AST(r, ast,
                                           "Left and right hand side of binary operator must be "
                                           "ints or floats, but they're %s and %s.",
                                           GetTypeName(r->sym, ast->binary.lhs->tag),
                                           GetTypeName(r->sym, ast->binary.rhs->tag));
                    }

                    ast->tag =
                        GetPrimitiveTypetag(r->tp, (iLhs && iRhs) ? TYPETAG_INT : TYPETAG_FLOAT);
                } break;

                case TOK_AND:
                case TOK_OR:
                case TOK_PERCENT: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    bool iLhs = ast->binary.lhs->tag->type == TYPETAG_INT;
                    bool iRhs = ast->binary.rhs->tag->type == TYPETAG_INT;

                    if (!(iLhs && iRhs)) {
                        RESOLVER_ERROR_AST(
                            r, ast,
                            "Both sides of binary operation must be ints, but they're %s and %s.",
                            GetTypeName(r->sym, ast->binary.lhs->tag),
                            GetTypeName(r->sym, ast->binary.rhs->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_INT);
                } break;

                case TOK_LOG_AND:
                case TOK_LOG_OR: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    bool bLhs = ast->binary.lhs->tag->type == TYPETAG_BOOL;
                    bool bRhs = ast->binary.lhs->tag->type == TYPETAG_BOOL;

                    if (!bLhs || !bRhs) {
                        RESOLVER_ERROR_AST(r, ast,
                                           "Both sides of the binary operator must be bools, but "
                                           "they're %s and %s.",
                                           GetTypeName(r->sym, ast->binary.lhs->tag),
                                           GetTypeName(r->sym, ast->binary.rhs->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_BOOL);
                } break;

                case TOK_GT:
                case TOK_LT:
                case TOK_GTE:
                case TOK_LTE: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    // TODO(Apaar): Refactor this; it's identical to the TOK_PLUS/MINUS
                    // stuff above
                    bool iLhs = ast->binary.lhs->tag->type == TYPETAG_INT;
                    bool iRhs = ast->binary.rhs->tag->type == TYPETAG_INT;

                    bool fLhs = !iLhs && ast->binary.lhs->tag->type == TYPETAG_FLOAT;
                    bool fRhs = !iRhs && ast->binary.rhs->tag->type == TYPETAG_FLOAT;

                    if ((iLhs && fRhs) || (fLhs && iRhs) || (!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        RESOLVER_ERROR_AST(r, ast,
                                           "Left and right hand side of binary operator must be "
                                           "ints or floats, but they're %s and %s.",
                                           GetTypeName(r->sym, ast->binary.lhs->tag),
                                           GetTypeName(r->sym, ast->binary.rhs->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_BOOL);
                } break;

                case TOK_EQUALS:
                case TOK_NOTEQUALS: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    if (ast->binary.lhs->tag->type == TYPETAG_VOID ||
                        ast->binary.rhs->tag->type == TYPETAG_VOID) {
                        RESOLVER_ERROR_AST(
                            r, ast,
                            "Attempted to check for equality with void. This is not allowed.");
                    }

                    if (ast->binary.lhs->tag->type != ast->binary.rhs->tag->type) {
                        RESOLVER_ERROR_AST(r, ast,
                                           "Attempted to check for equality between mismatched "
                                           "types %s and %s. This is not allowed.",
                                           GetTypeName(r->sym, ast->binary.lhs->tag),
                                           GetTypeName(r->sym, ast->binary.rhs->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_BOOL);
                } break;

                case TOK_DECLARE: {
                    assert(ast->binary.lhs->type == AST_ID);
                    assert(ast->binary.lhs->id.sym);

                    ResolveTypes(r, ast->binary.rhs);

                    if (ast->binary.rhs->tag->type == TYPETAG_VOID) {
                        RESOLVER_ERROR_AST(r, ast,
                                           "Attempted to initialize variable with void expression. "
                                           "Don't do that.");
                    }

                    ast->binary.lhs->id.sym->var.type = ast->binary.rhs->tag;

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
                } break;

                case TOK_EQUAL: {
                    ResolveTypes(r, ast->binary.lhs);
                    ResolveTypes(r, ast->binary.rhs);

                    if (!CompareTypes(ast->binary.lhs->tag, ast->binary.rhs->tag)) {
                        RESOLVER_ERROR_AST(r, ast, "Attempted to assign a %s to a %s.",
                                           GetTypeName(r->sym, ast->binary.rhs->tag),
                                           GetTypeName(r->sym, ast->binary.lhs->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
                } break;

                default: {
                    ResolveTypes(r, ast->binary.rhs);
                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
                } break;
            }
        } break;

        case AST_UNARY: {
            ResolveTypes(r, ast->unary.exp);

            switch (ast->unary.op) {
                case TOK_MINUS: {
                    bool i = ast->unary.exp->tag->type == TYPETAG_INT;
                    bool f = !i && ast->unary.exp->tag->type == TYPETAG_FLOAT;

                    if (!(i || f)) {
                        RESOLVER_ERROR_AST(r, ast, "Attempted to apply unary '-' to a %s.",
                                           GetTypeName(r->sym, ast->unary.exp->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, i ? TYPETAG_INT : TYPETAG_FLOAT);
                } break;

                case TOK_BANG: {
                    if (ast->unary.exp->tag->type != TYPETAG_BOOL) {
                        RESOLVER_ERROR_AST(r, ast, "Attempted to apply unary '!' to a %s.",
                                           GetTypeName(r->sym, ast->unary.exp->tag));
                    }

                    ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_BOOL);
                } break;

                default: {
                    // Should never be parsed as such
                    assert(false);
                } break;
            }
        } break;

        case AST_BLOCK: {
            for (int i = 0; i < BUF_LEN(ast->block); ++i) {
                ResolveTypes(r, ast->block[i]);
            }

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_PROC: {
            ResolveTypes(r, ast->proc.body);

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_IF: {
            ResolveTypes(r, ast->ifx.cond);

            if (ast->ifx.cond->tag->type != TYPETAG_BOOL) {
                RESOLVER_ERROR_AST(r, ast->ifx.cond,
                                   "If condition is supposed to be a bool but it's a %s.",
                                   GetTypeName(r->sym, ast->ifx.cond->tag));
            }

            ResolveTypes(r, ast->ifx.body);

            if (ast->ifx.alt) {
                ResolveTypes(r, ast->ifx.alt);
            }

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_RETURN: {
            if (ast->retExpr) {
                ResolveTypes(r, ast->retExpr);
            }

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_WHILE: {
            ResolveTypes(r, ast->whilex.cond);

            if (ast->whilex.cond->tag->type != TYPETAG_BOOL) {
                RESOLVER_ERROR_AST(r, ast,
                                   "While condition is supposed to be a bool but it's a %s.",
                                   GetTypeName(r->sym, ast->whilex.cond->tag));
            }

            ResolveTypes(r, ast->whilex.body);

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_FOR: {
            ResolveTypes(r, ast->forx.init);
            ResolveTypes(r, ast->forx.cond);

            if (ast->forx.cond->tag->type != TYPETAG_BOOL) {
                RESOLVER_ERROR_AST(r, ast, "For condition is supposed to be a bool but it's a %s.",
                                   GetTypeName(r->sym, ast->forx.cond->tag));
            }

            ResolveTypes(r, ast->forx.step);
            ResolveTypes(r, ast->forx.body);

            ast->tag = GetPrimitiveTypetag(r->tp, TYPETAG_VOID);
        } break;

        case AST_DOT: {
            ResolveTypes(r, ast->dot.lhs);

            if (ast->dot.lhs->tag->type != TYPETAG_STRUCT) {
                RESOLVER_ERROR_AST(r, ast, "Cannot use '.' on a %s.",
                                   GetTypeName(r->sym, ast->dot.lhs->tag));
            }

            int i = GetFieldIndex(ast->dot.lhs->tag, ast->dot.field);

            if (i < 0) {
                RESOLVER_ERROR_AST(r, ast, "Struct %s does not have a field named %s.",
                                   GetTypeName(r->sym, ast->dot.lhs->tag), ast->dot.field);
            }

            ast->tag = ast->dot.lhs->tag->tstruct.types[i];
        } break;

        case AST_CONSTRUCTOR: {
            assert(ast->constructor.type);
            assert(BUF_LEN(ast->constructor.args) <= UCHAR_MAX);

            int meCount = BUF_LEN(ast->constructor.args);
            int tagCount = BUF_LEN(ast->constructor.type->tstruct.types);

            if (meCount != tagCount) {
                RESOLVER_ERROR_AST(r, ast,
                                   "Struct %s constructor expects %d args but you supplied %d.",
                                   ast->constructor.type->name, tagCount, meCount);
            }

            for (int i = 0; i < BUF_LEN(ast->constructor.args); ++i) {
                ResolveTypes(r, ast->constructor.args[i]);

                if (!CompareTypes(ast->constructor.args[i]->tag,
                                  ast->constructor.type->tstruct.types[i])) {
                    RESOLVER_ERROR_AST(
                        r, ast,
                        "Argument %d to constructor is supposed to be a %s but you supplied a %s.",
                        i + 1, GetTypeName(r->sym, ast->constructor.type->tstruct.types[i]),
                        GetTypeName(r->sym, ast->constructor.args[i]->tag));
                }
            }

            ast->tag = ast->constructor.type;
        } break;

        case AST_CAST: {
            assert(ast->cast.value);
            assert(ast->cast.tag);

            ResolveTypes(r, ast->cast.value);

            // TODO(Apaar): Allow casting of int to float etc

            // Only allow casting 'any' values for now
            if (ast->cast.value->tag->type != TYPETAG_ANY) {
                RESOLVER_ERROR_AST(r, ast->cast.value,
                                   "Attempted to cast a %s; only any is allowed.",
                                   GetTypeName(r->sym, ast->cast.value->tag));
            }

            ast->tag = ast->cast.tag;
        } break;
    }
}

static bool ResolveProgram(Resolver* r, AST** asts) {
    int c = setjmp(r->topLevelEnv);

    if (c) {
        return false;
    }

    for (int i = 0; i < BUF_LEN(asts); ++i) {
        ResolveTypes(r, asts[i]);
    }

    return true;
}

static void ClearResolverError(Resolver* r) {
    if (r->errorMessage) {
        TFree(r->ctx, r->errorMessage);
    }
}

static void DestroyResolver(Resolver* r) { ClearResolverError(r); }
