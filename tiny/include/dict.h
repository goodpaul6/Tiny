#ifndef DICT_H
#define DICT_H

#include "array.h"
#include "tiny.h"

typedef struct {
    int bucketCount, filledCount;
    Array keys, values;
} Dict;

void InitDict(Dict *dict, Tiny_Context ctx);

void DictSet(Dict *dict, Tiny_Value key, Tiny_Value value);

const Tiny_Value* DictGet(Dict *dict, Tiny_Value key);

void DictRemove(Dict *dict, Tiny_Value key);
void DictClear(Dict *dict);

void DestroyDict(Dict *dict);

#endif
