#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "arena.h"

#define ARENA_BLOCK (64u << 20)   /* 每块 64MB */

typedef struct ArenaBlock {
    struct ArenaBlock *prev;
    size_t used, cap;
    char buf[];
} ArenaBlock;

static ArenaBlock *cur;
static size_t total;

void *arena_alloc(size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (!cur || cur->used + n > cur->cap) {
        size_t cap = n > ARENA_BLOCK ? n : ARENA_BLOCK;
        ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
        if (!b) {
            fprintf(stderr, "arena: out of memory\n");
            exit(1);
        }
        b->prev = cur;
        b->used = 0;
        b->cap = cap;
        cur = b;
        total += sizeof(ArenaBlock) + cap;
    }
    void *p = cur->buf + cur->used;
    cur->used += n;
    return p;
}

char *arena_copy(const char *src, int n)
{
    char *p = arena_alloc((size_t)n);
    memcpy(p, src, (size_t)n);
    return p;
}

void arena_reset(void)
{
    while (cur) {
        ArenaBlock *p = cur->prev;
        free(cur);
        cur = p;
    }
    total = 0;
}

size_t arena_bytes(void)
{
    return total;
}
