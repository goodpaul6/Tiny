#include "array.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "stretchy_buffer.h"

void InitArray(Array *array, Tiny_Context ctx) {
    array->ctx = ctx;
    array->data = NULL;
}

void InitArrayEx(Array *array, Tiny_Context ctx, int length, const Tiny_Value *initValues) {
    InitArray(array, ctx);

    Tiny_Value *values = sb_add(&ctx, array->data, length);
    memcpy(values, initValues, sizeof(Tiny_Value) * length);
}

int ArrayLen(Array *array) { return sb_count(array->data); }

void ArrayClear(Array *array) { stb__sbn(array->data) = 0; }

void ArrayResize(Array *array, int newLen, Tiny_Value newValue) {
    assert(newLen >= 0);

    int len = array->data ? stb__sbn(array->data) : 0;

    if (newLen > len) {
        Tiny_Value *newValues = sb_add(&array->ctx, array->data, newLen - len);

        for (int i = len; i < newLen; ++i) {
            newValues[i] = newValue;
        }
    } else {
        stb__sbn(array->data) = newLen;
    }
}

void ArrayPush(Array *array, Tiny_Value value) { sb_push(&array->ctx, array->data, value); }

void ArrayPop(Array *array, Tiny_Value *value) {
    assert(sb_count(array->data) > 0);

    memcpy(value, &array->data[sb_count(array->data) - 1], sizeof(Tiny_Value));
    stb__sbn(array->data) -= 1;
}

void ArrayShift(Array *array, Tiny_Value *value) {
    int len = sb_count(array->data);

    assert(len > 0);

    memcpy(value, &array->data[0], sizeof(Tiny_Value));

    memmove(&array->data[0], &array->data[1], (len - 1) * sizeof(Tiny_Value));
    stb__sbn(array->data) -= 1;
}

void ArrayInsert(Array *array, int index, Tiny_Value value) {
    int len = sb_count(array->data);

    assert(index >= 0 && index < len);

    sb_add(&array->ctx, array->data, 1);

    memmove(&array->data[index + 1], &array->data[index], sizeof(Tiny_Value) * (len - index - 1));
    memcpy(&array->data[index], &value, sizeof(Tiny_Value));
}

void ArrayRemove(Array *array, int index) {
    int len = sb_count(array->data);

    assert(index >= 0 && index < len);

    memmove(&array->data[index], &array->data[(index + 1)], sizeof(Tiny_Value) * (len - index - 1));
    stb__sbn(array->data) -= 1;
}

void ArraySet(Array *array, int index, Tiny_Value value) {
    assert(index >= 0 && index < sb_count(array->data));

    memcpy(&array->data[index], &value, sizeof(value));
}

Tiny_Value *ArrayGet(Array *array, int index) {
    assert(index >= 0 && index < sb_count(array->data));

    return &array->data[index];
}

void DestroyArray(Array *array) { sb_free(&array->ctx, array->data); }
