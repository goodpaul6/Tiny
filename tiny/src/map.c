#include <stdbool.h>
#include <stddef.h>

#include "map.h"

void Tiny_InitMap(Tiny_Map* map, Tiny_Context* ctx)
{
    map->ctx = ctx;

    map->cap = 0;
    map->used = 0;

    map->keys = NULL;
    map->values = NULL;
}

static void MapGrow(Tiny_Map* map, size_t newCap)
{
    if(newCap < 16) {
        newCap = 16;
    }

    Tiny_Map newMap = {
        map->ctx,

        newCap, 0,

        TMalloc(map->ctx, sizeof(uint64_t) * newCap),
        TMalloc(map->ctx, sizeof(void*) * newCap)
    };
    
    memset(newMap.keys, 0, sizeof(uint64_t) * newCap);
    memset(newMap.values, 0, sizeof(void*) * newCap);

    for(size_t i = 0; i < map->cap; ++i) {
        if(map->keys[i]) {
            Tiny_MapInsert(&newMap, map->keys[i], map->values[i]);
        }
    }

    Tiny_DestroyMap(map);

    *map = newMap;
}

void Tiny_MapInsert(Tiny_Map* map, uint64_t key, void* value)
{
    assert(key);

    if(map->used * 2 >= map->cap) {
        MapGrow(map, map->cap * 2);
    }

    size_t i = (size_t)HashUint64(key);    

    while(true) {
        i %= map->cap;

        if(!map->keys[i] || map->keys[i] == TINY_MAP_TOMBSTONE_KEY) {
            map->keys[i] = key;
            map->values[i] = value;
            map->used++;
            return;
        } else if(map->keys[i] == key) {
            map->values[i] = value;
            return;
        }

        i += 1;
    }
}

static int MapGetIndex(Tiny_Map* map, uint64_t key)
{
    if(map->used == 0) {
		return -1;
    }

    size_t i = HashUint64(key);    

    while(true) {
        i %= map->cap;
        if(map->keys[i] == key) {
            return (int)i;
        } else if(!map->keys[i]) {
            return -1;
        }

        i += 1;
    }

    return -1;
}

void* Tiny_MapGet(Tiny_Map* map, uint64_t key)
{
	int i = MapGetIndex(map, key);

	if(i < 0) {
		return NULL;
	}

	return map->values[i];
}

// Returns the removed value
void* Tiny_MapRemove(Tiny_Map* map, uint64_t key)
{
	int i = MapGetIndex(map, key);

	if(i < 0) {
		return NULL;
	}

	map->keys[i] = TINY_MAP_TOMBSTONE_KEY;
	--map->used;

	return map->values[i];
}

void Tiny_DestroyMap(Tiny_Map* map)
{
    TFree(map->ctx, map->keys);
    TFree(map->ctx, map->values);
}
