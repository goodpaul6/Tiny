#include <assert.h>

#include "minctest.h"

#include "array.h"
#include "dict.h"

#include "tiny.h"
#include "tiny_detail.h"

static void test_InitArrayEx(void)
{
    Array array;

    const int data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    
    InitArrayEx(&array, sizeof(int), sizeof(data) / sizeof(int), data);

    lok(memcmp(array.data, data, sizeof(data)) == 0);
    lok(array.length == (sizeof(data) / sizeof(int)));
    lok(array.capacity == array.length);

    DestroyArray(&array);
}

static void test_ArrayPush(void)
{
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    lequal(array.length, 1000); 
    
    for(int i = 0; i < 1000; ++i)
        lok(ArrayGetValue(&array, i, int) == i);

    DestroyArray(&array);
}

static void test_ArrayPop(void)
{
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);
    
    lequal(array.length, 1000);

    for(int i = 0; i < 1000; ++i)
    {
        int result;
        ArrayPop(&array, &result);

        lok(result == (1000 - i - 1));
        lok(array.length == (1000 - i - 1));
    }

    lok(array.length == 0);
    
    DestroyArray(&array);
}

static void test_ArraySet(void)
{
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    lequal(array.length, 1000);

    int value = 10;

    ArraySet(&array, 500, &value);

    lok(ArrayGetValue(&array, 500, int) == value);

    DestroyArray(&array);
}

static void test_ArrayInsert(void)
{
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    lequal(array.length, 1000);
    
    int value = 10;

    ArrayInsert(&array, 500, &value);
    
    lok(array.length == 1001);
    lok(ArrayGetValue(&array, 500, int) == value);

    DestroyArray(&array);
}

static void test_ArrayRemove(void)
{
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    lequal(array.length, 1000); 
    lok(ArrayGetValue(&array, 500, int) == 500);
    
    ArrayRemove(&array, 500);

    lequal(array.length, 999);
    lok(ArrayGetValue(&array, 500, int) == 501);

    DestroyArray(&array);
}

static void test_Array(void)
{
    test_InitArrayEx();
    test_ArrayPush();
    test_ArrayPop();
    test_ArraySet();
    test_ArrayInsert();
    test_ArrayRemove();
}

static void test_DictSet(void)
{
    Dict dict;

    InitDict(&dict, sizeof(int));

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10]; 
        sprintf(key, "%d", i);
        
        DictSet(&dict, key, &i);
    }

    lok(dict.filledCount == 1000);

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10];
        sprintf(key, "%d", i);
    
        const void* pValue = DictGet(&dict, key);
    
        lok(pValue);
        lok(*(int*)pValue == i);
    }

    DestroyDict(&dict);
}

static void test_DictRemove(void)
{
    Dict dict;

    InitDict(&dict, sizeof(int));

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10]; 
        sprintf(key, "%d", i);
        
        DictSet(&dict, key, &i);
    }

    lequal(dict.filledCount, 1000);
    
    for(int i = 0; i < 100; ++i)
    { 
        static char key[10]; 
        sprintf(key, "%d", i);

        DictRemove(&dict, key);
    }

    lequal(dict.filledCount, 900);

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10]; 
        sprintf(key, "%d", i);

        if(i >= 100)
        {
            int value = DictGetValue(&dict, key, int);
            lok(value == i);
        }
        else
            lok_print(!DictGet(&dict, key),
                "key=%s", key);
    }

    DestroyDict(&dict);
}

static void test_DictClear(void)
{
    Dict dict;

    InitDict(&dict, sizeof(int));

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10];
        sprintf(key, "%d", i);

        DictSet(&dict, key, &i);
    }

    lequal(dict.filledCount, 1000);

    DictClear(&dict);

    lequal(dict.filledCount, 0);

    for(int i = 0; i < 1000; ++i)
    {
        static char key[10];
        sprintf(key, "%d", i);

        lok(!DictGet(&dict, key));
    }

    DestroyDict(&dict);
}

static void test_Dict(void)
{
    test_DictSet();
    test_DictRemove();
    test_DictClear();
}

static Tiny_Value Lib_Print(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    lequal(count, 1);

    Tiny_Value val = args[0];
    
    switch(val.type)
    {
        case TINY_VAL_NULL: puts("null\n"); break;
        case TINY_VAL_BOOL: puts(val.boolean ? "true\n" : "false\n"); break;
        case TINY_VAL_NUM: printf("%g\n", val.number); break;
        case TINY_VAL_STRING: printf("%s\n", Tiny_ToString(val)); break;
        case TINY_VAL_NATIVE: printf("native <%s at %p>\n", Tiny_GetProp(val)->name, Tiny_ToAddr(val)); break;
    }

    return Tiny_Null;
}

static Tiny_Value Lib_Lequal(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);

    Tiny_Value val = args[0];
    Tiny_Value cmp = args[1];

    lequal((int)val.number, (int)cmp.number);
    
    return Tiny_Null;
}

static void test_StateCompile(void)
{
    Tiny_State* state = Tiny_CreateState();

    Tiny_BindFunction(state, "lequal", Lib_Lequal);

    Tiny_CompileString(state, "test_compile", "func fact(n) { if n <= 1 return 1 return n * fact(n - 1) } lequal(fact(5), 120)");

	static Tiny_StateThread threads[1000];

	for (int i = 0; i < 1000; ++i) {
		Tiny_InitThread(&threads[i], state);
		Tiny_StartThread(&threads[i]);
	}

	const int value = sizeof(threads[0]);

	while(true)
	{
		int count = 0;

		for (int i = 0; i < 1000; ++i)
        {
			if (Tiny_ExecuteCycle(&threads[i]))
				count += 1;
		}

        if(count <= 0)
            break;
	}

    for(int i = 0; i < 1000; ++i)
        Tiny_DestroyThread(&threads[i]);

    Tiny_DeleteState(state);
}

static void test_TinyState(void)
{
    test_StateCompile();
}

static void test_MultiCompileString(void)
{
	Tiny_State* state = Tiny_CreateState();

	Tiny_CompileString(state, "test_compile_1", "func add(x, y) { return x + y }");
	Tiny_CompileString(state, "test_compile_2", "func sub(x, y) { return x - y }");

	Tiny_StateThread thread;

	Tiny_InitThread(&thread, state);
	
	Tiny_Value args[] = { Tiny_NewNumber(10), Tiny_NewNumber(20) };

	Tiny_Value ret = Tiny_CallFunction(&thread, Tiny_GetFunctionIndex(state, "add"), args, 2);

	lok(ret.type == TINY_VAL_NUM);
	lequal((int)ret.number, 30);

	ret = Tiny_CallFunction(&thread, Tiny_GetFunctionIndex(state, "sub"), args, 2);

	lok(ret.type == TINY_VAL_NUM);
	lequal((int)ret.number, -10);

	Tiny_DestroyThread(&thread);

	Tiny_DeleteState(state);
}

static void test_TinyStateCallFunction(void)
{
    Tiny_State* state = Tiny_CreateState();

    Tiny_CompileString(state, "test_compile", "func fact(n) { if n <= 1 return 1 return n * fact(n - 1) }");

	Tiny_StateThread thread;

	Tiny_InitThread(&thread, state);

	int fact = Tiny_GetFunctionIndex(state, "fact");

	Tiny_Value arg = Tiny_NewNumber(5);

	Tiny_Value ret = Tiny_CallFunction(&thread, fact, &arg, 1);

	lok(ret.type == TINY_VAL_NUM);
	lok(Tiny_IsThreadDone(&thread));

	lequal((int)ret.number, 120);

	Tiny_DestroyThread(&thread);

	Tiny_DeleteState(state);
}

static Tiny_Value CallFunc(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Tiny_Value ret = Tiny_CallFunction(thread, Tiny_GetFunctionIndex(thread->state, Tiny_ToString(args[0])), &args[1], count - 1);

	lok(ret.type == TINY_VAL_NUM);
	lequal((int)ret.number, 120);
}

static void test_TinyStateCallMidRun(void)
{
    Tiny_State* state = Tiny_CreateState();

	Tiny_BindFunction(state, "call_func", CallFunc);

    Tiny_CompileString(state, "test_compile", "func fact(n) { if n <= 1 return 1 return n * fact(n - 1) } call_func(\"fact\", 5)");

	Tiny_StateThread thread;

	Tiny_InitThread(&thread, state);

	Tiny_StartThread(&thread);

	while (Tiny_ExecuteCycle(&thread));

	Tiny_DestroyThread(&thread);

	Tiny_DeleteState(state);
}

int main(int argc, char* argv[])
{
    lrun("All Array tests", test_Array);
    lrun("All Dict tests", test_Dict);
    lrun("Tiny State compilation and many threads", test_TinyState);
    lrun("Multiple Tiny_CompileString same state", test_MultiCompileString);
    lrun("Tiny_CallFunction", test_TinyStateCallFunction);
    lrun("Tiny_CallFunction While Running", test_TinyStateCallFunction);
    lresults();

    return lfails != 0;
}
