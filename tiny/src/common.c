// Core functions

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include "context.h"

#ifdef _MSC_VER
#define alignof(type) (__alignof(type))
#endif

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#define ALIGN_DOWN(n, a) ((n) & ~((a) - 1))
#define ALIGN_UP(n, a) ALIGN_DOWN((n) + (a) - 1, (a))
#define ALIGN_DOWN_PTR(p, a) ((void *)ALIGN_DOWN((uintptr_t)(p), (a)))
#define ALIGN_UP_PTR(p, a) ((void *)ALIGN_UP((uintptr_t)(p), (a)))

#define ARENA_PAGE_SIZE 4096
#define ARENA_ALIGNMENT	8

inline static void* TMalloc(Tiny_Context* ctx, size_t size)
{
    return ctx->alloc(ctx->data, NULL, size);
}

inline static void* TRealloc(Tiny_Context* ctx, void* mem, size_t newSize)
{
    return ctx->alloc(ctx->data, mem, newSize);
}

inline static void TFree(Tiny_Context* ctx, void* mem)
{
    ctx->alloc(ctx->data, mem, 0);
}

inline static uint64_t HashUint64(uint64_t x)
{
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 32;
    return x;
}

inline static uint64_t HashBytes(const void* ptr, size_t len)
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

	size = ALIGN_UP(MAX(ARENA_PAGE_SIZE, size), ARENA_ALIGNMENT);

    page->data = TMalloc(ctx, size);

    assert(page->data == ALIGN_DOWN_PTR(page->data, ARENA_ALIGNMENT));

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
    arena->tail->used = ALIGN_UP(arena->tail->used + size, ARENA_ALIGNMENT);

    assert(ptr == ALIGN_DOWN_PTR(ptr, ARENA_ALIGNMENT));

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
#define BUF_ADD(b, n)           (BUF_RESERVE((b), BUF_LEN(b) + (n)), (BUF_LEN(b) += (n)), (&(b)[BUF_LEN(b) - (n)]))
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
