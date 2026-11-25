/*
 * test.c - 正确性测试
 *
 * 对四个版本建立相同内容的链表，检查：
 *   1. 链表结构：头节点 prev 指向最后一个节点；沿 next 走一圈节点顺序与追加顺序一致；prev 与 next 对应；
 *   2. FindNode：命中头节点、中间节点、尾节点；数据重复时返回第一个；长度相同但内容不同、
 *      前 4 字节相同但后面不同、长度不同但前缀相同等情况都能正确区分；查不到时返回 NULL；
 *   3. 随机测试：随机长度（含 1~7 字节、32、512、较长数据）、随机内容（含大量重复），
 *      四个版本返回节点的位置必须完全一致，并与逐个比较的结果一致。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linklist.h"
#include "variants.h"
#include "arena.h"

static int failures;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

#define MAXN 2000

static int sizes[MAXN];
static char *datas[MAXN];

/* 节点在链表中的位置（沿 next 从头数），找不到返回 -1 */
#define POS_OF(T, first, target, out)                    \
    do {                                                 \
        T *n_ = (first);                                 \
        int p_ = 0;                                      \
        (out) = -1;                                      \
        if (target)                                      \
            do {                                         \
                if (n_ == (target)) { (out) = p_; break; } \
                n_ = n_->next;                           \
                p_++;                                    \
            } while (n_ != (first));                     \
    } while (0)

#define CHECK_STRUCT(T, first, count, label)                                         \
    do {                                                                             \
        T *n_ = (first);                                                             \
        int i_ = 0;                                                                  \
        CHECK((first)->prev->next == (first), "%s: last->next != head", label);      \
        do {                                                                         \
            CHECK(i_ < (count), "%s: list longer than %d", label, (count));          \
            if (i_ >= (count)) break;                                                \
            CHECK(n_->dataSize == sizes[i_] &&                                       \
                  memcmp(n_->data, datas[i_], (size_t)sizes[i_]) == 0,               \
                  "%s: node %d data mismatch", label, i_);                           \
            CHECK(n_->next->prev == n_, "%s: node %d next->prev mismatch", label, i_); \
            if (i_ == (count) - 1)                                                   \
                CHECK((first)->prev == n_, "%s: head->prev is not the last node", label); \
            n_ = n_->next;                                                           \
            i_++;                                                                    \
        } while (n_ != (first));                                                     \
        CHECK(i_ == (count), "%s: walked %d nodes, expected %d", label, i_, (count)); \
    } while (0)

static int expected_pos(int count, int size, const char *mem)
{
    for (int i = 0; i < count; i++)
        if (sizes[i] == size && memcmp(datas[i], mem, (size_t)size) == 0)
            return i;
    return -1;
}

/* 建立四个版本的链表，检查结构，再对 queries 逐个比对 */
static void run_case(const char *name, int count, int nqueries, int *qsize, char **qmem)
{
    LinkData *h0 = v0_InitLink(sizes[0], datas[0]);
    LinkData *h1 = v1_InitLink(sizes[0], datas[0]);
    LinkData2 *h2 = v2_InitLink(sizes[0], datas[0]);
    LinkData *h3 = InitLink(sizes[0], datas[0]);
    CHECK(h3->prev == h3 && h3->next == h3, "%s: InitLink head not self-linked", name);
    for (int i = 1; i < count; i++) {
        v0_AppendNode(h0, sizes[i], datas[i]);
        v1_AppendNode(h1, sizes[i], datas[i]);
        v2_AppendNode(h2, sizes[i], datas[i]);
        AppendNode(h3, sizes[i], datas[i]);
    }
    CHECK_STRUCT(LinkData, h0, count, "V0");
    CHECK_STRUCT(LinkData, h1, count, "V1");
    CHECK_STRUCT(LinkData2, h2, count, "V2");
    CHECK_STRUCT(LinkData, h3, count, "V3");

    for (int q = 0; q < nqueries; q++) {
        int want = expected_pos(count, qsize[q], qmem[q]);
        int p0, p1, p2, p3;
        LinkData *r0 = v0_FindNode(h0, qsize[q], qmem[q]);
        LinkData *r1 = v1_FindNode(h1, qsize[q], qmem[q]);
        LinkData2 *r2 = v2_FindNode(h2, qsize[q], qmem[q]);
        LinkData *r3 = FindNode(h3, qsize[q], qmem[q]);
        POS_OF(LinkData, h0, r0, p0);
        POS_OF(LinkData, h1, r1, p1);
        POS_OF(LinkData2, h2, r2, p2);
        POS_OF(LinkData, h3, r3, p3);
        CHECK(p0 == want && p1 == want && p2 == want && p3 == want,
              "%s: query %d expected pos %d, got V0=%d V1=%d V2=%d V3=%d", name, q, want, p0, p1, p2, p3);
    }
    v0_FreeLink(h0);
    v1_FreeLink(h1);
    v2_FreeLink(h2);
    FreeLink(h3);
}

static uint64_t rng = 88172645463325252ull;
static uint64_t xr(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static void set_node(int i, int size, const char *src)
{
    sizes[i] = size;
    datas[i] = realloc(datas[i], (size_t)size);
    memcpy(datas[i], src, (size_t)size);
}

static void fixed_cases(void)
{
    char a[600], b[600];
    int qsize[16];
    char *qmem[16];
    char qbuf[16][600];
    int count = 0;

    /* 0: 32 字节；1: 与 0 前 4 字节相同、后面不同；2: 512 字节；3: 与 0 完全相同（重复）；
       4: 长度不同但前 4 字节与 0 相同；5: 短数据 3 字节；6..: 长度 64 的普通数据 */
    memset(a, 'A', sizeof a);
    set_node(count++, 32, a);
    memcpy(b, a, 32);
    b[31] = 'B';
    set_node(count++, 32, b);
    for (int i = 0; i < 512; i++)
        b[i] = (char)i;
    set_node(count++, 512, b);
    set_node(count++, 32, a);
    set_node(count++, 33, a);
    set_node(count++, 3, "xyz");
    for (int k = 0; k < 40; k++) {
        for (int i = 0; i < 64; i++)
            b[i] = (char)(k * 7 + i);
        set_node(count++, 64, b);
    }

    int nq = 0;
#define ADDQ(sz, src) do { qsize[nq] = (sz); memcpy(qbuf[nq], (src), (size_t)(sz)); qmem[nq] = qbuf[nq]; nq++; } while (0)
    ADDQ(32, a);                       /* 头节点（与第 3 个重复，应返回头节点） */
    ADDQ(sizes[1], datas[1]);          /* 前缀相同、末字节不同 */
    ADDQ(sizes[2], datas[2]);          /* 512 字节 */
    ADDQ(33, a);                       /* 长度不同、前缀相同 */
    ADDQ(3, "xyz");                    /* 短数据 */
    ADDQ(sizes[count - 1], datas[count - 1]);   /* 尾节点 */
    ADDQ(sizes[20], datas[20]);        /* 中间节点 */
    memcpy(b, datas[20], 64);
    b[63] ^= 1;
    ADDQ(64, b);                       /* 不存在：最后一个字节不同 */
    ADDQ(31, a);                       /* 不存在：长度不同 */
    ADDQ(4, "xyzw");                   /* 不存在 */
    run_case("fixed", count, nq, qsize, qmem);
}

static void random_cases(void)
{
    static int qsize[400];
    static char *qmem[400];
    for (int round = 0; round < 60; round++) {
        int count = 1 + (int)(xr() % (round < 20 ? 30 : MAXN));
        int alphabet = 1 + (int)(xr() % 4);        /* 字符种类少时会有大量重复和公共前缀 */
        for (int i = 0; i < count; i++) {
            char tmp[1200];
            int size;
            switch (xr() % 6) {
            case 0: size = 1 + (int)(xr() % 7); break;
            case 1: size = 32; break;
            case 2: size = 512; break;
            case 3: size = 1000 + (int)(xr() % 100); break;
            default: size = 32 + (int)(xr() % 481); break;
            }
            for (int k = 0; k < size; k++)
                tmp[k] = (char)('a' + xr() % (uint64_t)alphabet);
            if (i > 0 && xr() % 5 == 0) {        /* 20% 复制一个已有节点 */
                int src = (int)(xr() % (uint64_t)i);
                set_node(i, sizes[src], datas[src]);
            } else
                set_node(i, size, tmp);
        }
        int nq = 400;
        for (int q = 0; q < nq; q++) {
            int src = (int)(xr() % (uint64_t)count);
            free(qmem[q]);
            qsize[q] = sizes[src];
            qmem[q] = malloc((size_t)qsize[q]);
            memcpy(qmem[q], datas[src], (size_t)qsize[q]);
            if (q % 3 == 0)                      /* 三分之一改一个字节，多数变成查不到 */
                qmem[q][xr() % (uint64_t)qsize[q]] ^= 1;
        }
        char name[32];
        snprintf(name, sizeof name, "random#%d", round);
        run_case(name, count, nq, qsize, qmem);
        arena_reset();
    }
}

int main(void)
{
    fixed_cases();
    arena_reset();
    random_cases();
    arena_reset();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
