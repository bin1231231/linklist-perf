/*
 * V1 pool：结构体与 FindNode 与 V0 相同，只改变节点的内存布局。
 *
 * 每条链表有自己的节点池：池由若干块组成，块容量 4、8、16 ... 256 递增。
 * 头节点是第一块的 nodes[0]，第一块的块头里记录当前最后一块，追加时直接取下一个空位。
 * 于是同一条链表的节点按追加顺序连续存放，遍历时访存地址是顺序的，
 * 硬件预取器可以提前把后面的节点取进 Cache。数据放入公共数据区（arena）。
 */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "variants.h"
#include "arena.h"

#define V1_FIRST_CAP 4
#define V1_MAX_CAP   256

typedef struct V1Block {
    struct V1Block *next;   /* 块链，用于追加和释放 */
    struct V1Block *tail;   /* 仅第一块有效：当前最后一块 */
    int count, cap;
    LinkData nodes[];
} V1Block;

static V1Block *v1_block_new(int cap)
{
    V1Block *b = malloc(sizeof(V1Block) + (size_t)cap * sizeof(LinkData));
    if (!b) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    b->next = NULL;
    b->tail = b;
    b->count = 0;
    b->cap = cap;
    return b;
}

static inline V1Block *v1_head_block(LinkData *firstNode)
{
    return (V1Block *)((char *)firstNode - offsetof(V1Block, nodes));
}

LinkData *v1_InitLink(int size, char *mem)
{
    V1Block *b = v1_block_new(V1_FIRST_CAP);
    LinkData *h = &b->nodes[b->count++];
    h->type = LINK_TYPE_HEAD;
    h->dataSize = size;
    h->data = arena_copy(mem, size);
    h->prev = h;
    h->next = h;
    return h;
}

void v1_AppendNode(LinkData *firstNode, int size, char *mem)
{
    V1Block *hb = v1_head_block(firstNode);
    V1Block *t = hb->tail;
    if (t->count == t->cap) {
        int cap = t->cap * 2 > V1_MAX_CAP ? V1_MAX_CAP : t->cap * 2;
        V1Block *nb = v1_block_new(cap);
        t->next = nb;
        hb->tail = nb;
        t = nb;
    }
    LinkData *n = &t->nodes[t->count++];
    n->type = LINK_TYPE_DATA;
    n->dataSize = size;
    n->data = arena_copy(mem, size);

    LinkData *last = firstNode->prev;
    n->prev = last;
    n->next = firstNode;
    last->next = n;
    firstNode->prev = n;
}

LinkData *v1_FindNode(LinkData *firstNode, int size, char *mem)
{
    LinkData *n = firstNode;
    do {
        if (n->dataSize == size && memcmp(n->data, mem, (size_t)size) == 0)
            return n;
        n = n->next;
    } while (n != firstNode);
    return NULL;
}

void v1_FreeLink(LinkData *firstNode)
{
    V1Block *b = v1_head_block(firstNode);
    while (b) {
        V1Block *next = b->next;
        free(b);
        b = next;
    }
}
