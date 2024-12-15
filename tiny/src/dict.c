#include "dict.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define INIT_BUCKET_COUNT 32

// djb2 - http://www.cse.yorku.ca/~oz/hash.html
static unsigned long HashBytes(const char *first, const char *last) {
    unsigned long hash = 5381;
    int c;

    while (first != last && (c = *first++)) hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

static unsigned long HashValue(Tiny_Value value) {
    switch (value.type) {
        case TINY_VAL_BOOL:
            return (int)value.boolean + 1;
        case TINY_VAL_INT:
            return (Tiny_Int)value.i;
        case TINY_VAL_CONST_STRING:
        case TINY_VAL_STRING: {
            const char *start = Tiny_ToString(value);
            size_t len = Tiny_StringLen(value);

            return HashBytes(start, start + len);
        } break;
        case TINY_VAL_STRUCT:
            return (uintptr_t)value.obj;
        default: {
            void *ptr = Tiny_ToAddr(value);

            assert(ptr);

            return (uintptr_t)ptr;
        } break;
    }
}

static void Init(Dict *dict, Tiny_Context ctx, int bucketCount) {
    dict->bucketCount = bucketCount;
    dict->filledCount = 0;

    InitArray(&dict->keys, ctx);
    InitArray(&dict->values, ctx);

    Tiny_Value nullValue = {0};

    ArrayResize(&dict->keys, bucketCount, nullValue);
    ArrayResize(&dict->values, bucketCount, nullValue);
}

static void Grow(Dict *dict) {
    Dict newDict;

    Init(&newDict, dict->keys.ctx, dict->bucketCount * 2);

    for (int i = 0; i < dict->bucketCount; ++i) {
        Tiny_Value prevKey = *ArrayGet(&dict->keys, i);

        // If there was a value in that bucket
        if (!Tiny_IsNull(prevKey)) {
            Tiny_Value prevValue = *ArrayGet(&dict->values, i);
            DictSet(&newDict, prevKey, prevValue);
        }
    }

    // Free the previous data (keys etc)
    DestroyDict(dict);
    *dict = newDict;
}

void InitDict(Dict *dict, Tiny_Context ctx) { Init(dict, ctx, INIT_BUCKET_COUNT); }

void DestroyDict(Dict *dict) {
    DestroyArray(&dict->keys);
    DestroyArray(&dict->values);
}

void DictSet(Dict *dict, Tiny_Value key, Tiny_Value value) {
    // TODO: Perhaps think about having a DictSetEx where you can
    // adjust growth factor and parameters (like allow it to fail
    // if there's no space). Better yet, break these apart into
    // DictSet and DictSetGrow so that it's clear.
    if (dict->filledCount >= dict->bucketCount / 2) Grow(dict);

    unsigned long hash = HashValue(key);

    unsigned long index = hash % dict->bucketCount;

    Tiny_Value prevKey = *ArrayGet(&dict->keys, (int)index);

    unsigned long origin = index;

    // While an empty spot or the key we want to set isn't found
    while (!Tiny_IsNull(prevKey) && !Tiny_AreValuesEqual(prevKey, key)) {
        // Probe for a spot to put this key/value
        ++index;
        index %= dict->bucketCount;

        // It wrapped around so there's no space
        // Just grow and keep on trying
        if (index == origin) {
            Grow(dict);

            // Index must be recreated since bucket count changed
            index = hash % dict->bucketCount;
            origin = index;
        }

        prevKey = *ArrayGet(&dict->keys, (int)index);
    }

    // There was no key here before (i.e bucket was empty)
    if (Tiny_IsNull(prevKey)) {
        ArraySet(&dict->keys, (int)index, key);

        dict->filledCount += 1;
    } else {
        // Otherwise, we do nothing with the key since the user
        // is probably replacing the value
    }

    ArraySet(&dict->values, (int)index, value);
}

const Tiny_Value *DictGet(Dict *dict, Tiny_Value key) {
    unsigned long index = HashValue(key) % dict->bucketCount;
    unsigned long origin = index;

    for (;;) {
        Tiny_Value keyHere = *ArrayGet(&dict->keys, (int)index);

        if (Tiny_IsNull(keyHere)) return NULL;

        if (Tiny_AreValuesEqual(keyHere, key)) return ArrayGet(&dict->values, (int)index);

        index += 1;
        index %= dict->bucketCount;

        if (index == origin) {
            // Wrapped around, so it definitely doesn't exist
            return NULL;
        }
    }

    // No key here indicates that it doesn't exist
    return NULL;
}

void DictRemove(Dict *dict, Tiny_Value key) {
    unsigned long hash = HashValue(key);

    unsigned long index = hash % dict->bucketCount;
    unsigned long origin = index;

    for (;;) {
        Tiny_Value keyHere = *ArrayGet(&dict->keys, (int)index);

        if (Tiny_IsNull(keyHere)) return;  // Done

        if (Tiny_AreValuesEqual(keyHere, key)) {
            ArraySet(&dict->keys, (int)index, Tiny_Null);

            dict->filledCount -= 1;
        } else if ((HashValue(keyHere) % dict->bucketCount) != index) {
            // NOTE(Apaar): I'm amazed that I knew how to avoid tombstones in
            // a linear-probed hash table way back in the day! The code below
            // is mostly from 2017 and it is now 2023 (almost 2024)

            // keyHere was displaced into the spot for `key`

            // The given key was colliding with the original or some other
            // key was colliding with the original and took keyHere's
            // spot so remove the key from this slot and readd the key/value
            // to the table.
            const Tiny_Value *value = ArrayGet(&dict->values, (int)index);

            ArraySet(&dict->keys, (int)index, Tiny_Null);
            dict->filledCount -= 1;

            DictSet(dict, keyHere, *value);
        }

        index += 1;
        index %= dict->bucketCount;
    }
}

void DictClear(Dict *dict) {
    for (int i = 0; i < dict->bucketCount; ++i) {
        ArraySet(&dict->keys, i, Tiny_Null);
        ArraySet(&dict->values, i, Tiny_Null);
    }

    dict->filledCount = 0;
}
