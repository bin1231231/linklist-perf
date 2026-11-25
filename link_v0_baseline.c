/*
 * V0 baseline：按题目描述的直接写法。
 * 节点和数据分别 malloc；多条链表交替追加时，同一条链表的节点在内存中是分散的。
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "variants.h"

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

static LinkData *v0_new_node(int type, int size, char *mem)
{
    LinkData *n = xmalloc(sizeof(LinkData));
    n->type = type;
    n->dataSize = size;
    n->data = xmalloc((size_t)size);
    memcpy(n->data, mem, (size_t)size);
    return n;
}

LinkData *v0_InitLink(int size, char *mem)
{
    LinkData *h = v0_new_node(LINK_TYPE_HEAD, size, mem);
    h->prev = h;
    h->next = h;
    return h;
}

void v0_AppendNode(LinkData *firstNode, int size, char *mem)
{
    LinkData *n = v0_new_node(LINK_TYPE_DATA, size, mem);
    LinkData *last = firstNode->prev;
    n->prev = last;
    n->next = firstNode;
    last->next = n;
    firstNode->prev = n;
}

LinkData *v0_FindNode(LinkData *firstNode, int size, char *mem)
{
    LinkData *n = firstNode;
    do {
        if (n->dataSize == size && memcmp(n->data, mem, (size_t)size) == 0)
            return n;
        n = n->next;
    } while (n != firstNode);
    return NULL;
}

void v0_FreeLink(LinkData *firstNode)
{
    LinkData *n = firstNode->next;
    while (n != firstNode) {
        LinkData *next = n->next;
        free(n->data);
        free(n);
        n = next;
    }
    free(firstNode->data);
    free(firstNode);
}
