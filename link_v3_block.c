/*
 * V3 block：正式实现（InitLink / AppendNode / FindNode）。
 *
 * 节点结构体保持题目原样，prev/next 照常维护，链表的语义不变。
 * 节点按块连续存放（块容量 8、16、32 ... 256），每块在节点数组之前另存一个键数组，
 * keys[i] 对应 nodes[i]。键 = 长度（低 32 位）| 数据前 4 字节（高 32 位），见 linklist.h。
 *
 * FindNode 从头节点所在的第一块开始，按链表顺序逐块遍历：
 *   - 只读键数组，8 字节一个节点，一条 64 字节 Cache 行覆盖 8 个节点；
 *   - 用 AVX2 的 _mm256_cmpeq_epi64 一次比较 4 个键；
 *   - 只有键相同的节点才比较完整数据，返回第一个匹配的节点；
 *   - 开始扫描一块时，先预取下一块的块头和前几行键，掩盖块与块之间跳转的访存延迟。
 *
 * 块的内存布局（64 字节对齐）：
 *   [块头 32B][keys[cap]，每个 8B][nodes[cap]，每个 32B]
 * 块头与前 4 个键在同一条 Cache 行内，读块头时顺带取到第一批键。
 * 头节点总是第一块的 nodes[0]，第一块容量固定为 V3_FIRST_CAP，由头节点地址可直接算出第一块地址。
 */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#ifdef __AVX2__
#include <immintrin.h>
#endif
#include "linklist.h"
#include "arena.h"

#define V3_FIRST_CAP 8
#define V3_MAX_CAP   256

typedef struct Block {
    struct Block *next;     /* 按链表顺序的下一块 */
    struct Block *tail;     /* 仅第一块有效：当前最后一块 */
    LinkData *nodes;        /* 指向本块的节点数组 */
    int count, cap;
    uint64_t keys[];        /* cap 个键，其后紧跟 cap 个节点 */
} Block;

_Static_assert(sizeof(LinkData) == 32, "LinkData must stay 32 bytes");
_Static_assert(offsetof(Block, keys) == 32, "block header must be 32 bytes");

static Block *block_new(int cap)
{
    size_t nodes_off = offsetof(Block, keys) + (size_t)cap * sizeof(uint64_t);
    size_t bytes = nodes_off + (size_t)cap * sizeof(LinkData);
    bytes = (bytes + 63) & ~(size_t)63;
    Block *b = aligned_alloc(64, bytes);
    if (!b) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    b->next = NULL;
    b->tail = b;
    b->nodes = (LinkData *)((char *)b + nodes_off);
    b->count = 0;
    b->cap = cap;
    return b;
}

static inline Block *head_block(LinkData *firstNode)
{
    return (Block *)((char *)firstNode - offsetof(Block, keys) -
                     V3_FIRST_CAP * sizeof(uint64_t));
}

LinkData *InitLink(int size, char *mem)
{
    Block *b = block_new(V3_FIRST_CAP);
    LinkData *h = &b->nodes[0];
    b->keys[0] = link_make_key(size, mem);
    b->count = 1;
    h->type = LINK_TYPE_HEAD;
    h->dataSize = size;
    h->data = arena_copy(mem, size);
    h->prev = h;
    h->next = h;
    return h;
}

void AppendNode(LinkData *firstNode, int size, char *mem)
{
    Block *hb = head_block(firstNode);
    Block *t = hb->tail;
    if (t->count == t->cap) {
        int cap = t->cap * 2 > V3_MAX_CAP ? V3_MAX_CAP : t->cap * 2;
        Block *nb = block_new(cap);
        t->next = nb;
        hb->tail = nb;
        t = nb;
    }
    int i = t->count++;
    LinkData *n = &t->nodes[i];
    t->keys[i] = link_make_key(size, mem);
    n->type = LINK_TYPE_DATA;
    n->dataSize = size;
    n->data = arena_copy(mem, size);

    LinkData *last = firstNode->prev;
    n->prev = last;
    n->next = firstNode;
    last->next = n;
    firstNode->prev = n;
}

static inline int node_match(const LinkData *n, int size, const char *mem)
{
    return n->dataSize == size && memcmp(n->data, mem, (size_t)size) == 0;
}

static inline void prefetch_block(const Block *b)
{
    if (b) {
        __builtin_prefetch(b);
        __builtin_prefetch((const char *)b + 64);
        __builtin_prefetch((const char *)b + 128);
    }
}

LinkData *FindNode(LinkData *firstNode, int size, char *mem)
{
    uint64_t key = link_make_key(size, mem);
    Block *b = head_block(firstNode);

    /* 第一块的节点数组从头节点开始，共 V3_FIRST_CAP * 32 = 256 字节（4 条 Cache 行）。
     * 在比较键的同时预取这几行：短链表命中时，紧接着要读的节点已经在路上了。 */
    for (int i = 0; i < V3_FIRST_CAP; i += 2)
        __builtin_prefetch(&firstNode[i]);

#ifdef __AVX2__
    __m256i vkey = _mm256_set1_epi64x((long long)key);
    for (; b; b = b->next) {
        prefetch_block(b->next);
        const uint64_t *k = b->keys;
        int n = b->count;
        for (int i = 0; i < n; i += 4) {
            __m256i v = _mm256_load_si256((const __m256i *)(k + i));
            unsigned m = (unsigned)_mm256_movemask_pd(
                _mm256_castsi256_pd(_mm256_cmpeq_epi64(v, vkey)));
            if (m) {
                if (n - i < 4)
                    m &= (1u << (n - i)) - 1;   /* 去掉块尾未使用的槽位 */
                while (m) {
                    int j = i + __builtin_ctz(m);
                    m &= m - 1;
                    if (node_match(&b->nodes[j], size, mem))
                        return &b->nodes[j];
                }
            }
        }
    }
#else
    for (; b; b = b->next) {
        prefetch_block(b->next);
        const uint64_t *k = b->keys;
        int n = b->count;
        for (int i = 0; i < n; i++)
            if (k[i] == key && node_match(&b->nodes[i], size, mem))
                return &b->nodes[i];
    }
#endif
    return NULL;
}

void FreeLink(LinkData *firstNode)
{
    Block *b = head_block(firstNode);
    while (b) {
        Block *next = b->next;
        free(b);
        b = next;
    }
}
