#include <assert.h>
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
    // TODO: Perhaps think about having a DictSetEx where you can
    // adjust growth factor and parameters (like allow it to fail
    // if there's no space). Better yet, break these apart into
    // DictSet and DictSetGrow so that it's clear.
    if(dict->filledCount >= (dict->bucketCount * 2) / 3)
        Grow(dict);

    unsigned long hash = HashString(key);

	unsigned long index = hash % dict->bucketCount;

    const char* prevKey = ArrayGetValue(&dict->keys, index, char*);
    
    unsigned long origin = index;

    // While an empty spot or the key we want to set isn't found
    while(prevKey && strcmp(prevKey, key) != 0)
    {
        // Probe for a spot to put this key/value 
        ++index;
        index %= dict->bucketCount;
        
        // It wrapped around so there's no space
        // Just grow and keep on trying
        if(index == origin)
        {
            Grow(dict);

            // Index must be recreated since bucket count changed 
            index = hash % dict->bucketCount;
            origin = index;
        }

        prevKey = ArrayGetValue(&dict->keys, index, char*);
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
    else
    {
        // Otherwise, we do nothing with the key since the user
        // is probably replacing the value
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
        
        if(!keyHere) return NULL;

        if(strcmp(keyHere, key) == 0)
            return ArrayGet(&dict->values, index);

        index += 1;
        index %= dict->bucketCount;

        if(index == origin)
        {
            // Wrapped around, so it definitely doesn't exist
            return NULL;
        }
    }

    // No key here indicates that it doesn't exist
    return NULL;
}

void DictRemove(Dict* dict, const char* key)
{
    unsigned long hash = HashString(key);

	unsigned long index = hash % dict->bucketCount;
    unsigned long origin = index;

    while(true)
    {
        char* keyHere = ArrayGetValue(&dict->keys, index, char*);
    
        if(!keyHere) return; // Done
            
        if(strcmp(keyHere, key) == 0)
        {
            free(keyHere);
            
            char* nullValue = NULL;
            ArraySet(&dict->keys, index, &nullValue);

            dict->filledCount -= 1;
        }
        else if((HashString(keyHere) % dict->bucketCount) == (index - 1))
        {
            // This key was colliding with the original or some other
			// key was colliding with the original and took keyHere's
			// spot
            // so remove it and re-add it to the table
            const void* value = ArrayGet(&dict->values, index); 

            char* nullValue = NULL;
            ArraySet(&dict->keys, index, &nullValue);
    
            dict->filledCount -= 1;

            DictSet(dict, keyHere, value);

            free(keyHere);
        }

        index += 1;
        index %= dict->bucketCount;
    }
}

void DictClear(Dict* dict)
{
    for(int i = 0; i < dict->bucketCount; ++i)
        free(ArrayGetValue(&dict->keys, i, char*));
    
    // TODO: Refactor this into an ArrayClearToZero inline function
    memset(dict->keys.data, 0, dict->keys.length * dict->keys.itemSize);
    
    dict->filledCount = 0;
}
