// Core functions

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "context.h"

static void* TMalloc(Tiny_Context* ctx, size_t size)
{
    return ctx->alloc(ctx->data, NULL, size);
}

static void* TRealloc(Tiny_Context* ctx, void* mem, size_t newSize)
{
    return ctx->alloc(ctx->data, mem, newSize);
}

static void TFree(Tiny_Context* ctx, void* mem)
{
    ctx->alloc(ctx->data, mem, 0);
}

static uint64_t HashUint64(uint64_t x)
{
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 32;
    return x;
}

static uint64_t HashBytes(const void* ptr, size_t len)
{
    uint64_t x = 0xcbf29ce484222325;
    const char *buf = (const char *)ptr;

    for (size_t i = 0; i < len; i++) {
        x ^= buf[i];
        x *= 0x100000001b3;
        x ^= x >> 32;
    }

    return x;
}

// Arena allocator
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#define ALIGN_DOWN(n, a) ((n) & ~((a) - 1))
#define ALIGN_UP(n, a) ALIGN_DOWN((n) + (a) - 1, (a))
#define ALIGN_DOWN_PTR(p, a) ((void *)ALIGN_DOWN((uintptr_t)(p), (a)))
#define ALIGN_UP_PTR(p, a) ((void *)ALIGN_UP((uintptr_t)(p), (a)))

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
        ArenaPage* page = CreateArenaPage(arena->snow, size);

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

// Map

typedef struct Map
{
    Tiny_Context* ctx;

    size_t cap, used;

    size_t* keys;
    void** values;
} Map;

static void InitMap(Map* map, Tiny_Context* ctx)
{
    map->ctx = ctx;

    map->cap = 0;
    map->used = 0;

    map->keys = NULL;
    map->values = NULL;
}

static void MapInsert(Map* map, size_t key, void* value);

static void MapGrow(Map* map, size_t newCap)
{
    if(newCap < 16) {
        newCap = 16;
    }

    Map newMap = {
        map->ctx,

        newCap, 0,

        TMalloc(map->ctx, sizeof(uint64_t) * newCap),
        TMalloc(map->ctx, sizeof(void*) * newCap)
    };
    
    memset(newMap.keys, 0, sizeof(uint64_t) * newCap);
    memset(newMap.values, 0, sizeof(void*) * newCap);

    for(size_t i = 0; i < map->cap; ++i) {
        if(map->keys[i]) {
            MapInsert(&newMap, map->keys[i], map->values[i]);
        }
    }

    DestroyMap(map);

    *map = newMap;
}

static void MapInsert(Map* map, uint64_t key, void* value)
{
    assert(key);

    if(map->used * 2 >= map->cap) {
        MapGrow(map, map->cap * 2);
    }

    size_t i = (size_t)HashUint64(key);    

    while(true) {
        i %= map->cap;

        if(!map->keys[i]) {
            map->keys[i] = key;
            map->values[i] = value;
            map->used++;
            return;
        } else if(map->keys[i] == key) {
            map->values[i] = value;
            return;
        }

        i += 1;
    }
}

static void* MapGet(Map* map, uint64_t key)
{
    if(map->used == 0) {
        return NULL;
    }

    size_t i = HashUint64(key);    

    while(true) {
        i %= map->cap;
        if(map->keys[i] == key) {
            return map->values[i];
        } else if(!map->keys[i]) {
            return NULL;
        }

        i += 1;
    }

    return NULL;
}

// Returns the removed value
static void* MapRemove(Map* map, uint64_t key)
{
    if(map->used == 0) {
        return;
    }

    size_t i = HashUint64(key);

    while(true) {
        i %= map->cap;
        if(map->keys[i] == key) {
            map->keys[i] = 0;
            return map->values[i];
        } else if(!map->keys[i]) {
            return NULL;
        }

        i += 1;
    }

    return NULL;
}

static void DestroyMap(Map* map)
{
    TFree(map->ctx, map->keys);
    TFree(map->ctx, map->values);
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
