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

static Tiny_Value Lib_Print(const Tiny_Value* args, int count)
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

static Tiny_Value Lib_Lequal(const Tiny_Value* args, int count)
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

    Tiny_BindFunction(state, "print", Lib_Print);
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

static int Fact(int n)
{
    if(n <= 1) return 1;
    return n * Fact(n - 1);
}

static void test_Fact(void)
{
    Tiny_State* state = Tiny_CreateState();

    Tiny_BindFunction(state, "print", Lib_Print);
    Tiny_BindFunction(state, "lequal", Lib_Lequal);

    Tiny_CompileString(state, "test_compile", "func fact(n) { if n <= 1 return 1 return n * fact(n - 1) } lequal(fact(5), 120)");

	static Tiny_StateThread threads[1000];

	for (int i = 0; i < 1000; ++i) {
		Tiny_InitThread(&threads[i], state);
		Tiny_StartThread(&threads[i]);
	}

    for(int i = 0; i < 1000; ++i)
    {
        for(int d = 0; d < 100; ++d) {
            assert(threads[i].pc >= 0);
        }

        lequal(Fact(5), 120);
    } 

    for(int i = 0; i < 1000; ++i)
        Tiny_DestroyThread(&threads[i]);

    Tiny_DeleteState(state);
}

static void test_TinyState(void)
{
    test_StateCompile();
}

int main(int argc, char* argv[])
{
    lrun("Array", test_Array);
    lrun("Dict", test_Dict);
    lrun("Tiny State", test_TinyState);
    lrun("Fact", test_Fact);
    lresults();

    return lfails != 0;
}
