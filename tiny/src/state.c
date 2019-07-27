#include <string.h>

#include "state.h"

typedef struct Tiny_State 
{
    Tiny_Context* ctx;

    Tiny_StringPool sp;

    // Buffer
    float* numbers;

    // Buffer
    uint8_t* code;
} Tiny_State;

static int RegisterNumber(Tiny_State* state, float f)
{
    int c = BUF_LEN(state->numbers);

    for(int i = 0; i < c; ++i) {
        if(state->numbers[i] == f) {
            return i;
        }
    }

    BUF_PUSH(state->numbers, f);
    return c;
}

