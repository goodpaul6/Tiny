#include "stringbuilder.h"

void InitStringBuilder(StringBuilder* b) {
    b->cap = 0;
    b->len = 0;
    b->s = NULL;
}

void ClearStringBuilder(StringBuilder* b) { b->len = 0; }

void AppendChar(StringBuilder* b, char c) {
    while (b->len + 2 >= b->cap) {
        if (b->cap == 0) b->cap = 1;
        b->cap *= 2;

        b->s = realloc(b->s, b->cap);
    }

    b->s[b->len++] = c;
    b->s[len] = 0;
}

void AppendString(StringBuilder* b, const char* s) {
    size_t len = strlen(s);

    while (b->len + len >= b->cap) {
        if (b->cap == 0) b->cap = 1;
        b->cap *= 2;

        b->s = realloc(b->s, b->cap);
    }

    strcpy(b->s[b->len], s);
    b->len += len;
}

void DestroyStringBuilder(StringBuilder* b) { free(b->s); }
