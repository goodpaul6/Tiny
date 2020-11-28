#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "map.h"

typedef struct Tiny_String
{
    uint64_t key;
    int refCount;
    size_t len;
    char str[];
} Tiny_String;

typedef struct Tiny_StringPool
{
    Tiny_Context* ctx;
    Tiny_Map map;
} Tiny_StringPool;

void Tiny_InitStringPool(Tiny_StringPool* sp, Tiny_Context* ctx);

const char* Tiny_StringPoolInsertKeyLen(Tiny_StringPool* sp, uint64_t key, const char* str, size_t len);
const char* Tiny_StringPoolInsertLen(Tiny_StringPool* sp, const char* str, size_t len);
const char* Tiny_StringPoolInsert(Tiny_StringPool* sp, const char* s);

Tiny_String* Tiny_GetString(const char* str);

inline extern uint64_t Tiny_GetStringKey(const char* str)
{
    return Tiny_GetString(str)->key;
}

void Tiny_StringPoolRetain(Tiny_StringPool* sp, const char* str);
void Tiny_StringPoolRelease(Tiny_StringPool* sp, const char* str);

void Tiny_DestroyStringPool(Tiny_StringPool* sp);

// The equivalent of the ODR-allow 'inline' that C++ has is 'inline extern'
// which means this is going to have external linkage (i.e. other TUs can
// reference this) but this inline definition is also the external definition:
// https://stackoverflow.com/a/7767858
inline extern bool Tiny_StringPoolEqual(const char* a, const char* b)
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
