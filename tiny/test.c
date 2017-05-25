#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "array.h"
#include "dict.h"

static const bool TestVerbose = false;
static int TestCount = 0;
static int SuccessCount = 0;

#define DEFINE_TEST(name) static void test_##name(void) \
{ \
    TestCount += 1; \
    const char* TestName = #name; \
    if(TestVerbose) \
        printf("== Testing %s ==\n", TestName); 

#define END_TEST \
    SuccessCount += 1; \
    printf("== %s Passed ==\n", TestName); \
}

#define FAIL_IF_FALSE(c) do \
{ \
    if(!(c)) \
    { \
        printf("== %s Failed ==", TestName); \
        if(TestVerbose) \
            printf("(%s:%i) '" #c "' was false.\n", __FILE__, __LINE__); \
        return; \
    } \
} while(0) 

DEFINE_TEST(InitArrayEx)
    Array array;

    const int data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    
    InitArrayEx(&array, sizeof(int), sizeof(data) / sizeof(int), data);

    FAIL_IF_FALSE(memcmp(array.data, data, sizeof(data)) == 0);
    FAIL_IF_FALSE(array.length == (sizeof(data) / sizeof(int)));
    FAIL_IF_FALSE(array.capacity == array.length);

    DestroyArray(&array);
END_TEST

DEFINE_TEST(ArrayPush)
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    FAIL_IF_FALSE(array.length == 1000); 
    
    for(int i = 0; i < 1000; ++i)
        FAIL_IF_FALSE(ArrayGetValue(&array, i, int) == i);

    DestroyArray(&array);
END_TEST

DEFINE_TEST(ArrayPop)
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);
    
    FAIL_IF_FALSE(array.length == 1000);

    for(int i = 0; i < 1000; ++i)
    {
        int result;
        ArrayPop(&array, &result);

        FAIL_IF_FALSE(result == (1000 - i - 1));
        FAIL_IF_FALSE(array.length == (1000 - i - 1));
    }

    FAIL_IF_FALSE(array.length == 0);
    
    DestroyArray(&array);
END_TEST

DEFINE_TEST(ArraySet)
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    FAIL_IF_FALSE(array.length == 1000);

    int value = 10;

    ArraySet(&array, 500, &value);

    FAIL_IF_FALSE(ArrayGetValue(&array, 500, int) == value);

    DestroyArray(&array);
END_TEST

DEFINE_TEST(ArrayInsert)
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    FAIL_IF_FALSE(array.length == 1000);
    
    int value = 10;

    ArrayInsert(&array, 500, &value);
    
    FAIL_IF_FALSE(array.length == 1001);
    FAIL_IF_FALSE(ArrayGetValue(&array, 500, int) == value);

    DestroyArray(&array);
END_TEST

DEFINE_TEST(ArrayRemove)
    Array array;

    InitArray(&array, sizeof(int));

    for(int i = 0; i < 1000; ++i)
        ArrayPush(&array, &i);

    FAIL_IF_FALSE(array.length == 1000); 
    FAIL_IF_FALSE(ArrayGetValue(&array, 500, int) == 499);
    
    ArrayRemove(&array, 500);

    FAIL_IF_FALSE(array.length == 999);
    FAIL_IF_FALSE(ArrayGetValue(&array, 500, int) == 500);

    DestroyArray(&array);
END_TEST

int main(int argc, char* argv[])
{
    test_InitArrayEx();
    test_ArrayPush();
    test_ArrayPop();
    test_ArraySet();
    test_ArrayInsert();
    test_ArrayRemove();

    printf("%d of %d tests passed.\n", SuccessCount, TestCount);

    return 0;
}
