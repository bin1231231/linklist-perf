/*
 * V2 compact：在 V1 的节点池基础上重构节点。
 *
 * 节点缩为 32 字节（去掉 type），前 16 字节依次是 next、dataSize、数据前 4 字节，
 * dataSize 与前 4 字节合起来是 8 字节的键，遍历一个节点只需读前 16 字节，一次比较即可筛选。
 * 长度不同或开头不同的节点直接跳过，不再通过 data 指针访问数据。
 * 当链表中大量数据长度相同时（例如定长记录），V0/V1 每个节点都要 memcmp，
 * 而 V2 只有键相同的节点才需要读数据。
 */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "variants.h"
#include "arena.h"

_Static_assert(sizeof(LinkData2) == 32, "LinkData2 must be 32 bytes");
_Static_assert(offsetof(LinkData2, prefix) == offsetof(LinkData2, dataSize) + 4,
               "dataSize and prefix must be adjacent");

#define V2_FIRST_CAP 4
#define V2_MAX_CAP   256

typedef struct V2Block {
    struct V2Block *next;
    struct V2Block *tail;
    int count, cap;
    LinkData2 nodes[];
} V2Block;

static V2Block *v2_block_new(int cap)
{
    V2Block *b = malloc(sizeof(V2Block) + (size_t)cap * sizeof(LinkData2));
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

static inline V2Block *v2_head_block(LinkData2 *firstNode)
{
    return (V2Block *)((char *)firstNode - offsetof(V2Block, nodes));
}

LinkData2 *v2_InitLink(int size, char *mem)
{
    V2Block *b = v2_block_new(V2_FIRST_CAP);
    LinkData2 *h = &b->nodes[b->count++];
    h->dataSize = size;
    h->prefix = (uint32_t)(link_make_key(size, mem) >> 32);
    h->data = arena_copy(mem, size);
    h->prev = h;
    h->next = h;
    return h;
}

void v2_AppendNode(LinkData2 *firstNode, int size, char *mem)
{
    V2Block *hb = v2_head_block(firstNode);
    V2Block *t = hb->tail;
    if (t->count == t->cap) {
        int cap = t->cap * 2 > V2_MAX_CAP ? V2_MAX_CAP : t->cap * 2;
        V2Block *nb = v2_block_new(cap);
        t->next = nb;
        hb->tail = nb;
        t = nb;
    }
    LinkData2 *n = &t->nodes[t->count++];
    n->dataSize = size;
    n->prefix = (uint32_t)(link_make_key(size, mem) >> 32);
    n->data = arena_copy(mem, size);

    LinkData2 *last = firstNode->prev;
    n->prev = last;
    n->next = firstNode;
    last->next = n;
    firstNode->prev = n;
}

/* 读节点中 dataSize 与 prefix 组成的 8 字节键 */
static inline uint64_t node_key(const LinkData2 *n)
{
    uint64_t k;
    memcpy(&k, &n->dataSize, sizeof k);
    return k;
}

LinkData2 *v2_FindNode(LinkData2 *firstNode, int size, char *mem)
{
    uint64_t key = link_make_key(size, mem);
    LinkData2 *n = firstNode;
    do {
        if (node_key(n) == key && memcmp(n->data, mem, (size_t)size) == 0)
            return n;
        n = n->next;
    } while (n != firstNode);
    return NULL;
}

void v2_FreeLink(LinkData2 *firstNode)
{
    V2Block *b = v2_head_block(firstNode);
    while (b) {
        V2Block *next = b->next;
        free(b);
        b = next;
    }
}
