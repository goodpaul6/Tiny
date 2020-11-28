#include <assert.h>

#include "tiny.c"

int main(int argc, char** argv) {
    Tiny_Context ctx = {NULL, Tiny_DefaultAlloc};

    const char s[] = "Hello, World!";

    Tiny_State state;
    InitState(&state, &ctx);

    AllocateFunctions(&state, 1);

    uintptr_t ps = (uintptr_t)Tiny_StringPoolInsertLen(&state.parser.sp, s, sizeof(s) - 1);

    uint32_t patchPos = GenGotoPatch(&state, OP_GOTO);

    // func(a: int, b: int, c: int): bool { return a + b - c == 0 }
    SetFuncStartHere(&state, 0);

    GenGetOrSet(&state, OP_GET_LOCAL, -3);
    GenGetOrSet(&state, OP_GET_LOCAL, -2);
    GenOp(&state, OP_ADD_I);
    GenGetOrSet(&state, OP_GET_LOCAL, -1);
    GenOp(&state, OP_SUB_I);
    GenOp(&state, OP_EQU_I_0);
    GenOp(&state, OP_RETVAL);

    uint32_t pc = GetGenPC(&state);
    FillPatch(&state, patchPos, &pc, sizeof(pc));

    GenOp(&state, OP_PUSH_S);
    GenBytes(&state, alignof(ps), &ps, sizeof(ps));

    GenPushInt(&state, 10);
    GenPushInt(&state, 20);
    GenPushInt(&state, 30);

    GenCall(&state, 3, 0);

    GenOp(&state, OP_HALT);

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &ctx);

    Tiny_VM vm;
    Tiny_InitVM(&vm, &ctx, &state, &sp);

    Tiny_Run(&vm);

    assert(vm.retVal.b == true);

    Tiny_DestroyVM(&vm);
    DestroyState(&state);

    return 0;
}
