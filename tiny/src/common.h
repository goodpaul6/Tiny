#pragma once

#include <stdalign.h>

#include "context.h"

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#define ALIGN_DOWN(n, a) ((n) & ~((a) - 1))
#define ALIGN_UP(n, a) ALIGN_DOWN((n) + (a) - 1, (a))
#define ALIGN_DOWN_PTR(p, a) ((void *)ALIGN_DOWN((uintptr_t)(p), (a)))
#define ALIGN_UP_PTR(p, a) ((void *)ALIGN_UP((uintptr_t)(p), (a)))

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
