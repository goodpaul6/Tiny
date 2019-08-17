#include <assert.h>

#include "context.c"
#include "common.c"
#include "map.c"
#include "stringpool.c"
#include "type.c"

int main(int argc, char** argv)
{
    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &ctx);

    TypetagPool tp;
    InitTypetagPool(&tp, &ctx);

    for(int i = 0; i <= TYPETAG_ANY; ++i) {
        Typetag* t = GetPrimitiveTypetag(&tp, (TypetagType)i);
        assert(t);
    }

    enum 
    {
        PRIM,
        FUNC,
        STRUCT,
        NAME
    };

    static const struct
    {
        uint8_t type;
        union
        {
            uint8_t primType;
            struct 
            {
                uint8_t retSig;
                uint8_t sigs[];
                bool varargs;
            } func;
            struct
            {

            } sstruct;
            const char* name;
        };
    } sigs[] = {

    };

    static Typetag* prevTags[sizeof(sigs) / sizeof(sigs[0])];

    for(int i = 0; i < sizeof(sigs) / sizeof(sigs[0]); ++i) {

    }

    return 0;
}
