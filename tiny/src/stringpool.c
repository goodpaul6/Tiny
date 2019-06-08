#include "common.h"
#include "stringpool.h"

void Tiny_InitStringPool(Tiny_StringPool* sp, Tiny_Context* ctx)
{
    sp->ctx = ctx;
    Tiny_InitMap(&sp->map, ctx);
}

const char* Tiny_StringPoolInsertLen(Tiny_StringPool* sp, const char* str, size_t len)
{
    uint64_t h = HashBytes(str, len);

    // Empty strings hash to 1
    uint64_t key = h ? h : 1;

    String* prevStr = MapGet(&sp->map, key);

    if(prevStr) {
        return prevStr->str;
    }

    String* newStr = TMalloc(sp->ctx, offsetof(Tiny_String, str) + len + 1);

    newStr->key = key;
    newStr->refCount = 0;

    memcpy(newStr->str, str, len);
    newStr->str[len] = '\0';

    return newStr->str;
}

const char* Tiny_StringPoolInsert(Tiny_StringPool* sp, const char* str)
{
    return Tiny_StringPoolInsertLen(sp, str, strlen(str));
}

Tiny_String* Tiny_GetString(const char* str)
{
    return (Tiny_String*)((char*)str - offsetof(String, str));
}

// Call this when a string is marked.
void Tiny_StringPoolRetain(Tiny_StringPool* sp, const char* str)
{
    (void)sp;

    Tiny_String* g = Tiny_GetString(str);
    g->refCount += 1;
}

// Call this when a string is sweeped (destroyed).
void Tiny_StringPoolRelease(Tiny_StringPool* sp, const char* str)
{
    Tiny_String* g = Tiny_GetString(str);

    // Must have been retained in order to be released like this
    assert(g->refCount >= 0);

    g->refCount -= 1;

    if(g->refCount <= 0) {
        Tiny_String* removed = Tiny_MapRemove(&sp->map, g->key);
        assert(g == removed);

        TFree(sp->ctx, g);
    }
}

void Tiny_DestroyStringPool(Tiny_StringPool* sp)
{
    for(size_t i = 0; i < sp->map.cap; ++i) {
        if(sp->map.keys[i]) {
            TFree(sp->ctx, sp->map.values[i]);
        }
    }

    Tiny_DestroyMap(&sp->map);    
}
