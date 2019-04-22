#include <setjmp.h>

typedef struct Resolver
{
    Tiny_Context* ctx;

    StringPool* sp;
    Symbols* sym;
    TypetagPool* tp;

    jmp_buf topLevelEnv;

    AST* errorAST;
    char* errorMessage;
} Resolver;

#define RESOLVER_ERROR_LONGJMP(r) do { \
    longjmp((r)->topLevelEnv, 1); \
} while(0)

#define RESOLVER_ERROR(r, fmt, ...) do { \
    (r)->errorMessage = MemPrintf(r->ctx, (fmt), __VA_ARGS__); \
    RESOLVER_ERROR_LONGJMP(r); \
} while(0)

#define RESOLVER_ERROR_AST(r, ast, fmt, ...) do { \
    (r)->errorAST = (ast); \
    RESOLVER_ERROR(r, fmt, __VA_ARGS__); \
} while(0)

static void InitResolver(Resolver* r, Tiny_Context* ctx, StringPool* sp, Symbols* sym, TypetagPool* tp)
{
    r->ctx = ctx;

    r->sp = sp;
    r->sym = sym;
    r->tp = tp;

    r->errorAST = NULL;
    r->errorMessage = NULL;
}

static void ResolveTypes(Resolver* r, AST* ast)
{
    if(ast->tag) {
        return;
    }

    switch(ast->type) {
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
            if(!ast->id.sym) {
                // The symbol declaration phase (parsing) is over; we shouldn't
                // be inside a function.
                assert(!r->sym->func);

                // Try and find a global variable that matches this name
                // This would be for when the global is declared _after_
                // being referenced.
                ast->id.sym = ReferenceVar(r->sym, ast->id.name);
            }

            if(!ast->id.sym) {
                RESOLVER_ERROR_AST(r, ast, "Referencing undeclared identifier '%s'.", ast->id.name);
            }

            assert(ast->id.sym->type == SYM_VAR ||
                   ast->id.sym->type == SYM_CONST);

            if(ast->id.sym->type != SYM_CONST) {
                assert(ast->id.sym->var.type);
                ast->tag = ast->id.sym->var.type;
            } else {
                ast->tag = ast->id.sym->constant.type;
            }
        } break;

        case AST_CALL: {
            Sym* func = ReferenceFunc(r->sym, ast->call.calleeName);

            if(!func) {
                RESOLVER_ERROR_AST(r, ast, "Calling undeclared function '%s'.", ast->call.calleeName);
            }

            for(int i = 0; i < BUF_LEN(ast->call.args); ++i) {
                ResolveTypes(r, ast->call.args[i]);
            }

            if(!func->func.type->func.varargs && 
                BUF_LEN(ast->call.args) > BUF_LEN(func->func.type->func.args)) {

            }

            if(func->type == SYM_FOREIGN_FUNC) {
            }
        } break;
    }
}

static bool ResolveProgram(Resolver* r, AST** asts)
{
    int c = setjmp(r->topLevelEnv);

    if(c) {
        return false;
    }

    return true;
}

static void DestroyResolver(Resolver* r)
{
}
