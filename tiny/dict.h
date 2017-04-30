#pragma once

#include "tiny.h"

#define DICT_BUCKET_COUNT 1024

typedef struct sDictNode
{
	struct sDictNode* next;
	char* key;
	Value val;
} DictNode;

typedef struct
{
	int nodeCount;
	DictNode* buckets[DICT_BUCKET_COUNT];
} Dict;

extern const NativeProp DictProp;

void InitDict(Dict* dict);

void DictPut(Dict* dict, const char* key, const Value* value);
const Value* DictGet(Dict* dict, const char* key);
void DictRemove(Dict* dict, const char* key);
void DictClear(Dict* dict);

void DestroyDict(Dict* dict);
