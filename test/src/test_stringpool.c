#include <assert.h>

#include "context.c"
#include "common.c"
#include "map.c"
#include "stringpool.c"

int main(int argc, char** argv)
{
    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &ctx);

    static const char* strings[1000];

    for(int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        const char* s = Tiny_StringPoolInsertLen(&sp, buf, n);
        assert(s);

        strings[i] = s;
    }

    for(int i = 0; i < 1000; ++i) {
        char buf[32];
        int n = sprintf(buf, "%d", i);

        const char* s = Tiny_StringPoolInsertLen(&sp, buf, n);

        assert(s);
        assert(s == strings[i]);
    }

    Tiny_DestroyStringPool(&sp);
   
    return 0;
}

