#include "arena.h"

#include <assert.h>

#include "util.h"

static ArenaPage* AllocPage(Tiny_Context* ctx, size_t size) {
    ArenaPage* page = TMalloc(ctx, sizeof(ArenaPage));

    page->data = TMalloc(ctx, size);
    page->cap = size;
    page->used = 0;
    page->next = NULL;

    return page;
}

static size_t NextMultipleOf(size_t value, size_t factor) {
    assert(factor % 2 == 0);

    // Seems correct, I have tried 2 numbers
    // 3, 4
    // 011, 100
    // 011 => 100
    // 000 + factor = 4
    //
    // 7, 4
    // 111, 100
    // 100...
    return (value & ~(factor - 1)) + factor;
}

void InitArena(Arena* a, Tiny_Context ctx) {
    a->ctx = ctx;
    a->head = NULL;
}

void* ArenaAlloc(Arena* a, size_t size, size_t align) {
    if (size > ARENA_PAGE_SIZE) {
        ArenaPage* page = AllocPage(&a->ctx, size);

        page->used = size;

        if (a->head) {
            page->next = a->head->next;
            a->head->next = page;
        } else {
            a->head = page;
        }

        return page->data;
    }

    if (!a->head || NextMultipleOf(a->head->used, align) + size > a->head->cap) {
        ArenaPage* page = AllocPage(&a->ctx, ARENA_PAGE_SIZE);

        page->used = size;

        page->next = a->head;
        a->head = page;

        return page->data;
    }

    a->head->used = NextMultipleOf(a->head->used, align) + size;

    void* data = (char*)a->head->data + (a->head->used - size);

    return data;
}

void DestroyArena(Arena* a) {
    ArenaPage* next = NULL;

    for (ArenaPage* node = a->head; node; node = next) {
        next = node->next;

        TFree(&a->ctx, node->data);
        TFree(&a->ctx, node);
    }
}
