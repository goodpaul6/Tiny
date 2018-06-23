#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "array.h"

static void Expand(Array* array, int newLength)
{
    while(newLength >= array->capacity)
    {
        array->capacity *= 2;

        if(array->capacity == 0)
            array->capacity = 2;

        void* newData = realloc(array->data, array->capacity * array->itemSize);
        assert(newData);

        array->data = newData;
    }
}

void InitArray(Array* array, size_t itemSize)
{
    array->itemSize = itemSize;

    array->length = 0;
    array->capacity = 0;

    array->data = NULL;
}

void InitArrayEx(Array* array, size_t itemSize, int length, const void* data)
{
    array->itemSize = itemSize;

    array->length = length;
    array->capacity = length;
    
    array->data = malloc(itemSize * length);
    assert(array->data);

    memcpy(array->data, data, itemSize * length); 
}

void ArrayClear(Array* array)
{
    array->length = 0;
}

void ArrayResize(Array* array, int length, const void* newValue)
{
    if(array->capacity < length)
    {
        void* newData = realloc(array->data, array->itemSize * length);
        assert(newData);

        array->data = newData;
        array->capacity = length;
    }

    if(newValue)
    {
        for(int i = array->length; i < length; ++i)
            memcpy(&array->data[i * array->itemSize], newValue, array->itemSize); 
    }

    array->length = length;
}

void ArrayPush(Array* array, const void* value)
{
    Expand(array, array->length + 1);

    memcpy(&array->data[array->length * array->itemSize], value, array->itemSize);
    array->length += 1;
}

void ArrayPop(Array* array, void* value)
{
    assert(array->length > 0);

    memcpy(value, &array->data[(array->length - 1) * array->itemSize], array->itemSize);
    array->length -= 1;
}

void ArrayInsert(Array* array, int index, const void* value)
{
    assert(index >= 0 && index < array->length);

    Expand(array, array->length + 1);

    memmove(&array->data[(index + 1) * array->itemSize], &array->data[index * array->itemSize], array->itemSize * (array->length - index - 1));
    memcpy(&array->data[index * array->itemSize], value, array->itemSize);
    
    array->length += 1;
}

void ArrayRemove(Array* array, int index)
{
    assert(index >= 0 && index < array->length);

    memmove(&array->data[index * array->itemSize], &array->data[(index + 1) * array->itemSize], array->itemSize * (array->length - index - 1));
    array->length -= 1;
}

void ArraySet(Array* array, int index, const void* value)
{
    assert(index >= 0 && index < array->length);

    memcpy(&array->data[index * array->itemSize], value, array->itemSize);
}

const void* ArrayGet(const Array* array, int index)
{
    assert(index >= 0 && index < array->length);

    return &array->data[index * array->itemSize];
}

void DestroyArray(Array* array)
{
    free(array->data);
}
