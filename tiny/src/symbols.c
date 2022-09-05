// Symbol table

typedef enum SymType {
    SYM_VAR,
    SYM_CONST,
    SYM_FUNC,
    SYM_FOREIGN_FUNC,
    SYM_TYPE,
    SYM_MODULE
} SymType;

typedef struct Sym {
    SymType type;
    const char* name;

    TokenPos pos;

    union {
        struct {
            int scope, index;

            struct Sym* func;
            bool reachable;

            Typetag* type;
        } var;

        struct {
            Typetag* type;

            union {
                bool bValue;
                int iValue;
                float fValue;
                const char* str;
            };
        } constant;

        struct {
            int index;

            // Buffers
            struct Sym** args;
            struct Sym** locals;

            Typetag* type;
        } func;

        struct {
            int index;

            Typetag* type;

            // TODO(Apaar): Add field to store the actual callee function pointer
        } foreignFunc;

        Typetag* typetag;

        // Used to look up the module in the State.
        // Can't store any hard reference because the module
        // may not even exist yet.
        const char* moduleName;
    };
} Sym;

typedef struct Symbols {
    Tiny_Context* ctx;
    Arena arena;

    // Buffers
    Sym** types;
    Sym** globals;
    Sym** functions;
    Sym** modules;

    // Function currently being parsed
    Sym* func;
    int scope;

    char* errorMessage;
} Symbols;

static Sym* AllocSym(Symbols* s, SymType type, const char* name, TokenPos pos);

static void InitSymbols(Symbols* s, Tiny_Context* ctx, Tiny_StringPool* sp, TypetagPool* tp) {
    s->ctx = ctx;

    InitArena(&s->arena, ctx);

    INIT_BUF(s->types, ctx);
    INIT_BUF(s->globals, ctx);
    INIT_BUF(s->functions, ctx);
    INIT_BUF(s->modules, ctx);

    const char* primitiveTypetagNames[TYPETAG_ANY + 1] = {
        Tiny_StringPoolInsert(sp, "void"),  Tiny_StringPoolInsert(sp, "bool"),
        Tiny_StringPoolInsert(sp, "char"),  Tiny_StringPoolInsert(sp, "int"),
        Tiny_StringPoolInsert(sp, "float"), Tiny_StringPoolInsert(sp, "str"),
        Tiny_StringPoolInsert(sp, "any")};

    for (int i = 0; i <= TYPETAG_ANY; ++i) {
        Sym* sym = AllocSym(s, SYM_TYPE, primitiveTypetagNames[i], 0);
        sym->typetag = GetPrimitiveTypetag(tp, i);

        BUF_PUSH(s->types, sym);
    }

    s->func = NULL;
    s->scope = 0;

    s->errorMessage = NULL;
}

#define SYMBOLS_ERROR(s, fmt, ...) \
    (((s)->errorMessage = MemPrintf((s)->ctx, fmt, __VA_ARGS__)), NULL)

// name must be a pooled string!
static Sym* AllocSym(Symbols* s, SymType type, const char* name, TokenPos pos) {
    Sym* sym = ArenaAlloc(&s->arena, sizeof(Sym));

    sym->type = type;
    sym->name = name;
    sym->pos = pos;

    return sym;
}

static void PushScope(Symbols* s) { ++s->scope; }

static void PopScope(Symbols* s) {
    if (s->func) {
        for (int i = 0; i < BUF_LEN(s->func->func.locals); ++i) {
            Sym* sym = s->func->func.locals[i];

            assert(sym->type == SYM_VAR);

            if (sym->var.scope == s->scope) {
                sym->var.reachable = false;
            }
        }
    }

    --s->scope;
}

static Sym* FindModuleSym(Symbols* sym, const char* name) {
    for (int i = 0; i < BUF_LEN(sym->modules); ++i) {
        if (Tiny_StringPoolEqual(sym->modules[i]->name, name)) {
            return sym->modules[i];
        }
    }

    return NULL;
}

static Sym* ReferenceVar(Symbols* s, const char* name) {
    if (s->func) {
        // Shadowing is not allowed, so we always just look for the first thing that
        // matches and is not out of scope.
        for (int i = 0; i < BUF_LEN(s->func->func.locals); ++i) {
            Sym* sym = s->func->func.locals[i];

            assert(sym->type == SYM_VAR);

            if (sym->var.reachable && Tiny_StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }

        for (int i = 0; i < BUF_LEN(s->func->func.args); ++i) {
            Sym* sym = s->func->func.args[i];

            assert(sym->type == SYM_VAR);
            assert(sym->var.reachable == true);

            if (Tiny_StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }
    }

    for (int i = 0; i < BUF_LEN(s->globals); ++i) {
        Sym* sym = s->globals[i];

        if (sym->type == SYM_VAR || sym->type == SYM_CONST) {
            if (Tiny_StringPoolEqual(sym->name, name)) {
                return sym;
            }
        }
    }

    return FindModuleSym(s, name);
}

static Sym* DeclareVar(Symbols* sym, const char* name, TokenPos pos, bool arg) {
    Sym* r = ReferenceVar(sym, name);

    if (r && r->type == SYM_VAR && r->var.func == sym->func) {
        return SYMBOLS_ERROR(sym,
                             "Attempted to declare a variable '%s' with the same name as another "
                             "variable in the same scope.",
                             name);
    }

    Sym* s = AllocSym(sym, SYM_VAR, name, pos);

    if (sym->func) {
        if (arg) {
            BUF_PUSH(sym->func->func.args, s);
        } else {
            BUF_PUSH(sym->func->func.locals, s);
        }
    }

    s->var.scope = sym->scope;
    s->var.index = -1;
    s->var.func = sym->func;
    s->var.reachable = true;
    s->var.type = NULL;

    return s;
}

static Sym* DeclareConst(Symbols* s, const char* name, TokenPos pos, Typetag* type) {
    Sym* sym = ReferenceVar(s, name);

    if (s->func) {
        return SYMBOLS_ERROR(s, "Attempted to declare a constant '%s' inside of a function.", name);
    }

    if (sym && (sym->type == SYM_CONST || sym->type == SYM_VAR)) {
        return SYMBOLS_ERROR(
            s, "Attempted to define a constant with the same name '%s' as another value.\n", name);
    }

    sym = AllocSym(s, SYM_CONST, name, pos);

    sym->constant.type = type;

    BUF_PUSH(s->globals, sym);

    return sym;
}

static Sym* ReferenceFunc(Symbols* s, const char* name) {
    for (int i = 0; i < BUF_LEN(s->functions); ++i) {
        Sym* sym = s->functions[i];

        if ((sym->type == SYM_FUNC || sym->type == SYM_FOREIGN_FUNC) &&
            Tiny_StringPoolEqual(sym->name, name)) {
            return sym;
        }
    }

    return NULL;
}

static Sym* DeclareFunc(Symbols* sym, const char* name, TokenPos pos) {
    Sym* s = AllocSym(sym, SYM_FUNC, name, pos);

    s->func.index = -1;

    INIT_BUF(s->func.args, sym->ctx);
    INIT_BUF(s->func.locals, sym->ctx);

    BUF_PUSH(sym->functions, s);

    return s;
}

static Sym* BindFunction(Symbols* sym, const char* name, Typetag* type) {
    Sym* prevFunc = ReferenceFunc(sym, name);

    if (prevFunc) {
        return SYMBOLS_ERROR(sym, "There is already a function bound to name '%s'.", name);
    }

    Sym* s = AllocSym(sym, SYM_FOREIGN_FUNC, name, 0);

    s->foreignFunc.index = -1;
    s->foreignFunc.type = type;

    BUF_PUSH(sym->functions, s);

    return s;
}

static Sym* FindTypeSym(Symbols* sym, const char* name) {
    for (int i = 0; i < BUF_LEN(sym->types); ++i) {
        if (Tiny_StringPoolEqual(sym->types[i]->name, name)) {
            return sym->types[i];
        }
    }

    return NULL;
}

static Sym* DefineTypeSym(Symbols* sym, const char* name, TokenPos pos, Typetag* typetag) {
    // FIXME(Apaar): Why is this an assert and not a runtime SYMBOLS_ERROR?
    assert(FindTypeSym(sym, name) == NULL);

    Sym* s = AllocSym(sym, SYM_TYPE, name, pos);
    s->typetag = typetag;

    BUF_PUSH(sym->types, s);

    return s;
}

// TODO(Apaar): Use a Map to store symbols eventually
static const char* GetTypeName(Symbols* sym, const Typetag* type) {
    for (int i = 0; i < BUF_LEN(sym->types); ++i) {
        if (type == sym->types[i]->typetag) {
            return sym->types[i]->name;
        }
    }

    return NULL;
}

static Sym* DefineModuleSym(Symbols* sym, const char* name, TokenPos pos, const char* moduleName) {
    const Sym* prevModule = FindModuleSym(sym, name);

    if (FindModuleSym(sym, name)) {
        return SYMBOLS_ERROR(sym, "You have already imported a module {} with the name {}.",
                             prevModule->moduleName, name);
    }

    Sym* s = AllocSym(sym, SYM_MODULE, name, pos);
    s->moduleName = moduleName;

    BUF_PUSH(sym->modules, s);

    return s;
}

static void ClearSymbolsError(Symbols* s) {
    if (s->errorMessage) {
        TFree(s->ctx, s->errorMessage);
    }
}

static void DestroySymbols(Symbols* s) {
    ClearSymbolsError(s);

    for (int i = 0; i < BUF_LEN(s->functions); ++i) {
        Sym* sym = s->functions[i];

        assert(sym->type == SYM_FUNC || sym->type == SYM_FOREIGN_FUNC);

        DESTROY_BUF(sym->func.args);
        DESTROY_BUF(sym->func.locals);
    }

    DESTROY_BUF(s->functions);
    DESTROY_BUF(s->globals);
    DESTROY_BUF(s->types);

    DestroyArena(&s->arena);
}
