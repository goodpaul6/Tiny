#pragma once

#include <stdarg.h>
#include <stddef.h>

#include "tokens.h"

typedef struct Tiny_Context Tiny_Context;

void *TMalloc(Tiny_Context *ctx, size_t size);
void *TRealloc(Tiny_Context *ctx, void *ptr, size_t size);
void TFree(Tiny_Context *ctx, void *ptr);

int Tiny_TranslatePosToLineNumber(const char *src, Tiny_TokenPos pos);

void Tiny_ReportErrorV(const char *fileName, const char *src, Tiny_TokenPos pos, const char *s,
                       va_list args);
