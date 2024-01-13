#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stddef.h>

#include "tiny.h"

// Append to intrusive linked list
#define TINY_LL_APPEND(head, tail, node) \
    do {                                 \
        if (!(head) && !(tail)) {        \
            head = tail = (node);        \
        } else {                         \
            (tail)->next = (node);       \
            tail = (tail)->next;         \
        }                                \
    } while (0)

typedef struct Tiny_Context Tiny_Context;

void *TMalloc(Tiny_Context *ctx, size_t size);
void *TRealloc(Tiny_Context *ctx, void *ptr, size_t size);
void TFree(Tiny_Context *ctx, void *ptr);

int Tiny_TranslatePosToLineNumber(const char *src, Tiny_TokenPos pos);

void Tiny_FormatErrorV(char *buf, size_t bufsize, const char *fileName, const char *src,
                       Tiny_TokenPos pos, const char *s, va_list args);

#endif

