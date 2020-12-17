#include <assert.h>

#include "common.c"
#include "context.c"
#include "map.c"
#include "stringpool.c"

static void* TrackAlloc(void* map, void* ptr, size_t newSize) {
    if (newSize == 0) {
        free(ptr);
    } else {
        ptr = realloc(ptr, newSize);
    }

    // FIXME(Apaar): Computes hash of NULL ptr even though it isn't used
    uint64_t ptrHash = HashBytes(&ptr, sizeof(ptr));

    if (!ptr) {
        return NULL;
    } else if (newSize == 0) {
        void* value = Tiny_MapRemove(map, ptrHash);
        assert(value);
        return NULL;
    }

    void** psize = Tiny_MapGetPtr(map, ptrHash);

    if (!psize) {
        void* dest;
        memcpy(&dest, &newSize, sizeof(dest));

        Tiny_MapInsert(map, ptrHash, dest);
    } else {
        memcpy(psize, &newSize, sizeof(*psize));
    }

    return ptr;
}

static void InitTrackAllocContext(Tiny_Context* ctx, Tiny_Context* trackerCtx, Tiny_Map* allocMap) {
    Tiny_InitMap(allocMap, trackerCtx);

    ctx->data = allocMap;
    ctx->alloc = TrackAlloc;
}

static void DestroyTrackAllocContext(Tiny_Context* ctx) {
    Tiny_Map* map = ctx->data;

    assert(map->used == 0);
    Tiny_DestroyMap(map);
}

int main(int argc, char** argv) {
    Tiny_Context ctx = {NULL, Tiny_DefaultAlloc};

    Tiny_Map allocMap;
    Tiny_Context trackCtx;

    InitTrackAllocContext(&trackCtx, &ctx, &allocMap);

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &trackCtx);

    static const char* strings[1000];

    for (int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        const char* s = Tiny_StringPoolInsertLen(&sp, buf, n);
        assert(s);

        strings[i] = s;
    }

    for (int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        const char* s = Tiny_StringPoolInsertLen(&sp, buf, n);

        assert(s);
        assert(s == strings[i]);
    }

    for (int i = 0; i < 1000; ++i) {
        Tiny_StringPoolRetain(&sp, strings[i]);
        Tiny_StringPoolRelease(&sp, strings[i]);
    }

    Tiny_DestroyStringPool(&sp);

    DestroyTrackAllocContext(&trackCtx);

    return 0;
}
