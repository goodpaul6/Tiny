#pragma once

#include "tiny.h"
#include "array.h"

typedef struct
{
    int bucketCount, filledCount;
    Array keys, values;
} Dict;

void InitDict(Dict* dict, size_t valueSize);

void DictSet(Dict* dict, const char* key, const void* value);

const void* DictGet(Dict* dict, const char* key);
#define DictGetValue(dict, key, type) (*(type*)DictGet((dict), (key)))

void DictRemove(Dict* dict, const char* key);
void DictClear(Dict* dict);

void DestroyDict(Dict* dict);
