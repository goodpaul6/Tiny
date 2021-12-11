#pragma once

typedef struct {
    int cap;
    int len;
    char* s;
} StringBuilder;

void InitStringBuilder(StringBuilder* b);

void ClearStringBuilder(StringBuilder* b);

void AppendChar(StringBuilder* b, char c);
void AppendString(StringBuilder* b, const char* s);

// b->s is guaranteed to be null terminated, so just get that to get
// the built string

void DestroyStringBuilder(StringBuilder* b);
