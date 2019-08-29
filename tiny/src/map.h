#pragma once

#include <stdint.h>

#define TINY_MAP_EMPTY_KEY		((uint64_t)0)
#define TINY_MAP_TOMBSTONE_KEY  ((uint64_t)-1)

typedef struct Tiny_Context Tiny_Context;

typedef struct Tiny_Map
{
    Tiny_Context* ctx;

    size_t cap, used;

    uint64_t* keys;
    void** values;
} Tiny_Map;

void Tiny_InitMap(Tiny_Map* map, Tiny_Context* ctx);

void Tiny_MapInsert(Tiny_Map* map, uint64_t key, void* value);
void** Tiny_MapGetPtr(Tiny_Map* map, uint64_t key);
void* Tiny_MapGet(Tiny_Map* map, uint64_t key);
void* Tiny_MapRemove(Tiny_Map* map, uint64_t key);

void Tiny_DestroyMap(Tiny_Map* map);
