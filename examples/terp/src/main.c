#include <stdio.h>

#include "tiny.h"

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s (path to tiny file)\n", argv[0]);
        return 1;
    }

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindStandardIO(state);
    Tiny_BindStandardArray(state);
    Tiny_BindStandardDict(state);
    Tiny_BindStandardLib(state);
    Tiny_BindI64(state);

    Tiny_CompileFile(state, argv[1]);

    Tiny_StateThread stateThread;

    Tiny_InitThread(&stateThread, state);

    Tiny_StartThread(&stateThread);

    while(Tiny_ExecuteCycle(&stateThread));

    Tiny_DeleteState(state);

    return 0;
}