#pragma once

#include "tiny.h"
#include "tiny_detail.h"

#define DICT_BUCKET_COUNT 1024

typedef struct sDictNode
{
	struct sDictNode* next;
	char* key;
	Tiny_Value val;
} DictNode;

typedef struct
{
	int nodeCount;
	DictNode* buckets[DICT_BUCKET_COUNT];
} Dict;

extern const Tiny_NativeProp DictProp;

void InitDict(Dict* dict);

void DictPut(Dict* dict, const char* key, const Tiny_Value* value);
const Tiny_Value* DictGet(Dict* dict, const char* key);
void DictRemove(Dict* dict, const char* key);
void DictClear(Dict* dict);

void DestroyDict(Dict* dict);
