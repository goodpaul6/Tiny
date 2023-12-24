#include <stdio.h>

#include "tiny.h"

static Tiny_MacroResult ImportModuleFunction(Tiny_State* state, char* const* args, int nargs,
                                             const char* asName) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Expected exactly 1 argument to 'import'",
        };
    }

    Tiny_CompileFile(state, args[0]);

    return (Tiny_MacroResult){.type = TINY_MODULE_SUCCESS};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s (path to tiny file)\n", argv[0]);
        return 1;
    }

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindStandardIO(state);
    Tiny_BindStandardArray(state);
    Tiny_BindStandardDict(state);
    Tiny_BindStandardLib(state);
    Tiny_BindI64(state);

    Tiny_BindMacro(state, "import", ImportModuleFunction);

    Tiny_CompileFile(state, argv[1]);

    Tiny_StateThread stateThread;

    Tiny_InitThread(&stateThread, state);

    Tiny_StartThread(&stateThread);

    while (Tiny_ExecuteCycle(&stateThread))
        ;

    Tiny_DestroyThread(&stateThread);

    Tiny_DeleteState(state);

    return 0;
}