#ifndef TINY_ARRAY_H
#define TINY_ARRAY_H

#include <stddef.h>

#include "tiny.h"

typedef struct {
    Tiny_Context ctx;
    Tiny_Value *data;  // stretchy buffer
} Array;

void InitArray(Array *array, Tiny_Context ctx);
void InitArrayEx(Array *array, Tiny_Context ctx, int length, const Tiny_Value *initValues);

int ArrayLen(Array *array);

void ArrayClear(Array *array);
void ArrayResize(Array *array, int length, Tiny_Value newValue);

void ArrayPush(Array *array, Tiny_Value value);
void ArrayPop(Array *array, Tiny_Value *value);

// Pop from front
void ArrayShift(Array *array, Tiny_Value *value);

void ArrayInsert(Array *array, int index, Tiny_Value value);
void ArrayRemove(Array *array, int index);

void ArraySet(Array *array, int index, Tiny_Value value);
Tiny_Value *ArrayGet(Array *array, int index);

void DestroyArray(Array *array);

#endif
