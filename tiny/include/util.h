#pragma once

#include <stdarg.h>

#include "tokens.h"

char* Tiny_Strdup(const char* s);

int Tiny_TranslatePosToLineNumber(const char* src, Tiny_TokenPos pos);

void Tiny_ReportErrorV(const char* fileName, const char* src, Tiny_TokenPos pos, const char* s, va_list args);
