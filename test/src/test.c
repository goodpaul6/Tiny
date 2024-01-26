#include <assert.h>
#include <stdlib.h>

#include "arena.h"
#include "array.h"
#include "detail.h"
#include "dict.h"
#include "minctest.h"
#include "pos.h"
#include "tiny.h"

#ifndef TINY_NO_COMPILER

static int MallocCalls = 0;
static int FreeCalls = 0;

static void* Alloc(void *ptr, size_t size, void *userdata) {
    if (size == 0) {
        FreeCalls += 1;
        free(ptr);
        return NULL;
    }

    MallocCalls += 1;
    return realloc(ptr, size);
}

static Tiny_Context Context = { Alloc, NULL };

static Tiny_State* CreateState(void) {
    return Tiny_CreateStateWithContext(Context);
}

static void InitThread(Tiny_StateThread *thread, const Tiny_State *state) {
    Tiny_InitThreadWithContext(thread, state, Context);
}

static void test_PosToFriendlyPos(void) {
    Tiny_Pos pos = { 8 };

    const char src[] = "hello\nworld\n";

    Tiny_FriendlyPos friendlyPos = Tiny_PosToFriendlyPos(pos, src, sizeof(src));

    lequal(friendlyPos.lineIndex, 1);
    lequal(friendlyPos.charIndex, 2);
}

static void test_InitArrayEx(void) {
    Array array;

    Tiny_Value data[] = {
            { .type = TINY_VAL_INT, .i = 1, },
            { .type = TINY_VAL_INT, .i = 2, }
    };

    InitArrayEx(&array, Tiny_DefaultContext, sizeof(data) / sizeof(data[0]), data);

    lok(memcmp(array.data, data, sizeof(data)) == 0);
    lok(ArrayLen(&array) == 2);

    DestroyArray(&array);
}

static void test_ArrayPush(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 64; ++i) {
        ArrayPush(&array, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(ArrayLen(&array), 64);
    lequal(ArrayGet(&array, 2)->i, 2);
    lequal(ArrayGet(&array, 21)->i, 21);

    DestroyArray(&array);
}

static void test_ArrayPop(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(ArrayLen(&array), 1000);

    Tiny_Value result;
    ArrayPop(&array, &result);

    lequal(result.i, 999);
    lequal(ArrayLen(&array), 999);

    for (int i = 0; i < 998; ++i) {
        ArrayPop(&array, &result);
    }

    ArrayPop(&array, &result);

    lequal(result.i, 0);
    lequal(ArrayLen(&array), 0);

    DestroyArray(&array);
}

static void test_ArraySet(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(ArrayLen(&array), 1000);

    ArraySet(&array, 500, (Tiny_Value ) { .type = TINY_VAL_INT, .i = 10, });

    lequal(ArrayGet(&array, 500)->i, 10);

    DestroyArray(&array);
}

static void test_ArrayInsert(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(ArrayLen(&array), 1000);

    ArrayInsert(&array, 500, (Tiny_Value ) { .type = TINY_VAL_INT, .i = 10, });

    lequal(ArrayLen(&array), 1001);
    lequal(ArrayGet(&array, 500)->i, 10);

    DestroyArray(&array);
}

static void test_ArrayRemove(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(ArrayLen(&array), 1000);
    lequal(ArrayGet(&array, 500)->i, 500);

    ArrayRemove(&array, 500);

    lequal(ArrayLen(&array), 999);
    lequal(ArrayGet(&array, 500)->i, 501);

    DestroyArray(&array);
}

static void test_Array(void) {
    test_InitArrayEx();
    test_ArrayPush();
    test_ArrayPop();
    test_ArraySet();
    test_ArrayInsert();
    test_ArrayRemove();
}

static void test_DictSet(void) {
    Dict dict;

    InitDict(&dict, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        DictSet(&dict, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, }, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lok(dict.filledCount == 1000);

    bool allGood = true;

    for (int i = 0; i < 1000; ++i) {
        const Tiny_Value *pValue = DictGet(&dict, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });

        if (!pValue || pValue->i != i) {
            allGood = false;
            break;
        }
    }

    lok(allGood);

    DestroyDict(&dict);
}

static void test_DictRemove(void) {
    Dict dict;

    InitDict(&dict, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        DictSet(&dict, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, }, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(dict.filledCount, 1000);

    for (int i = 0; i < 100; ++i) {
        DictRemove(&dict, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(dict.filledCount, 900);

    Tiny_Value foundKey = { 0 };

    for (int i = 0; i < 100; ++i) {
        Tiny_Value key = { .type = TINY_VAL_INT, .i = i, };

        if (DictGet(&dict, key)) {
            foundKey = key;
            break;
        }
    }

    lok_print(Tiny_IsNull(foundKey), "key=%d", foundKey.i);

    int neInt = -1;

    for (int i = 100; i < 1000; ++i) {
        Tiny_Value key = { .type = TINY_VAL_INT, .i = i, };

        int value = DictGet(&dict, key)->i;

        if (value != i) {
            neInt = i;
            break;
        }
    }

    lok_print(neInt == -1, "i=%d", neInt);

    DestroyDict(&dict);
}

static void test_DictClear(void) {
    Dict dict;

    InitDict(&dict, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        DictSet(&dict, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, }, (Tiny_Value ) { .type = TINY_VAL_INT, .i = i, });
    }

    lequal(dict.filledCount, 1000);

    DictClear(&dict);

    lequal(dict.filledCount, 0);

    Tiny_Value foundKey = { 0 };

    for (int i = 0; i < 1000; ++i) {
        Tiny_Value key = { .type = TINY_VAL_INT, .i = i, };

        if (DictGet(&dict, key)) {
            foundKey = key;
            break;
        }
    }

    lok_print(Tiny_IsNull(foundKey), "key=%d", foundKey.i);

    DestroyDict(&dict);
}

static void test_Dict(void) {
    test_DictSet();
    test_DictRemove();
    test_DictClear();
}

static Tiny_Value Lib_Print(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    lequal(count, 1);

    Tiny_Value val = args[0];

    switch (val.type) {
        case TINY_VAL_NULL:
            puts("null\n");
            break;
        case TINY_VAL_BOOL:
            puts(val.boolean ? "true\n" : "false\n");
            break;
        case TINY_VAL_INT:
            printf("%d\n", val.i);
            break;
        case TINY_VAL_FLOAT:
            printf("%f\n", val.f);
            break;
        case TINY_VAL_STRING:
            printf("%s\n", Tiny_ToString(val));
            break;
        case TINY_VAL_NATIVE:
            printf("native <%s at %p>\n", Tiny_GetProp(val)->name, Tiny_ToAddr(val));
            break;
    }

    return Tiny_Null;
}

static void test_StateCompile(void) {
    Tiny_State *state = CreateState();

    Tiny_CompileString(state, "test_compile", "func fact(n: int): int { if n <= 1 return 1 return n * "
            "fact(n - 1) }");

    static Tiny_StateThread threads[1000];

    bool allEqual = true;

    int factIndex = Tiny_GetFunctionIndex(state, "fact");

    for (int i = 0; i < 1000; ++i) {
        InitThread(&threads[i], state);

        Tiny_Value val = Tiny_CallFunction(&threads[i], factIndex, &(Tiny_Value ) { .type = TINY_VAL_INT, .i = 5 }, 1);

        if (Tiny_ToInt(val) != 120) {
            allEqual = false;
            break;
        }
    }

    lok(allEqual);

    for (int i = 0; i < 1000; ++i)
        Tiny_DestroyThread(&threads[i]);

    Tiny_DeleteState(state);
}

static void test_TinyState(void) {
    test_StateCompile();
}

static void test_MultiCompileString(void) {
    Tiny_State *state = CreateState();

    Tiny_CompileString(state, "test_compile_1", "func add(x: int, y: int): int { return x + y }");
    Tiny_CompileString(state, "test_compile_2", "func sub(x: int, y: int): int { return x - y }");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_Value args[] = { Tiny_NewInt(10), Tiny_NewInt(20) };

    Tiny_Value ret = Tiny_CallFunction(&thread, Tiny_GetFunctionIndex(state, "add"), args, 2);

    lok(ret.type == TINY_VAL_INT);
    lequal(ret.i, 30);

    ret = Tiny_CallFunction(&thread, Tiny_GetFunctionIndex(state, "sub"), args, 2);

    lok(ret.type == TINY_VAL_INT);
    lequal(ret.i, -10);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_TinyStateCallFunction(void) {
    Tiny_State *state = CreateState();

    Tiny_CompileString(state, "test_compile", "func fact(n : int) : int { if n <= 1 return 1 return n * fact(n - 1) }");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    int fact = Tiny_GetFunctionIndex(state, "fact");

    Tiny_Value arg = Tiny_NewInt(5);

    Tiny_Value ret = Tiny_CallFunction(&thread, fact, &arg, 1);

    lok(ret.type == TINY_VAL_INT);
    lok(Tiny_IsThreadDone(&thread));

    lequal(ret.i, 120);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

/*
 static Tiny_Value CallFunc(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
 Tiny_Value ret = Tiny_CallFunction(
 thread, Tiny_GetFunctionIndex(thread->state, Tiny_ToString(args[0])), &args[1], count - 1);

 lok(ret.type == TINY_VAL_INT);
 lequal(ret.i, 120);

 return ret;
 }
 */

/*
 static void test_TinyStateCallMidRun(void) {
 Tiny_State *state = CreateState();

 Tiny_BindFunction(state, "call_func(str, ...): any", CallFunc);

 Tiny_CompileString(state, "test_compile",
 "func fact(n : int) : int { if n <= 1 return 1 return n * "
 "fact(n - 1) } call_func(\"fact\", 5)");

 Tiny_StateThread thread;

 InitThread(&thread, state);

 Tiny_StartThread(&thread);

 while (Tiny_ExecuteCycle(&thread))
 ;

 Tiny_DestroyThread(&thread);

 Tiny_DeleteState(state);
 }
 */

static Tiny_Value Lib_GetStaticNative(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    static int a = 0;
    return Tiny_NewLightNative(&a);
}

static void test_TinyEquality(void) {
    Tiny_State *state = CreateState();

    Tiny_BindStandardLib(state);

    Tiny_BindFunction(state, "get_static", Lib_GetStaticNative);

    Tiny_CompileString(state, "test_equ", "a := 10 == 10");
    Tiny_CompileString(state, "test_equ", "b := \"hello\" == \"hello\"");
    Tiny_CompileString(state, "test_equ", "c := get_static() == get_static()");
    Tiny_CompileString(state, "test_equ", "d := strcat(\"aba\", \"aba\") == \"abaaba\"");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_StartThread(&thread);

    while (Tiny_ExecuteCycle(&thread))
        ;

    int ag = Tiny_GetGlobalIndex(state, "a");
    int bg = Tiny_GetGlobalIndex(state, "b");
    int cg = Tiny_GetGlobalIndex(state, "c");
    int dg = Tiny_GetGlobalIndex(state, "d");

    Tiny_Value a = Tiny_GetGlobal(&thread, ag);
    Tiny_Value b = Tiny_GetGlobal(&thread, bg);
    Tiny_Value c = Tiny_GetGlobal(&thread, cg);
    Tiny_Value d = Tiny_GetGlobal(&thread, dg);

    lok(Tiny_ToBool(a));
    lok(Tiny_ToBool(b));
    lok(Tiny_ToBool(c));
    lok(Tiny_ToBool(d));

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_TinyDict(void) {
    Tiny_State *state = CreateState();

    Tiny_BindStandardDict(state);

    Tiny_CompileString(state, "test_dict", "d := dict(\"a\", 10, \"b\", 20)");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_StartThread(&thread);

    while (Tiny_ExecuteCycle(&thread))
        ;

    int dg = Tiny_GetGlobalIndex(state, "d");

    Tiny_Value dict = Tiny_GetGlobal(&thread, dg);

    Dict *d = Tiny_ToAddr(dict);

    extern const Tiny_NativeProp DictProp;

    lok(Tiny_GetProp(dict) == &DictProp);

    Tiny_Value num = *DictGet(d, (Tiny_Value ) { .type = TINY_VAL_CONST_STRING, .cstr = "a", });

    lequal(Tiny_ToInt(num), 10);

    num = *DictGet(d, (Tiny_Value ) { .type = TINY_VAL_CONST_STRING, .cstr = "b", });

    lequal(Tiny_ToInt(num), 20);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static Tiny_Value Lib_MyInput(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    static int i = 0;

    const char *input[] = { "10", "20", "+", "30", "-", "quit" };

    return Tiny_NewConstString(input[i++]);
}

static void test_RevPolishCalc(void) {
    Tiny_State *state = CreateState();

    Tiny_BindStandardArray(state);
    Tiny_BindStandardLib(state);

    Tiny_BindFunction(state, "my_input(): str", Lib_MyInput);

    const char *code = "use array(\"float\") as farray\n"
            "stack := farray()\n"
            "op := \"\"\n"
            "while op != \"quit\" {\n"
            "op = my_input()\n"
            "if stridx(op, 0) == '+' farray_push(stack, farray_pop(stack) "
            "+ farray_pop(stack))\n"
            "else if stridx(op, 0) == '-' farray_push(stack, farray_pop(stack) "
            "- farray_pop(stack))\n"
            "else if stridx(op, 0) == '*' farray_push(stack, farray_pop(stack) "
            "* farray_pop(stack))\n"
            "else if stridx(op, 0) == '/' farray_push(stack, farray_pop(stack) "
            "/ farray_pop(stack))\n"
            "else if op != \"quit\" farray_push(stack, ston(op)) }\n";

    Tiny_CompileString(state, "test_rpn", code);

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_StartThread(&thread);

    while (Tiny_ExecuteCycle(&thread))
        ;

    //extern const Tiny_NativeProp ArrayProp;

    int stack = Tiny_GetGlobalIndex(state, "stack");

    Array *a = Tiny_ToAddr(Tiny_GetGlobal(&thread, stack));

    lequal(ArrayLen(a), 1);

    Tiny_Value num = *ArrayGet(a, 0);

    // Float because ston produces float
    lequal(num.type, TINY_VAL_FLOAT);
    lfequal(num.f, 0);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_CheckMallocs() {
    lok(MallocCalls > 0);
    lok(FreeCalls > 0);
}

static void test_Arena() {
    Tiny_Arena a;

    Tiny_InitArena(&a, Context);

    void *data = Tiny_ArenaAlloc(&a, 10, 1);

    lequal((int )a.head->used, 10);

    char s[10] = "hello wor\0";
    strcpy(data, s);

    //void *data2 = Tiny_ArenaAlloc(&a, ARENA_PAGE_SIZE + 10, 1);

    // This is checking that the large allocation we perform above
    // does not cause the "small allocation" page to be moved back.
    lequal((int )a.head->used, 10);

    void *data3 = Tiny_ArenaAlloc(&a, 7, 8);

    lequal((int )((uintptr_t )data3 % 8), 0);
    lequal((int )a.head->used, 23);

    Tiny_DestroyArena(&a);
}

static void test_StructTypeSafe() {
    Tiny_State *state = CreateState();

    const char *code = "struct X { x: int }\n"
            "struct Y { x: int }\n"
            "func do(x: X) {}\n"
            "do(new X{10})\n";

    Tiny_CompileString(state, "test_struct_type_safe", code);
    lok(true);

    Tiny_DeleteState(state);
}

static void test_HexLiteral() {
    Tiny_State *state = CreateState();

    const char *code = "x := 0xffffffff";

    Tiny_CompileString(state, "test_hexliteral", code);

    int idx = Tiny_GetGlobalIndex(state, "x");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    while (Tiny_ExecuteCycle(&thread))
        ;

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 0xffffffff);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_Break() {
    Tiny_State *state = CreateState();

    const char *code = "x := 0\n"
            "while x < 10 {\n"
            "    x += 1\n"
            "    break\n"
            "}";

    Tiny_CompileString(state, "test_break", code);

    int idx = Tiny_GetGlobalIndex(state, "x");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    while (Tiny_ExecuteCycle(&thread))
        ;

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 1);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_Continue() {
    Tiny_State *state = CreateState();

    const char *code = "x := 0\n"
            "while x < 10 {\n"
            "    x += 1\n"
            "    if x < 10 {\n"
            "        continue\n"
            "    }\n"
            "    x += 1\n"
            "}";

    Tiny_CompileString(state, "test_continue", code);

    int idx = Tiny_GetGlobalIndex(state, "x");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    while (Tiny_ExecuteCycle(&thread))
        ;

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 11);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_ParseNullable() {
    Tiny_State *state = CreateState();

    const char *code = "x : int? = 10\n"
            "func f(): int?\n"
            "{ return 10 }\n";

    Tiny_CompileString(state, "(parse opt test)", code);

    lok(true);

    Tiny_DeleteState(state);
}

static void test_BindNullable() {
    Tiny_State *state = CreateState();

    Tiny_RegisterType(state, "Point");

    Tiny_BindFunction(state, "f(Point?, int?): int?", Lib_Print);

    lok(true);

    Tiny_DeleteState(state);
}

static void test_CannotUseNullable() {
    Tiny_State *state = CreateState();

    const char *code = "x : int? = 10\n"
            "func f(x: int) {}\n"
            "f(x)\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(cannot use nullable test)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_ParseFailureIsOkay() {
    Tiny_State *state = CreateState();

    const char *code = "x : int? = 10\n"
            "func fx: int) {}\n"
            "f(x)\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(parse errors do not crash program)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

#endif

int main(int argc, char *argv[]) {
#ifndef TINY_NO_COMPILER
    lrun("Pos to friendly pos", test_PosToFriendlyPos);
    lrun("All Array tests", test_Array);
    lrun("All Dict tests", test_Dict);
    lrun("Tiny State compilation and many threads", test_TinyState);
    lrun("Multiple Tiny_CompileString same state", test_MultiCompileString);
    lrun("Tiny_CallFunction", test_TinyStateCallFunction);
    lrun("Tiny_CallFunction While Running", test_TinyStateCallFunction);
    lrun("Tiny Equality", test_TinyEquality);
    lrun("Tiny Stdlib Dict", test_TinyDict);
    lrun("Tiny RPN", test_RevPolishCalc);
    lrun("Tests Allocations Occur", test_CheckMallocs);
    lrun("Test Arena Allocator", test_Arena);
    lrun("Tiny Struct Type Safe", test_StructTypeSafe);
    lrun("Tiny Hex Literal", test_HexLiteral);
    lrun("Tiny Break Statement", test_Break);
    lrun("Tiny Continue Statement", test_Continue);
    lrun("Tiny Parse Nullable Types", test_ParseNullable);
    lrun("Tiny Bind Nullable Types in Fn", test_BindNullable);
    lrun("Tiny Check Cannot Use Nullable", test_CannotUseNullable);
    lrun("Tiny Parse Failure is Ok", test_ParseFailureIsOkay);

    lresults();
#else
    lfails = 0;
#endif

    return lfails != 0;
}
