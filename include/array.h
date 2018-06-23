// Generic dynamic array implementation
#pragma once

#include <stddef.h>

typedef struct
{
    size_t itemSize;
    int length, capacity;
    unsigned char* data;
} Array;

void InitArray(Array* array, size_t itemSize);
void InitArrayEx(Array* array, size_t itemSize, int length, const void* data);

void ArrayClear(Array* array);
void ArrayResize(Array* array, int length, const void* newValue);

void ArrayPush(Array* array, const void* value);
void ArrayPop(Array* array, void* value);

void ArrayInsert(Array* array, int index, const void* value);
void ArrayRemove(Array* array, int index);

void ArraySet(Array* array, int index, const void* value);
const void* ArrayGet(const Array* array, int index);
#define ArrayGetValue(array, index, type) (*(type*)(ArrayGet((array), (index))))

void DestroyArray(Array* array);

