#include <stdlib.h>
#include <string.h>

#include "dict.h"

#define INIT_BUCKET_COUNT 32

// djb2 - http://www.cse.yorku.ca/~oz/hash.html
static unsigned long HashString(const char* key)
{
	unsigned long hash = 5381;
	int c;

	while (c = *key++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

static void Init(Dict* dict, size_t valueSize, int bucketCount)
{
    dict->bucketCount = bucketCount;
    dict->filledCount = 0;

    InitArray(&dict->keys, sizeof(char*));
    InitArray(&dict->values, valueSize);
    
    // Initialize the array to be full of null char pointers to
    // indicate that there's nothing there
    char* nullValue = NULL;

    ArrayResize(&dict->keys, bucketCount, &nullValue);
    ArrayResize(&dict->values, bucketCount, NULL);
}

static void Grow(Dict* dict)
{
    Dict newDict;

    Init(&newDict, dict->values.itemSize, dict->bucketCount * 2);

    for(int i = 0; i < dict->bucketCount; ++i)
    {
        const char* prevKey = ArrayGetValue(&dict->keys, i, char*);
    
        // If there was a value in that bucket
        if(prevKey)
        {
            const void* prevValue = ArrayGet(&dict->values, i);
            DictSet(&newDict, prevKey, prevValue);
        }
    }
    
    // Free the previous data (keys etc)
    DestroyDict(dict);
    *dict = newDict;
}

void InitDict(Dict* dict, size_t valueSize)
{
    Init(dict, valueSize, INIT_BUCKET_COUNT);
}

void DestroyDict(Dict* dict)
{
    for(int i = 0; i < dict->keys.length; ++i)
        free(ArrayGetValue(&dict->keys, i, char*));

    DestroyArray(&dict->keys);
    DestroyArray(&dict->values);
}

void DictSet(Dict* dict, const char* key, const void* value)
{
    if(dict->filledCount >= (dict->bucketCount * 2) / 3)
        Grow(dict);

	unsigned long index = HashString(key) % dict->bucketCount;

    const char* prevKey = ArrayGetValue(&dict->keys, index, char*);
    
    // There was already a value there before
    if(prevKey)
    {
        // If there is a collision
        if(strcmp(prevKey, key) != 0)
        {
            unsigned long origin = index;

            // Probe for a sport to put this key/value
            index += 1;
            
            while(true)
            {
                // It wrapped around so there's no space
                // Just grow and keep on trying
                if(index == origin)
                    Grow(dict);

                prevKey = ArrayGetValue(&dict->keys, index, char*);

                if(prevKey)
                {
                    ++index;
                    index %= dict->bucketCount;
                }
                else break; // This spot is empty
            }
        }
    }

    // There was no key here before (i.e bucket was empty)
    if(!prevKey)
    {
        char* str = malloc(strlen(key) + 1);
        assert(str);

        strcpy(str, key);

        ArraySet(&dict->keys, index, &str);
        dict->filledCount += 1;
    }

    ArraySet(&dict->values, index, value);
}

const void* DictGet(Dict* dict, const char* key)
{
	unsigned long index = HashString(key) % dict->bucketCount;
    unsigned long origin = index; 

    while(true)
    { 
        const char* keyHere = ArrayGetValue(&dict->keys, index, char*);

        if(keyHere)
        {
            if(strcmp(keyHere, key) == 0)
                return ArrayGet(&dict->values, index);
        }
        else return NULL;

        index += 1;
        index %= dict->bucketCount;

        if(index == origin)
        {
            // Wrapped around, so it definitely doesn't exist
            return NULL;
        }
    }
}

void DictRemove(Dict* dict, const char* key)
{
	unsigned long index = HashString(key) % dict->bucketCount;
    unsigned long origin = index;

    while(true)
    {
        const char* keyHere = ArrayGetValue(&dict->keys, index, char*);

        if(keyHere)
        {
            if(strcmp(keyHere, key) == 0)
            {
                free(keyHere);
                
                char* nullValue = NULL;
                ArraySet(&dict->keys, index, &nullValue);

                dict->filledCount -= 1;
            }
            else return; // Never existed to begin with

            index += 1;
            index %= dict->bucketCount;
        
            if(index == origin)
            {
                // Wrapped around, so it never existed to begin with
                return;
            }
        }
    }
}

void DictClear(Dict* dict)
{
    for(int i = 0; i < dict->bucketCount; ++i)
        free(ArrayGetValue(&dict->keys, i, char*));
    
    // TODO: Refactor this into an ArrayClearToZero inline function
    memset(array->data, 0, array->length * array->itemSize);
    
    dict->filledCount = 0;
}
