#pragma once

#include <stdbool.h>

#include "tinycthread.h"

#define LIST_EACH(l) for (ListNode* node = (l).head; node; node = node->next)

typedef struct ListNode ListNode;

typedef struct {
    mtx_t mutex;

    size_t itemSize;
    ListNode* head;
    ListNode* tail;
} List;

void InitList(List* l, size_t itemSize);

void ListPushBack(List* l, const void* data);

// Returns false if there is no data
bool ListPopFront(List* l, void* data);

void DestroyList(List* l);
