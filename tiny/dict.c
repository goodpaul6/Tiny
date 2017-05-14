#include <stdlib.h>
#include <string.h>

#include "dict.h"

static void DictFree(void* pd)
{
	Dict* dict = pd;

	DestroyDict(dict);
	free(dict);
}

static void DictMark(void* pd)
{
	Dict* dict = pd;

	for (int i = 0; i < DICT_BUCKET_COUNT; ++i)
	{
		if (Tiny_IsObject(dict->buckets[i]->val))
			Tiny_Mark(dict->buckets[i]->val.obj);
	}
}

const Tiny_NativeProp DictProp = {
	"dict",
	DictMark,
	DictFree
};

// djb2 - http://www.cse.yorku.ca/~oz/hash.html
static unsigned long HashString(const char* key)
{
	unsigned long hash = 5381;
	int c;

	while (c = *key++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

static void FreeDictNode(DictNode* node)
{
	free(node->key);
	free(node);
}

void InitDict(Dict* dict)
{
	dict->nodeCount = 0;
	memset(dict->buckets, 0, sizeof(dict->buckets));
}

void DestroyDict(Dict* dict)
{
	for (int i = 0; i < DICT_BUCKET_COUNT; ++i)
	{
		DictNode* node = dict->buckets[i];

		while (node)
		{
			DictNode* next = node->next;

			FreeDictNode(node);

			node = next;
		}
	}
}

static DictNode* CreateDictNode(const char* key, const Tiny_Value* val)
{
	DictNode* node = emalloc(sizeof(DictNode));

	node->key = estrdup(key);
	node->val = *val;
	node->next = NULL;

	return node;
}

void DictPut(Dict* dict, const char* key, const Tiny_Value* val)
{
	unsigned long index = HashString(key) % DICT_BUCKET_COUNT;

	DictNode* node = CreateDictNode(key, val);

	node->next = dict->buckets[index];
	dict->buckets[index] = node;

	dict->nodeCount += 1;
}

const Tiny_Value* DictGet(Dict* dict, const char* key)
{
	unsigned long index = HashString(key) % DICT_BUCKET_COUNT;

	DictNode* node = dict->buckets[index];

	while (node)
	{
		if (strcmp(node->key, key) == 0)
			return &node->val;
	}

	return NULL;
}

void DictRemove(Dict* dict, const char* key)
{
	unsigned long index = HashString(key) % DICT_BUCKET_COUNT;

	DictNode* node = dict->buckets[index];
	DictNode* prev = NULL;

	while (node)
	{
		if (strcmp(node->key, key) == 0)
		{
			DictNode* next = node->next;

			FreeDictNode(node);

			if (prev) prev->next = next;
			else dict->buckets[index] = NULL;

			dict->nodeCount -= 1;

			return;
		}

		prev = node;
		node = node->next;
	}
}

void DictClear(Dict* dict)
{
	for (int i = 0; i < DICT_BUCKET_COUNT; ++i)
	{
		DictNode* node = dict->buckets[i];

		while (node)
		{
			DictNode* next = node->next;
			FreeDictNode(node);
			node = next;
		}
	}

	memset(dict->buckets, 0, sizeof(dict->buckets));
	dict->nodeCount = 0;
}

