#pragma once

#include <stddef.h>

#include "tiny.h"

#define ARENA_PAGE_SIZE 4096

typedef struct ArenaPage {
    size_t used;
    size_t cap;
    void* data;

    struct ArenaPage* next;
} ArenaPage;

typedef struct Arena {
    Tiny_Context ctx;
    ArenaPage* head;
} Arena;

void InitArena(Arena* a, Tiny_Context ctx);

void* ArenaAlloc(Arena* a, size_t size, size_t align);

void DestroyArena(Arena* a);
