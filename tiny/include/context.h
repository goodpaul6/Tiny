#pragma once

typedef void* (*Tiny_AllocFunction)(void* data, void* ptr, size_t newSize);

typedef struct Tiny_Context
{
    void* data;
    Tiny_AllocFunction alloc;
} Tiny_Context;
