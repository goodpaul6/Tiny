#include <assert.h>
#include <stdlib.h>

#define B_STACKTRACE_IMPL

#include "arena.h"
#include "array.h"
#include "b_stacktrace.h"
#include "detail.h"
#include "dict.h"
#include "minctest.h"
#include "pos.h"
#include "tiny.h"

#define lok_print_return(test, ...)   \
    do {                              \
        lok_print(test, __VA_ARGS__); \
        if (!(test)) {                \
            return;                   \
        }                             \
    } while (0)

#define lequal_return(a, b) \
    do {                    \
        lequal(a, b);       \
        if ((a) != (b)) {   \
            return;         \
        }                   \
    } while (0)

static Dict AllocDict;

typedef struct AllocInfo {
    size_t size;
    char *stackTrace;
} AllocInfo;

static void *Alloc(void *ptr, size_t size, void *userdata) {
    if (size == 0) {
        if (ptr) {
            free(ptr);

            const Tiny_Value key = Tiny_NewLightNative(ptr);

            const Tiny_Value *value = DictGet(&AllocDict, key);

            assert(value);

            AllocInfo *info = Tiny_ToAddr(*value);

            DictRemove(&AllocDict, key);

            free(info->stackTrace);
            free(info);
        }

        return NULL;
    }

    if (!AllocDict.bucketCount) {
        // Initialize with default context which just goes straight to realloc/free
        InitDict(&AllocDict, Tiny_DefaultContext);
    }

    void *res = realloc(ptr, size);
    AllocInfo *info = NULL;

    if (ptr == NULL) {
        info = malloc(sizeof(AllocInfo));

        info->size = size;
#ifdef TINY_TEST_ENABLE_BACKTRACE
        info->stackTrace = b_stacktrace_get_string();
#else
        info->stackTrace = NULL;
#endif
    } else {
        // Update the existing entry
        const Tiny_Value key = Tiny_NewLightNative(ptr);

        const Tiny_Value *value = DictGet(&AllocDict, key);
        assert(value);

        info = Tiny_ToAddr(*value);

        if (info->size != size) {
            info->size = size;
        }

        if (res != ptr) {
            // Since the pointer moved, remove the old entry, we'll re-add the new one below
            DictRemove(&AllocDict, key);
        }
    }

    DictSet(&AllocDict, Tiny_NewLightNative(res), Tiny_NewLightNative(info));

    return res;
}

static Tiny_Context Context = {Alloc, NULL};

static Tiny_State *CreateState(void) { return Tiny_CreateStateWithContext(Context); }

static void InitThread(Tiny_StateThread *thread, const Tiny_State *state) {
    Tiny_InitThreadWithContext(thread, state, Context);
}

static void test_PosToFriendlyPos(void) {
    Tiny_Pos pos = {8};

    const char src[] = "hello\nworld\n";

    Tiny_FriendlyPos friendlyPos = Tiny_PosToFriendlyPos(pos, src, sizeof(src));

    lequal(friendlyPos.lineIndex, 1);
    lequal(friendlyPos.charIndex, 2);
}

static void test_InitArrayEx(void) {
    Array array;

    Tiny_Value data[] = {{
                             .type = TINY_VAL_INT,
                             .i = 1,
                         },
                         {
                             .type = TINY_VAL_INT,
                             .i = 2,
                         }};

    InitArrayEx(&array, Tiny_DefaultContext, sizeof(data) / sizeof(data[0]), data);

    lok(memcmp(array.data, data, sizeof(data)) == 0);
    lok(ArrayLen(&array) == 2);

    DestroyArray(&array);
}

static void test_ArrayPush(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 64; ++i) {
        ArrayPush(&array, (Tiny_Value){
                              .type = TINY_VAL_INT,
                              .i = i,
                          });
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
        ArrayPush(&array, (Tiny_Value){
                              .type = TINY_VAL_INT,
                              .i = i,
                          });
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
        ArrayPush(&array, (Tiny_Value){
                              .type = TINY_VAL_INT,
                              .i = i,
                          });
    }

    lequal(ArrayLen(&array), 1000);

    ArraySet(&array, 500,
             (Tiny_Value){
                 .type = TINY_VAL_INT,
                 .i = 10,
             });

    lequal(ArrayGet(&array, 500)->i, 10);

    DestroyArray(&array);
}

static void test_ArrayInsert(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value){
                              .type = TINY_VAL_INT,
                              .i = i,
                          });
    }

    lequal(ArrayLen(&array), 1000);

    ArrayInsert(&array, 500,
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = 10,
                });

    lequal(ArrayLen(&array), 1001);
    lequal(ArrayGet(&array, 500)->i, 10);

    DestroyArray(&array);
}

static void test_ArrayRemove(void) {
    Array array;

    InitArray(&array, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        ArrayPush(&array, (Tiny_Value){
                              .type = TINY_VAL_INT,
                              .i = i,
                          });
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
        DictSet(&dict,
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                },
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                });
    }

    lok(dict.filledCount == 1000);

    bool allGood = true;

    for (int i = 0; i < 1000; ++i) {
        const Tiny_Value *pValue = DictGet(&dict, (Tiny_Value){
                                                      .type = TINY_VAL_INT,
                                                      .i = i,
                                                  });

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
        DictSet(&dict,
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                },
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                });

        if (i % 2 == 0) {
            DictRemove(&dict, (Tiny_Value){.type = TINY_VAL_INT, .i = i / 2});
        }
    }

    lequal_return(dict.filledCount, 500);

    Tiny_Value foundKey = {0};

    for (int i = 0; i < 500; ++i) {
        Tiny_Value key = {
            .type = TINY_VAL_INT,
            .i = i,
        };

        if (DictGet(&dict, key)) {
            foundKey = key;
            break;
        }
    }

    lok_print_return(Tiny_IsNull(foundKey), "A key that was expected to be deleted was found\n");

    int neInt = -1;

    for (int i = 500; i < 1000; ++i) {
        Tiny_Value key = {
            .type = TINY_VAL_INT,
            .i = i,
        };

        int value = DictGet(&dict, key)->i;

        if (value != i) {
            neInt = i;
            break;
        }
    }

    lequal_return(neInt, -1);

    DestroyDict(&dict);
}

static void test_DictClear(void) {
    Dict dict;

    InitDict(&dict, Tiny_DefaultContext);

    for (int i = 0; i < 1000; ++i) {
        DictSet(&dict,
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                },
                (Tiny_Value){
                    .type = TINY_VAL_INT,
                    .i = i,
                });
    }

    lequal(dict.filledCount, 1000);

    DictClear(&dict);

    lequal(dict.filledCount, 0);

    Tiny_Value foundKey = {0};

    for (int i = 0; i < 1000; ++i) {
        Tiny_Value key = {
            .type = TINY_VAL_INT,
            .i = i,
        };

        if (DictGet(&dict, key)) {
            foundKey = key;
            break;
        }
    }

    lok_print(Tiny_IsNull(foundKey), "key=%lld", (int64_t)foundKey.i);

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
            printf("%lld\n", val.i);
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

    Tiny_CompileString(state, "test_compile",
                       "func fact(n: int): int { if n <= 1 return 1 return n * "
                       "fact(n - 1) }");

    static Tiny_StateThread threads[1000];

    bool allEqual = true;

    int factIndex = Tiny_GetFunctionIndex(state, "fact");

    for (int i = 0; i < 1000; ++i) {
        InitThread(&threads[i], state);

        Tiny_Value val = Tiny_CallFunction(&threads[i], factIndex,
                                           &(Tiny_Value){.type = TINY_VAL_INT, .i = 5}, 1);

        if (Tiny_ToInt(val) != 120) {
            allEqual = false;
            break;
        }
    }

    lok(allEqual);

    for (int i = 0; i < 1000; ++i) Tiny_DestroyThread(&threads[i]);

    Tiny_DeleteState(state);
}

static void test_TinyState(void) { test_StateCompile(); }

static void test_MultiCompileString(void) {
    Tiny_State *state = CreateState();

    Tiny_CompileString(state, "test_compile_1", "func add(x: int, y: int): int { return x + y }");
    Tiny_CompileString(state, "test_compile_2", "func sub(x: int, y: int): int { return x - y }");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_Value args[] = {Tiny_NewInt(10), Tiny_NewInt(20)};

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

    Tiny_CompileString(state, "test_compile",
                       "func fact(n : int) : int { if n <= 1 return 1 return n * fact(n - 1) }");

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

static Tiny_Value CallFunc(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Tiny_Value ret = Tiny_CallFunction(
        thread, Tiny_GetFunctionIndex(thread->state, Tiny_ToString(args[0])), &args[1], count - 1);

    lok(ret.type == TINY_VAL_INT);
    lequal(ret.i, 120);

    return ret;
}

static void test_TinyStateCallMidRun(void) {
    Tiny_State *state = CreateState();

    Tiny_BindFunction(state, "call_func(str, ...): any", CallFunc);

    Tiny_CompileString(state, "test_compile",
                       "func fact(n : int) : int { if n <= 1 return 1 return n * "
                       "fact(n - 1) } call_func(\"fact\", 5)");

    Tiny_StateThread thread;

    InitThread(&thread, state);

    Tiny_StartThread(&thread);

    while (Tiny_ExecuteCycle(&thread));

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

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

    while (Tiny_ExecuteCycle(&thread));

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

    while (Tiny_ExecuteCycle(&thread));

    int dg = Tiny_GetGlobalIndex(state, "d");

    Tiny_Value dict = Tiny_GetGlobal(&thread, dg);

    Dict *d = Tiny_ToAddr(dict);

    extern const Tiny_NativeProp DictProp;

    lok(Tiny_GetProp(dict) == &DictProp);

    Tiny_Value num = *DictGet(d, (Tiny_Value){
                                     .type = TINY_VAL_CONST_STRING,
                                     .cstr = "a",
                                 });

    lequal(Tiny_ToInt(num), 10);

    num = *DictGet(d, (Tiny_Value){
                          .type = TINY_VAL_CONST_STRING,
                          .cstr = "b",
                      });

    lequal(Tiny_ToInt(num), 20);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static Tiny_Value Lib_MyInput(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    static int i = 0;

    const char *input[] = {"10", "20", "+", "30", "-", "quit"};

    return Tiny_NewConstString(input[i++]);
}

static void test_RevPolishCalc(void) {
    Tiny_State *state = CreateState();

    Tiny_BindStandardArray(state);
    Tiny_BindStandardLib(state);

    Tiny_BindFunction(state, "my_input(): str", Lib_MyInput);

    const char *code =
        "use array(\"float\") as farray\n"
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

    while (Tiny_ExecuteCycle(&thread));

    extern const Tiny_NativeProp ArrayProp;

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
    lequal(AllocDict.filledCount, 0);

    if (AllocDict.filledCount == 0) {
        return;
    }

    for (int i = 0; i < AllocDict.bucketCount; ++i) {
        Tiny_Value key = *ArrayGet(&AllocDict.keys, i);

        if (Tiny_IsNull(key)) {
            continue;
        }

        Tiny_Value value = *ArrayGet(&AllocDict.values, i);

        AllocInfo *info = Tiny_ToAddr(value);

        printf("Leaked %zu bytes. Stack trace:\n", info->size);
        printf("%s\n", info->stackTrace);
    }
}

static void test_Arena() {
    Tiny_Arena a;

    Tiny_InitArena(&a, Context);

    void *data = Tiny_ArenaAlloc(&a, 10, 1);

    lequal((int)a.head->used, 10);

    const char *s = "hello wor\0";
    strcpy(data, s);

    void *data2 = Tiny_ArenaAlloc(&a, ARENA_PAGE_SIZE + 10, 1);

    // This is checking that the large allocation we perform above
    // does not cause the "small allocation" page to be moved back.
    lequal((int)a.head->used, 10);

    void *data3 = Tiny_ArenaAlloc(&a, 7, 8);

    lequal((int)((uintptr_t)data3 % 8), 0);
    lequal((int)a.head->used, 23);

    Tiny_DestroyArena(&a);
}

static void test_StructTypeSafe() {
    Tiny_State *state = CreateState();

    const char *code =
        "struct X { x: int }\n"
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
    while (Tiny_ExecuteCycle(&thread));

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 0xffffffff);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_Break() {
    Tiny_State *state = CreateState();

    const char *code =
        "x := 0\n"
        "while x < 10 {\n"
        "    x += 1\n"
        "    break\n"
        "}";

    Tiny_CompileString(state, "test_break", code);

    int idx = Tiny_GetGlobalIndex(state, "x");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    while (Tiny_ExecuteCycle(&thread));

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 1);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_Continue() {
    Tiny_State *state = CreateState();

    const char *code =
        "x := 0\n"
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
    while (Tiny_ExecuteCycle(&thread));

    int x = Tiny_ToInt(Tiny_GetGlobal(&thread, idx));

    lequal(x, 11);

    Tiny_DestroyThread(&thread);

    Tiny_DeleteState(state);
}

static void test_ParseNullable() {
    Tiny_State *state = CreateState();

    const char *code =
        "x : ?int = 10\n"
        "func f(): ?int\n"
        "{ return 10 }\n";

    Tiny_CompileString(state, "(parse opt test)", code);

    lok(true);

    Tiny_DeleteState(state);
}

static void test_BindNullable() {
    Tiny_State *state = CreateState();

    Tiny_RegisterType(state, "Point");

    Tiny_BindFunction(state, "f(?Point, ?int): ?int", Lib_Print);

    lok(true);

    Tiny_DeleteState(state);
}

static void test_CannotUseNullable() {
    Tiny_State *state = CreateState();

    const char *code =
        "x : ?int = 10\n"
        "func f(x: int) {}\n"
        "f(x)\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(cannot use nullable test)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_ParseFailureIsOkay() {
    Tiny_State *state = CreateState();

    const char *code =
        "x : ?int = 10\n"
        "func fx: int) {}\n"
        "f(x)\n";

    Tiny_CompileResult result =
        Tiny_CompileString(state, "(parse errors do not crash program)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_CannotAssignNull() {
    Tiny_State *state = CreateState();

    const char *code = "x : int = null\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(cannot assign null to int)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_AssignNullToAny() {
    Tiny_State *state = CreateState();

    const char *code = "x : any = null\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(can assign null to any)", code);

    lequal(result.type, TINY_COMPILE_SUCCESS);

    Tiny_DeleteState(state);
}

static void test_AssignValueToNullable() {
    Tiny_State *state = CreateState();

    const char *code = "x : ?int = 10\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(can assign 10 to int?)", code);

    lequal(result.type, TINY_COMPILE_SUCCESS);

    Tiny_DeleteState(state);
}

static void test_CantAssignNullableToNonNullable() {
    Tiny_State *state = CreateState();

    const char *code =
        "x: ?int = 10\n"
        "y: int = x\n";

    Tiny_CompileResult result =
        Tiny_CompileString(state, "(can't assign nullable to non-nullable)", code);

    lequal(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_DisasmOne() {
    Tiny_State *state = CreateState();

    const char *code = "x := 10 + 20\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(disasm)", code);

    lequal(result.type, TINY_COMPILE_SUCCESS);

    char buf[256];
    int pc = 0;

    Tiny_DisasmOne(state, &pc, buf, sizeof(buf));

    lsequal(buf, "0 ((disasm):1)\tPUSH_INT 10");

    Tiny_DeleteState(state);
}

static TINY_MACRO_FUNCTION(CompileStringMacro) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "compile_string requires one arg",
        };
    }

    Tiny_CompileResult compileResult =
        Tiny_CompileString(state, "(compile_string macro code)", args[0]);

    if (compileResult.type != TINY_COMPILE_SUCCESS) {
        Tiny_MacroResult mr = {.type = TINY_MACRO_ERROR};

        snprintf(mr.error.msg, sizeof(mr.error.msg), "%s", compileResult.error.msg);
        return mr;
    }

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

static void test_NestCompileFailPropagates() {
    Tiny_State *state = CreateState();

    Tiny_BindMacro(state, "compile_string", CompileStringMacro);

    const char *code = "use compile_string(\"x: int = null\")";

    Tiny_CompileResult result = Tiny_CompileString(state, "(macro fail)", code);

    lequal_return(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_GetStringConst() {
    Tiny_State *state = CreateState();

    const char *code = "x :: \"str\"";

    Tiny_CompileResult result = Tiny_CompileString(state, "(string const)", code);

    lequal_return(result.type, TINY_COMPILE_SUCCESS);

    const Tiny_Symbol *c = Tiny_FindConstSymbol(state, "x");

    lok_print_return(c, "Constant 'x' not found");
    lequal_return(c->type, TINY_SYM_CONST);
    lequal_return(c->constant.tag->type, TINY_SYM_TAG_STR);

    const char *str = Tiny_GetStringFromConstIndex(state, c->constant.sIndex);

    lok_print_return(str, "Couldn't get string from 'x'");

    lsequal(str, "str");

    Tiny_DeleteState(state);
}

static void test_IndexSyntax() {
    Tiny_State *state = CreateState();

    Tiny_BindStandardArray(state);

    const char *code =
        "use array(\"int\") as aint\n"
        "a := aint(0)\n"
        "zero := a[0]\n"
        "a[0] = 2\n"
        "two := a[0]";

    Tiny_CompileResult result = Tiny_CompileString(state, "(index syntax)", code);

    lequal_return(result.type, TINY_COMPILE_SUCCESS);

    int zeroIdx = Tiny_GetGlobalIndex(state, "zero");
    int twoIdx = Tiny_GetGlobalIndex(state, "two");

    lequal_return(twoIdx, 2);

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    Tiny_Run(&thread);

    Tiny_Value zv = Tiny_GetGlobal(&thread, zeroIdx);
    lequal_return(zv.type, TINY_VAL_INT);
    lequal_return(zv.i, 0);

    Tiny_Value tv = Tiny_GetGlobal(&thread, twoIdx);
    lequal_return(tv.type, TINY_VAL_INT);
    lequal_return(tv.i, 2);

    Tiny_DeleteState(state);
}

static void test_NestedStructAssignValue() {
    Tiny_State *state = CreateState();

    const char *code =
        "struct A { x: int } struct B { x: int a: A } b := new B{10, new A{20}}\n"
        "b.x = 10\n"
        "b.a.x = 30\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(nested struct assignment)", code);

    lequal_return(result.type, TINY_COMPILE_SUCCESS);

    int b = Tiny_GetGlobalIndex(state, "b");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    Tiny_Run(&thread);

    Tiny_Value bv = Tiny_GetGlobal(&thread, b);
    lequal_return(bv.type, TINY_VAL_STRUCT);
    lequal_return(bv.obj->ostruct.fields[0].i, 10);
    lequal_return(bv.obj->ostruct.fields[1].type, TINY_VAL_STRUCT);
    lequal_return(bv.obj->ostruct.fields[1].obj->ostruct.fields[0].i, 30);

    Tiny_DeleteState(state);
}

static void test_LeftAndRightShift() {
    Tiny_State *state = CreateState();

    const char *code =
        "x := 1\n"
        "x = x << 2\n"
        "y := x >> 2\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(left and right shift)", code);

    lequal_return(result.type, TINY_COMPILE_SUCCESS);

    int x = Tiny_GetGlobalIndex(state, "x");
    int y = Tiny_GetGlobalIndex(state, "y");

    Tiny_StateThread thread;

    Tiny_InitThread(&thread, state);

    Tiny_StartThread(&thread);
    Tiny_Run(&thread);

    Tiny_Value xv = Tiny_GetGlobal(&thread, x);
    lequal_return(xv.type, TINY_VAL_INT);
    lequal_return(xv.i, 4);

    Tiny_Value yv = Tiny_GetGlobal(&thread, y);
    lequal_return(yv.type, TINY_VAL_INT);
    lequal_return(yv.i, 1);

    Tiny_DeleteState(state);
}

static void test_InsufficientArgsError() {
    Tiny_State *state = CreateState();

    const char *code =
        "func add(x: int, y: int): int { return x + y }\n"
        "a := add(10)\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(insufficient args)", code);

    lequal_return(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}

static void test_MatchingArgLocalName() {
    Tiny_State *state = CreateState();

    const char *code =
        "func add(x: int, y: int): int { x := 20 return x + y }\n"
        "a := add(10)\n";

    Tiny_CompileResult result = Tiny_CompileString(state, "(matching arg local names)", code);

    lequal_return(result.type, TINY_COMPILE_ERROR);

    Tiny_DeleteState(state);
}


int main(int argc, char *argv[]) {
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
    lrun("Test Arena Allocator", test_Arena);
    lrun("Tiny Struct Type Safe", test_StructTypeSafe);
    lrun("Tiny Hex Literal", test_HexLiteral);
    lrun("Tiny Break Statement", test_Break);
    lrun("Tiny Continue Statement", test_Continue);
    lrun("Tiny Parse Nullable Types", test_ParseNullable);
    lrun("Tiny Bind Nullable Types in Fn", test_BindNullable);
    lrun("Tiny Check Cannot Use Nullable", test_CannotUseNullable);
    lrun("Tiny Parse Failure is Ok", test_ParseFailureIsOkay);
    lrun("Tiny Cannot Assign Null to Non-Nullable", test_CannotAssignNull);
    lrun("Tiny Can Assign Null to Any", test_AssignNullToAny);
    lrun("Tiny Can Assign Value to Nullable", test_AssignValueToNullable);
    lrun("Tiny Can't Assign Nullable to Non-Nullable", test_CantAssignNullableToNonNullable);
    lrun("Tiny Test DisasmOne", test_DisasmOne);
    lrun("Tiny Test GetStringConstant", test_GetStringConst);
    lrun("Tiny Nested Compile Error Propagates", test_NestCompileFailPropagates);
    lrun("Tiny Index Syntax Works", test_IndexSyntax);
    lrun("Tiny Nested Struct Assignment Works", test_NestedStructAssignValue);
    lrun("Tiny Left and Right Shift Works", test_LeftAndRightShift);
    lrun("Tiny Insufficient Args", test_InsufficientArgsError);
    lrun("Tiny Matching Arg and Local Names", test_MatchingArgLocalName);

    lrun("Check no leak in tests", test_CheckMallocs);

    lresults();

    return lfails != 0;
}
