#include "context.c"
#include "common.c"

int main(int argc, char** argv)
{
    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    int* ints = NULL; 
    
    INIT_BUF(ints, &ctx);

    for(int i = 0; i < 1000; ++i) {
        BUF_PUSH(ints, i);
    }

    assert(BUF_LEN(ints) == 1000);
    assert(BUF_CAP(ints) >= 1000);

    for(int i = 0; i < 1000; ++i) {
        assert(ints[i] == i);
    }

    assert(BUF_POP(ints) == 999);

    DESTROY_BUF(ints);

    INIT_BUF(ints, &ctx);
    BUF_RESERVE(ints, 1000);

    assert(BUF_CAP(ints) == 1000);

    for(int i = 0; i < 1000; ++i) {
        BUF_PUSH(ints, i);
    }
    
    assert(BUF_LEN(ints) == 1000);
    assert(BUF_CAP(ints) == 1000);

    int* start = BUF_ADD(ints, 1000);

    for(int i = 0; i < 1000; ++i) {
        start[i] = i;
    }

    assert(BUF_LEN(ints) == 2000);
    assert(BUF_CAP(ints) == 2000);
    
    for(int i = 0; i < 1000; ++i) {
        assert(ints[i + 1000] == i);
    }

    DESTROY_BUF(ints);

    return 0;
}

