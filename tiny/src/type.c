#include <assert.h>
#include <stdbool.h>

// Type system

typedef struct Typetag Typetag;

typedef enum TypetagType
{
    TYPETAG_VOID,
    TYPETAG_BOOL,
    TYPETAG_CHAR,
    TYPETAG_INT,
    TYPETAG_FLOAT,
    TYPETAG_STR,
    TYPETAG_ANY,
    TYPETAG_FOREIGN,
    TYPETAG_FUNC,
    TYPETAG_STRUCT,
    TYPETAG_NAME
} TypetagType;

typedef struct Typetag
{
    TypetagType type;

    union
    {
        struct
        {
            // Buffer
            struct Typetag** args;

            struct Typetag* ret;

            bool varargs;
        } func;

        struct
        {
            // Buffer
            const char** names;

            // Buffer
            struct Typetag** types;
        } tstruct;

        // Resolved later via the symbol table
        const char* name;
    };
} Typetag;

typedef struct TypetagPool
{
    Tiny_Context* ctx;
    Arena arena;

    Typetag** types;

    // Buffer
    // Stores other buffers that are to be destroyed when the
    // TypetagPool is destroyed. It stores pointers
    // to the start of the buffers' memory because Typetags
    // are immutable and so these should never be realloc'd
    // and thus moved.
    void** buffers;
} TypetagPool;

static void InitTypetagPool(TypetagPool* pool, Tiny_Context* ctx)
{
    pool->ctx = ctx;
    
    InitArena(&pool->arena, ctx);

    INIT_BUF(pool->types, ctx);
    INIT_BUF(pool->buffers, ctx);
}

static Typetag* GetPrimitiveTypetag(TypetagPool* pool, TypetagType type)
{
    assert(type <= TYPETAG_ANY);

    static Typetag tags[] = {
        { TYPETAG_VOID },
        { TYPETAG_BOOL },
        { TYPETAG_CHAR },
        { TYPETAG_INT },
        { TYPETAG_FLOAT },
        { TYPETAG_STR },
        { TYPETAG_ANY },
    };

    return &tags[type];
}

static Typetag* AllocTypetag(TypetagPool* pool, TypetagType type)
{
    Typetag* tag = ArenaAlloc(&pool->arena, sizeof(Typetag));
    tag->type = type;

    return tag;
}

static Typetag* InternFuncTypetag(TypetagPool* pool, Typetag** args, Typetag* ret, bool varargs)
{
    for(int i = 0; i < BUF_LEN(pool->types); ++i) {
        Typetag* type = pool->types[i];

        if(type->type != TYPETAG_FUNC) {
            continue;
        }

        if(type->func.varargs != varargs) {
            continue;
        }
        
        if(type->func.ret != ret) {
            continue;
        }
        
        if(BUF_LEN(type->func.args) != BUF_LEN(args)) {
            continue;
        }

        bool match = true;

        for(int j = 0; j < BUF_LEN(type->func.args); ++j) {
            if(type->func.args[j] != args[j]) {
                match = false;
                break;
            }
        }

        if(match) {
            DESTROY_BUF(args);
            return type;
        }
    }

    Typetag* type = AllocTypetag(pool, TYPETAG_FUNC);

	BUF_PUSH(pool->buffers, args);

    type->func.args = args;
    type->func.ret = ret;
    type->func.varargs = varargs;

    BUF_PUSH(pool->types, type);

    return type;
}

static Typetag* InternStructTypetag(TypetagPool* pool, const char** names, Typetag** types)
{
    for(int i = 0; i < BUF_LEN(pool->types); ++i) {
        Typetag* type = pool->types[i];

        if(type->type != TYPETAG_STRUCT) {
            continue;
        }

        if(BUF_LEN(type->tstruct.names) != BUF_LEN(names) ||
           BUF_LEN(type->tstruct.types) != BUF_LEN(types)) {
            continue;
        }

        bool match = true;

        for(int i = 0; i < BUF_LEN(names); ++i) {
            if(!Tiny_StringPoolEqual(type->tstruct.names[i], names[i])) {
                match = false;
                break;
            }
        }

        if(!match) {
            continue;
        }

        for(int i = 0; i < BUF_LEN(types); ++i) {
            if(type->tstruct.types[i] != types[i]) {
                match = false;
                break;
            }
        }

        if(match) {
            DESTROY_BUF(names);
            DESTROY_BUF(types);
            return type;
        }
    }

    Typetag* type = AllocTypetag(pool, TYPETAG_STRUCT);

	BUF_PUSH(pool->buffers, (void*)names);
    BUF_PUSH(pool->buffers, types);

    type->tstruct.names = names;
    type->tstruct.types = types;

    BUF_PUSH(pool->types, type);

    return type;
}

static Typetag* InternNameTypetag(TypetagPool* pool, const char* name)
{
    for(int i = 0; i < BUF_LEN(pool->types); ++i) {
        Typetag* type = pool->types[i];
        if(type->type == TYPETAG_NAME && type->name == name) {
            return type;
        }
    }

    Typetag* type = AllocTypetag(pool, TYPETAG_NAME);
    type->name = name;

    BUF_PUSH(pool->types, type);

    return type;
}

static bool IsPrimitiveType(const Typetag* tag)
{
    return tag->type != TYPETAG_ANY 
        && tag->type != TYPETAG_STRUCT 
        && tag->type != TYPETAG_STR;
}

// 'a' is src type, 'b' is target type
static bool CompareTypes(const Typetag* a, const Typetag* b)
{
    if(a->type == TYPETAG_VOID) {
        return b->type == TYPETAG_VOID;
    }

    // Can convert *to* 'any' implicitly
    if(b->type == TYPETAG_ANY) {
        return true;
    }

    return a == b;
}

// Returns -1 if it doesn't exist
static int GetFieldIndex(const Typetag* s, const char* name)
{
    assert(s->type == TYPETAG_STRUCT);

    for(int i = 0; i < BUF_LEN(s->tstruct.names); ++i) {
        if(Tiny_StringPoolEqual(name, s->tstruct.names[i])) {
            return i;
        }
    }

    return -1;
}

static void DestroyTypetagPool(TypetagPool* pool)
{
    for(int i = 0; i < BUF_LEN(pool->buffers); ++i) {
        DESTROY_BUF(pool->buffers[i]);
    }

    DESTROY_BUF(pool->buffers);
    DESTROY_BUF(pool->types);

    DestroyArena(&pool->arena);
}
