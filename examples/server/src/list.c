#include "list.h"

#include <stdio.h>
#include <stdlib.h>

struct ListNode {
    ListNode* next;
    char data[];
};

static ListNode* CreateNode(const void* data, size_t itemSize) {
    ListNode* node = malloc(sizeof(ListNode) + itemSize);

    node->next = NULL;
    memcpy(node->data, data, itemSize);

    return node;
}

void InitList(List* l, size_t itemSize) {
    mtx_init(&l->mutex, mtx_plain);

    l->itemSize = itemSize;
    l->head = l->tail = NULL;
}

void ListPushBack(List* l, const void* data) {
    ListNode* node = CreateNode(data, l->itemSize);

    mtx_lock(&l->mutex);

    if (!l->tail) {
        l->head = l->tail = node;
    } else {
        l->tail->next = node;
        l->tail = node;
    }

    mtx_unlock(&l->mutex);
}

bool ListPopFront(List* l, void* data) {
    mtx_lock(&l->mutex);

    // I guess someone could be pushing back as I
    // pop and we want that to be serial, so I'm
    // putting it after the lock operation
    if (!l->head) {
        mtx_unlock(&l->mutex);
        return false;
    }

    ListNode* node = l->head;

    memcpy(data, node->data, l->itemSize);

    l->head = node->next;
    free(node);

    if (!l->head) {
        l->tail = NULL;
    }

    mtx_unlock(&l->mutex);

    return true;
}

void DestroyList(List* l) {
    mtx_destroy(&l->mutex);

    while (l->head) {
        ListNode* next = l->head->next;

        free(l->head);

        l->head = next;
    }
}
