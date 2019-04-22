// Symbol table

typedef enum SymType
{
    SYM_VAR,
    SYM_CONST,
    SYM_FUNC,
    SYM_FOREIGN_FUNC,
    SYM_TYPE
} SymType;

typedef struct Sym
{
    SymType type;
    const char* name;

    TokenPos pos;

    union
    {
        struct
        {
            int scope, index;

            struct Sym* func;
            bool reachable;

            Typetag* type;
        } var;

        struct
        {
            Typetag* type;

            union
            {
                bool bValue;
                int iValue;
                int fIndex;
                const char* str;
            };
        } constant;

        struct
        {
            int index;

            struct Sym* outerFunc;

            // Buffers
            struct Sym** args;
            struct Sym** locals;

            Typetag* type;
        } func;

        struct
        {
            int index;

            Typetag* type;

            Tiny_ForeignFunction callee;
        } foreignFunc;

        // If this is NULL, that means the type hasn't been defined yet.
        // We will iterate through the symbol table and check to make sure
        // no such types exist before compiling.
        Typetag* typetag;
    };
} Sym;

typedef struct Symbols
{
    Tiny_Context* ctx;
    Arena arena;

    // Buffers
    Sym** types;
    Sym** globals;
    Sym** functions;

    Sym* func;
    int scope;

    char* errorMessage;
} Symbols;

static Sym* AllocSym(Symbols* s, SymType type, const char* name, TokenPos pos);

static void InitSymbols(Symbols* s, Tiny_Context* ctx, StringPool* sp, TypetagPool* tp)
{
    s->ctx = ctx;

    InitArena(&s->arena, ctx);

    INIT_BUF(s->types);
    INIT_BUF(s->globals);
    INIT_BUF(s->functions);

    const char* primitiveTypetagNames[TYPETAG_ANY + 1] = {
        StringPoolInsert(sp, "void"),
        StringPoolInsert(sp, "bool"), 
        StringPoolInsert(sp, "char"), 
        StringPoolInsert(sp, "int"), 
        StringPoolInsert(sp, "float"), 
        StringPoolInsert(sp, "str"), 
        StringPoolInsert(sp, "any")
    };

    for(int i = 0; i <= TYPETAG_ANY; ++i) {
        Sym* sym = AllocSym(s, SYM_TYPE, primitiveTypetagNames[i], 0);
        sym->typetag = GetPrimitiveTypetag(tp, i);

        BUF_PUSH(s->types, sym);
    }

    s->func = NULL;
    s->scope = 0;

    s->errorMessage = NULL;
}

#define SYMBOLS_ERROR(s, fmt, ...) (((s)->errorMessage = MemPrintf((s)->ctx, fmt, __VA_ARGS__)), NULL)

// name must be a pooled string!
static Sym* AllocSym(Symbols* s, SymType type, const char* name, TokenPos pos)
{
    Sym* sym = ArenaAlloc(&s->arena, sizeof(Sym));

    sym->type = type;
    sym->name = name;
    sym->pos = pos;

    return sym;
}

static void PushScope(Symbols* s)
{
    ++s->scope;
}

static void PopScope(Symbols* s)
{
    if(s->func) {
        for(int i = 0; i < BUF_LEN(s->func->func.locals); ++i) {
            Sym* sym = s->func->func.locals[i];

            assert(sym->type == SYM_VAR);

            if(sym->var.scope == s->scope) {
                sym->var.reachable = false;
            }
        }
    }

    --s->scope;
}

static Sym* ReferenceVar(Symbols* s, const char* name)
{
    if(s->func) {
        // Shadowing is not allowed, so we always just look for the first thing that
        // matches and is not out of scope.
        for(int i = 0; i < BUF_LEN(s->func->func.locals); ++i) {
            Sym* sym = s->func->func.locals[i];

            assert(sym->type == SYM_VAR);

            if(sym->var.reachable && StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }

        for(int i = 0; i < BUF_LEN(s->func->func.args); ++i) {
            Sym* sym = s->func->func.args[i];

            assert(sym->type == SYM_VAR);
            assert(sym->var.reachable == true);

            if(StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }
    }

    for(int i = 0; i < BUF_LEN(s->globals); ++i) {
        Sym* sym = s->globals[i];

        if(sym->type == SYM_VAR || sym->type == SYM_CONST) {
            if(StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }
    }

    return NULL;
}

static Sym* DeclareVar(Symbols* sym, const char* name, TokenPos pos, bool arg)
{
    Sym* r = ReferenceVar(sym, name);

    if(r && r->type == SYM_VAR && r->var.func == sym->func) {
        return SYMBOLS_ERROR(s, "Attempted to declare a variable '%s' with the same name as another variable in the same scope.", name);
    }

    Sym* s = AllocSym(sym, SYM_VAR, name, pos);

    if(sym->func) {
        if(arg) {
            BUF_PUSH(sym->func->func.args, s);
        } else {
            BUF_PUSH(sym->func->func.locals, s);
        }
    }

    s->var.scope = sym->scope;
    s->var.index = -1;
    s->var.func = func;
    s->var.reachable = true;
    s->var.type = NULL;

    return s;
}

static Sym* DeclareConst(Symbols* s, const char* name, TokenPos pos, Typetag* type)
{
    Sym* sym = ReferenceVar(s, name);

    if(s->func) {
        return SYMBOLS_ERROR(s, "Attempted to declare a constant '%s' inside of a function.", name);
    }

    if(sym && (sym->type == SYM_CONST || sym->type == SYM_VAR)) {
        return SYMBOLS_ERROR(s, "Attempted to define a constant with the same name '%s' as another value.\n", name);
    }

    sym = AllocSym(s, SYM_CONST, name, pos);

    sym->constant.type = type;

    BUF_PUSH(s->globals, sym);

    return sym;
}

static Sym* ReferenceFunc(Symbols* s, const char* name)
{
    for(int i = 0; i < BUF_LEN(s->functions); ++i) {
        Sym* sym = s->functions[i];

        if((sym->type == SYM_FUNC || sym->type == SYM_FOREIGN_FUNC) &&
           StringPoolEqual(sym->name, name)) {
            return sym;
        }
    }

    return NULL;
}

static Sym* DeclareFunc(Symbols* sym, const char* name, TokenPos pos)
{
    Sym* s = AllocSym(sym, SYM_FUNC, name, pos);

    s->func.index = -1;
    
    BUF_INIT(s->func.args);
    BUF_INIT(s->func.locals);

    BUF_PUSH(sym->functions, s);

    return s;
}

static Sym* BindFunction(Symbols* sym, const char* name, Typetag* type, Tiny_ForeignFunction callee)
{
    Sym* prevFunc = ReferenceFunc(sym, name);

    if(prevFunc) {
        return SYMBOLS_ERROR(sym, "There is already a function bound to name '%s'.", name);
    }

    Sym* s = AllocSym(SYM_FOREIGN_FUNC, name, 0);

    s->foreignFunc.index = -1;
    s->foreignFunc.type = type;
    s->foreignFunc.callee = callee;

    BUF_PUSH(sym->functions, s);

    return s;
}

static Sym* RegisterType(Symbols* sym, const char* name, TokenPos pos)
{
    for(int i = 0; i < BUF_LEN(sym->types); ++i) {
        if(StringPoolEqual(sym->types[i].name, name)) {
            return sym->types[i];
        }
    }

    Sym* s = AllocSym(sym, SYM_TYPE, name, pos);
    s->typetag = NULL;

    BUF_PUSH(sym->types, s);

    return s;
}

static void ClearSymbolsError(Symbols* s)
{
    if(s->errorMessage) {
        TFree(s->ctx, s->errorMessage);
    }
}

static void DestroySymbols(Symbols* s)
{
    ClearSymbolsError(s);

    DESTROY_BUF(s->functions);
    DESTROY_BUF(s->globals);
    DESTROY_BUF(s->types);

    DestroyArena(&s->arena);
}
