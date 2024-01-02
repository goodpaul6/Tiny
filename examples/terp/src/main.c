#include <stdio.h>
#include <string.h>

#include "tiny.h"

static Tiny_MacroResult ImportModuleFunction(Tiny_State* state, char* const* args, int nargs,
                                             const char* asName) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Expected exactly 1 argument to 'import'",
        };
    }

    // HACK(Apaar): We abuse a foreign type to keep track of whether a module was already imported
    char buf[1024] = {0};
    snprintf(buf, sizeof(buf), "%s_imported", args[0]);

    if (Tiny_FindTypeSymbol(state, buf)) {
        return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
    }

    Tiny_RegisterType(state, buf);

    Tiny_CompileFile(state, args[0]);

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s (path to tiny file)\n", argv[0]);
        return 1;
    }

    bool dis = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--dis") == 0) {
            dis = true;
        }
    }

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindStandardIO(state);
    Tiny_BindStandardArray(state);
    Tiny_BindStandardDict(state);
    Tiny_BindStandardLib(state);
    Tiny_BindI64(state);

    Tiny_BindMacro(state, "import", ImportModuleFunction);

    Tiny_CompileFile(state, argv[1]);

    if (dis) {
        char buf[1024];

        int pc = 0;
        while (pc >= 0) {
            bool res = Tiny_DisasmOne(state, &pc, buf, sizeof(buf));

            printf("%s\n", buf);

            if (!res) {
                break;
            }
        }
    }

    Tiny_StateThread stateThread;

    Tiny_InitThread(&stateThread, state);

    Tiny_StartThread(&stateThread);

    Tiny_Run(&stateThread);

    Tiny_DestroyThread(&stateThread);

    Tiny_DeleteState(state);

    return 0;
}