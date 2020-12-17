#include <assert.h>

#include "common.c"
#include "context.c"
#include "map.c"
#include "stringpool.c"
#include "type.c"

int main(int argc, char** argv) {
    Tiny_Context ctx = {NULL, Tiny_DefaultAlloc};

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &ctx);

    TypetagPool tp;
    InitTypetagPool(&tp, &ctx);

    Typetag* tag[TYPETAG_ANY + 1];

    for (int i = 0; i <= TYPETAG_ANY; ++i) {
        Typetag* t = GetPrimitiveTypetag(&tp, (TypetagType)i);
        assert(t);
        tag[i] = t;
    }

    for (int i = 0; i <= TYPETAG_ANY; ++i) {
        Typetag* t = GetPrimitiveTypetag(&tp, (TypetagType)i);
        assert(t == tag[i]);
    }

    // TODO(Apaar): Intern struct types and function types

    return 0;
}
