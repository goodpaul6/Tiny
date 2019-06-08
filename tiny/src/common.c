// Core functions

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "common.h"
#include "context.h"

// Arena allocator

typedef struct ArenaPage
{ 
    uint8_t* data;
    size_t size;
    size_t used;

    struct ArenaPage* next;
} ArenaPage;

typedef struct Arena
{
    Tiny_Context* ctx;

    ArenaPage* head;
    ArenaPage* tail;
} Arena;

static ArenaPage* CreateArenaPage(Tiny_Context* ctx, size_t size)
{
    ArenaPage* page = TMalloc(ctx, sizeof(ArenaPage));

    size = ALIGN_UP(MAX(SNOW_ARENA_PAGE_SIZE, size), SNOW_ARENA_ALIGNMENT);

    page->data = TMalloc(ctx, size);

    assert(page->data == ALIGN_DOWN_PTR(page->data, SNOW_ARENA_ALIGNMENT));

    page->size = size;
    page->used = 0;

    page->next = NULL;

    return page;
}

static void InitArena(Arena* arena, Tiny_Context* ctx)
{
    arena->ctx = ctx;
    arena->head = arena->tail = NULL;
}

static void* ArenaAlloc(Arena* arena, size_t size)
{
    if(!arena->tail || size > (arena->tail->size - arena->tail->used)) {
        ArenaPage* page = CreateArenaPage(arena->ctx, size);

        if(!arena->tail) {
            arena->head = arena->tail = page;
        } else {
            arena->tail->next = page;
            arena->tail = page;
        }
    }

    void* ptr = &arena->tail->data[arena->tail->used];
    arena->tail->used = ALIGN_UP(arena->tail->used + size, SNOW_ARENA_ALIGNMENT);

    assert(ptr == ALIGN_DOWN_PTR(ptr, SNOW_ARENA_ALIGNMENT));

    return ptr;
}

static void DestroyArena(Arena* arena)
{
    ArenaPage* page = arena->head;
    
    while(page) {
        ArenaPage* next = page->next;

        TFree(arena->ctx, page->data);
        TFree(arena->ctx, page);

        page = next;
    }
}

// Stretchy buffer (ala stb)

typedef struct
{
    Tiny_Context* ctx;
    size_t len, cap;
    char data[];
} BufHeader;

// This is necessary so that we have access to the allocation routines.
#define INIT_BUF(b, c)          ((b) = CreateBuf((c)))

#define BUF_HEADER(b)           ((BufHeader*)((char*)(b) - offsetof(BufHeader, data)))

#define BUF_LEN(b)              (BUF_HEADER(b)->len)
#define BUF_CAP(b)              (BUF_HEADER(b)->cap)
#define BUF_END(b)              ((b) + BUF_LEN(b))

#define BUF_RESERVE(b, n)       ((n) <= BUF_CAP(b) ? 0 : ((b) = BufGrow((b), (n), sizeof(*(b)))))
#define BUF_PUSH(b, v)          (BUF_RESERVE((b), BUF_LEN(b) + 1), (b)[BUF_HEADER(b)->len++] = (v))
#define BUF_POP(b)              (b[--BUF_HEADER(b)->len])
#define BUF_CLEAR(b)            (BUF_HEADER(b)->len = 0)

#define DESTROY_BUF(b)          ((b) ? (TFree(BUF_HEADER(b)->ctx, BUF_HEADER(b)), (b) = NULL) : 0)

static void* CreateBuf(Tiny_Context* ctx)
{
    BufHeader* header = TMalloc(ctx, offsetof(BufHeader, data));

    header->ctx = ctx;
    header->len = header->cap = 0;

    return header->data;
}

static void* BufGrow(void* b, size_t newLen, size_t elemSize)
{
    assert(b);
    assert(BUF_CAP(b) <= (SIZE_MAX - 1) / 2);

    size_t newCap = MAX(2 * BUF_CAP(b), MAX(newLen, 16));

    assert(newLen <= newCap);

    // The new capacity is larger than SIZE_MAX bytes
    assert(newCap <= (SIZE_MAX - offsetof(BufHeader, data)) / elemSize);

    size_t newSize = offsetof(BufHeader, data) + newCap * elemSize;

    BufHeader* newHeader;

    newHeader = TRealloc(BUF_HEADER(b)->ctx, BUF_HEADER(b), newSize);
    newHeader->cap = newCap;

    return newHeader->data;
}

// A string pool that allows for garbage collection via refcounting.

typedef struct String
{
    uint64_t key;
    int refCount;
    char str[];
} String;

typedef struct StringPool
{
    Tiny_Context* ctx;
    Map map;
} StringPool;

static void InitStringPool(StringPool* sp, Tiny_Context* ctx)
{
    sp->ctx = ctx;
    InitMap(&sp->map, ctx);
}

static const char* StringPoolInsertLen(StringPool* sp, const char* str, size_t len)
{
    uint64_t h = HashBytes(str, len);

    // Empty strings hash to 1
    uint64_t key = h ? h : 1;

    String* prevStr = MapGet(&sp->map, key);

    if(prevStr) {
        return prevStr->str;
    }

    String* newStr = SMalloc(sp->ctx, offsetof(String, str) + len + 1);

    newStr->key = key;
    newStr->refCount = 0;

    memcpy(newStr->str, str, len);
    newStr->str[len] = '\0';

    return newStr->str;
}

static const char* StringPoolInsert(StringPool* sp, const char* str)
{
    return StringPoolInsertLen(sp, str, strlen(str));
}

static String* GetString(const char* str)
{
    return (String*)((char*)str - offsetof(String, str));
}

static uint64_t GetStringKey(const char* str)
{
    return GetString(str)->key;
}

// Call this when a string is marked.
static void StringPoolRetain(StringPool* sp, const char* str)
{
    (void)sp;

    String* g = GetString(str);
    g->refCount += 1;
}

// Call this when a string is sweeped (destroyed).
static void StringPoolRelease(StringPool* sp, const char* str)
{
    String* g = GetString(str);

    // Must have been retained in order to be released like this
    assert(g->refCount >= 0);

    g->refCount -= 1;

    if(g->refCount <= 0) {
        String* removed = MapRemove(&sp->map, g->key);
        assert(g == removed);

        TFree(sp->ctx, g);
    }
}

static void DestroyStringPool(StringPool* sp)
{
    for(size_t i = 0; i < sp->map.cap; ++i) {
        if(sp->map.keys[i]) {
            TFree(sp->ctx, sp->map.values[i]);
        }
    }

    DestroyMap(&sp->map);    
}

inline static bool StringPoolEqual(const char* a, const char* b)
{
#ifndef NDEBUG
    if(strcmp(a, b) == 0) {
        assert(a == b);
        return true;
    }

    return false;
#else
    return a == b;
#endif
}

typedef struct ConstPool
{
    float* numbers;
} ConstPool;

static void InitConstPool(ConstPool* np, Tiny_Context* ctx)
{
    INIT_BUF(np->numbers, ctx);
}

static int RegisterNumber(ConstPool* np, float f)
{
    int c = BUF_LEN(np->numbers);

    for(int i = 0; i < c; ++i) {
        if(np->numbers[i] == f) {
            return i;
        }
    }

    BUF_PUSH(np->numbers, f);
    return c;
}

static void DestroyConstPool(ConstPool* np)
{
    DESTROY_BUF(np->numbers);
}

static char* MemPrintf(Tiny_Context* ctx, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    size_t n = 1 + vsnprintf(NULL, 0, fmt, args);

    va_end(args);

    char* str = TMalloc(ctx, n);

    va_start(args, fmt);

    vsnprintf(str, n, fmt, args);

    va_end(args);

    return str;
}
