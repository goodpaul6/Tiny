#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#include "tiny.h"

#define ARENA_PAGE_SIZE 4096

typedef struct Tiny_ArenaPage {
    size_t used;
    size_t cap;
    void* data;

    struct Tiny_ArenaPage* next;
} Tiny_ArenaPage;

typedef struct Tiny_Arena {
    Tiny_Context ctx;
    Tiny_ArenaPage* head;
} Tiny_Arena;

void Tiny_InitArena(Tiny_Arena* a, Tiny_Context ctx);

void* Tiny_ArenaAlloc(Tiny_Arena* a, size_t size, size_t align);

void Tiny_DestroyArena(Tiny_Arena* a);

#endif
