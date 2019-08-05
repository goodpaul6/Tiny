#include <assert.h>
#include <string.h>

#include "context.c"
#include "common.c"
#include "map.c"

int main(int argc, char** argv)
{
    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    Tiny_Map map;

    Tiny_InitMap(&map, &ctx);

    for(int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        Tiny_MapInsert(&map, HashBytes(buf, (size_t)n), (void*)(uintptr_t)i);
    }

    for(int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        void* p = Tiny_MapGet(&map, HashBytes(buf, (size_t)n));
        assert((uintptr_t)p == i);
    }

    for(int i = 0; i < 1000; i += 2) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        void* p = Tiny_MapRemove(&map, HashBytes(buf, (size_t)n));
        assert((uintptr_t)p == i);
    }

    for(int i = 0; i < 1000; i += 2) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        void* p = Tiny_MapGet(&map, HashBytes(buf, (size_t)n));
        assert(!p);
    }

    return 0;
}
