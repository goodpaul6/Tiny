#include <assert.h>

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
    TYPETAG_STRUCT
} TypetagType;

typedef struct Typetag
{
    TypetagType type;

    union
    {
        struct
        {
            // Buffer
            Typetag** args;

            Typetag* ret;

            bool varargs;
        } func;

        struct
        {
            // Buffer
            const char** names;

            // Buffer
            Typetag** types;
        } tstruct;
    };
} Typetag;

typedef struct TypetagPool
{
    Tiny_Context* ctx;
    Arena arena;

    Typetag* types;

    // Buffer
    // Stores other buffers that are to be destroyed when the
    // TypetagPool is destroyed. It stores pointers
    // to the start of the buffers' memory because Typetags
    // are immutable and so these should never be realloc'd
    // and thus moved.
    void* buffers;
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

    return tags[type];
}

static Typetag* AllocTypetag(TypetagPool* pool, TypetagType type)
{
    Typetag* type = ArenaAlloc(&pool->arena, sizeof(Typetag));
    type->type = type;

    return type;
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
            if(!StringPoolEqual(type->tstruct.names[i], names[i])) {
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

    BUF_PUSH(pool->buffers, names);
    BUF_PUSH(pool->types, types);

    type->tstruct.names = names;
    type->tstruct.types = types;

    return type;
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
