#include <stdlib.h>

#include "context.h"

void* Tiny_DefaultAlloc(void* data, void* ptr, size_t newSize)
{
    if(newSize == 0) {
        free(ptr);
        return NULL;
    }

    return realloc(ptr, newSize);
}
