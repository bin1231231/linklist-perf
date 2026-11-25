/*
 * bench.c - 链表搜索性能测试
 *
 * 1. 按交替顺序（第 0 个节点追加到每条链表，再第 1 个……）建立 lists 条链表，
 *    每条 nodes 个节点，数据长度 32~512 字节（或固定长度）。数据由 (链表号, 位置) 确定性生成，
 *    各版本的链表内容完全相同。
 * 2. 预先生成查询：随机选一条链表，75% 查询链表中已有的数据（位置随机），25% 查询不存在的数据。
 * 3. 对每个版本：建表 -> 打开计数器 -> 执行全部查询 -> 关闭计数器 -> 校验结果 -> 释放。
 *    计数范围是整个查询循环，主要开销是 FindNode 内的遍历。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <unistd.h>
#include "linklist.h"
#include "variants.h"
#include "arena.h"
#include "pmu.h"

/* ---------- 各版本统一接口 ---------- */

typedef struct {
    const char *name;
    const char *desc;
    void *(*init)(int, char *);
    void  (*append)(void *, int, char *);
    void *(*find)(void *, int, char *);
    void  (*destroy)(void *);
    int   (*data_eq)(void *, int, const char *);
} Variant;

static int eq_linkdata(void *p, int size, const char *mem)
{
    LinkData *n = p;
    return n->dataSize == size && memcmp(n->data, mem, (size_t)size) == 0;
}

static int eq_linkdata2(void *p, int size, const char *mem)
{
    LinkData2 *n = p;
    return n->dataSize == size && memcmp(n->data, mem, (size_t)size) == 0;
}

#define WRAP(prefix, T)                                                              \
    static void *prefix##_init_w(int s, char *m) { return prefix##_InitLink(s, m); } \
    static void prefix##_append_w(void *f, int s, char *m) { prefix##_AppendNode((T *)f, s, m); } \
    static void *prefix##_find_w(void *f, int s, char *m) { return prefix##_FindNode((T *)f, s, m); } \
    static void prefix##_free_w(void *f) { prefix##_FreeLink((T *)f); }

WRAP(v0, LinkData)
WRAP(v1, LinkData)
WRAP(v2, LinkData2)

static void *v3_init_w(int s, char *m) { return InitLink(s, m); }
static void v3_append_w(void *f, int s, char *m) { AppendNode((LinkData *)f, s, m); }
static void *v3_find_w(void *f, int s, char *m) { return FindNode((LinkData *)f, s, m); }
static void v3_free_w(void *f) { FreeLink((LinkData *)f); }

static const Variant variants[] = {
    { "V0 baseline", "malloc per node and per data",          v0_init_w, v0_append_w, v0_find_w, v0_free_w, eq_linkdata },
    { "V1 pool",     "per-list node pool, same FindNode",     v1_init_w, v1_append_w, v1_find_w, v1_free_w, eq_linkdata },
    { "V2 compact",  "pool + 8-byte key in node header",      v2_init_w, v2_append_w, v2_find_w, v2_free_w, eq_linkdata2 },
    { "V3 block",    "blocked nodes + key array + AVX2",      v3_init_w, v3_append_w, v3_find_w, v3_free_w, eq_linkdata },
};
#define NVARIANTS (int)(sizeof variants / sizeof variants[0])

/* ---------- 数据生成 ---------- */

static uint64_t g_seed = 20251124;
static int g_fixed_size = 0;      /* 0：长度随机 32~512；否则为固定长度 */

static inline uint64_t splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* 由 (链表号, 位置) 生成一份数据，返回长度。不同位置的数据一定不同。 */
static int gen_data(int list, int pos, char *out)
{
    uint64_t s = g_seed ^ (((uint64_t)(uint32_t)list << 32) | (uint32_t)pos);
    uint64_t first = splitmix64(&s);
    int size = g_fixed_size ? g_fixed_size : 32 + (int)(first % 481);
    for (int i = 0; i < size; i += 8) {
        uint64_t r = splitmix64(&s);
        int n = size - i < 8 ? size - i : 8;
        memcpy(out + i, &r, (size_t)n);
    }
    return size;
}

/* ---------- 查询 ---------- */

typedef struct {
    int list;
    int pos;          /* 目标位置；-1 表示查询不存在的数据 */
    int size;
    int pad;
    uint64_t off;     /* 查询数据在 keybuf 中的偏移 */
} Query;

/* 当前进程常驻内存（MB） */
static double rss_mb(void)
{
    long pages = 0, resident = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &resident) != 2)
            resident = 0;
        fclose(f);
    }
    return (double)resident * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t __attribute__((noinline))
run_queries(const Variant *v, void **heads, const Query *qs, int nq, char *keybuf)
{
    uint64_t found = 0;
    for (int i = 0; i < nq; i++) {
        const Query *q = &qs[i];
        found += v->find(heads[q->list], q->size, keybuf + q->off) != NULL;
    }
    return found;
}

static uint64_t verify(const Variant *v, void **heads, const Query *qs, int nq, char *keybuf)
{
    uint64_t errors = 0;
    for (int i = 0; i < nq; i++) {
        const Query *q = &qs[i];
        void *r = v->find(heads[q->list], q->size, keybuf + q->off);
        if (q->pos < 0)
            errors += r != NULL;
        else
            errors += !r || !v->data_eq(r, q->size, keybuf + q->off);
    }
    return errors;
}

/* ---------- 输出 ---------- */

typedef struct {
    int ran;
    double build_s, search_s;
    uint64_t found, errors;
    double mem_mb;          /* 建表后增加的常驻内存 */
    PmuResult pmu;
} Result;

static void fmt_count(char *buf, size_t n, const PmuResult *p, int ev)
{
    if (!p->valid[ev])
        snprintf(buf, n, "n/a");
    else
        snprintf(buf, n, "%.0f", p->value[ev]);
}

static void fmt_ratio(char *buf, size_t n, const PmuResult *p, int ev, double denom)
{
    if (!p->valid[ev] || denom <= 0)
        snprintf(buf, n, "n/a");
    else
        snprintf(buf, n, "%.2f", p->value[ev] / denom);
}

static void print_results(const Result *res, double visits, int markdown)
{
    const char *sep = markdown ? " | " : "  ";
    const char *lb = markdown ? "| " : "";
    const char *rb = markdown ? " |" : "";
    double base_t = res[0].ran ? res[0].search_s : 0;
    char a[32], b[32], c[32], d[32], e[32], f[32], g[32];

    printf("\n[1] Search time\n\n");
    printf("%s%-12s%s%9s%s%9s%s%11s%s%9s%s%8s%s%7s%s\n", lb, "variant", sep, "build(s)", sep, "mem(MB)",
           sep, "search(ms)", sep, "ns/node", sep, "speedup", sep, "errors", rb);
    if (markdown)
        printf("|---|---:|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < NVARIANTS; i++) {
        const Result *r = &res[i];
        if (!r->ran)
            continue;
        if (base_t > 0)
            snprintf(a, sizeof a, "%.1fx", base_t / r->search_s);
        else
            snprintf(a, sizeof a, "-");
        printf("%s%-12s%s%9.2f%s%9.0f%s%11.1f%s%9.2f%s%8s%s%7llu%s\n", lb, variants[i].name, sep, r->build_s,
               sep, r->mem_mb, sep, r->search_s * 1e3, sep, r->search_s * 1e9 / visits, sep, a, sep,
               (unsigned long long)r->errors, rb);
    }

    printf("\n[2] Hardware counters, totals over all queries\n\n");
    printf("%s%-12s%s%14s%s%14s%s%6s%s%14s%s%12s%s%8s%s%12s%s\n", lb, "variant", sep, "cycles", sep,
           "instructions", sep, "IPC", sep, "L1D loads", sep, "L1D misses", sep, "L1 hit%", sep,
           "LLC misses", rb);
    if (markdown)
        printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < NVARIANTS; i++) {
        const Result *r = &res[i];
        if (!r->ran)
            continue;
        const PmuResult *p = &r->pmu;
        fmt_count(a, sizeof a, p, PMU_CYCLES);
        fmt_count(b, sizeof b, p, PMU_INSTRUCTIONS);
        if (p->valid[PMU_CYCLES] && p->valid[PMU_INSTRUCTIONS] && p->value[PMU_CYCLES] > 0)
            snprintf(c, sizeof c, "%.2f", p->value[PMU_INSTRUCTIONS] / p->value[PMU_CYCLES]);
        else
            snprintf(c, sizeof c, "n/a");
        fmt_count(d, sizeof d, p, PMU_L1D_LOADS);
        fmt_count(e, sizeof e, p, PMU_L1D_MISSES);
        if (p->valid[PMU_L1D_LOADS] && p->valid[PMU_L1D_MISSES] && p->value[PMU_L1D_LOADS] > 0)
            snprintf(f, sizeof f, "%.2f",
                     100.0 * (1.0 - p->value[PMU_L1D_MISSES] / p->value[PMU_L1D_LOADS]));
        else
            snprintf(f, sizeof f, "n/a");
        fmt_count(g, sizeof g, p, PMU_LLC_MISSES);
        printf("%s%-12s%s%14s%s%14s%s%6s%s%14s%s%12s%s%8s%s%12s%s\n", lb, variants[i].name, sep, a, sep, b,
               sep, c, sep, d, sep, e, sep, f, sep, g, rb);
    }

    printf("\n[3] Per node visited\n\n");
    printf("%s%-12s%s%8s%s%8s%s%10s%s%12s%s%12s%s\n", lb, "variant", sep, "cycles", sep, "instr", sep,
           "L1D loads", sep, "L1D misses", sep, "LLC misses", rb);
    if (markdown)
        printf("|---|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < NVARIANTS; i++) {
        const Result *r = &res[i];
        if (!r->ran)
            continue;
        const PmuResult *p = &r->pmu;
        fmt_ratio(a, sizeof a, p, PMU_CYCLES, visits);
        fmt_ratio(b, sizeof b, p, PMU_INSTRUCTIONS, visits);
        fmt_ratio(c, sizeof c, p, PMU_L1D_LOADS, visits);
        fmt_ratio(d, sizeof d, p, PMU_L1D_MISSES, visits);
        fmt_ratio(e, sizeof e, p, PMU_LLC_MISSES, visits);
        printf("%s%-12s%s%8s%s%8s%s%10s%s%12s%s%12s%s\n", lb, variants[i].name, sep, a, sep, b, sep, c, sep,
               d, sep, e, rb);
    }
    for (int i = 0; i < NVARIANTS; i++)
        if (res[i].ran && res[i].pmu.multiplexed) {
            printf("\nNote: some counters were time-multiplexed; values are scaled estimates.\n");
            break;
        }
}

/* ---------- 主程序 ---------- */

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n"
           "  --check               only check whether hardware counters can be read\n"
           "  --preset NAME         wide (1000000 lists x 8), mid (20000 x 200, default), long (200 x 10000)\n"
           "  --lists N --nodes N   number of lists / nodes per list\n"
           "  --queries N           number of FindNode calls\n"
           "  --hit PCT             percentage of queries whose data exists (default 75)\n"
           "  --fixed-size N        all data N bytes long (default: random 32~512)\n"
           "  --variants LIST       e.g. 0,3 (default: all)\n"
           "  --seed N              data seed\n"
           "  --markdown            print tables in Markdown\n", prog);
}

static int check_mode(void)
{
    int ok = pmu_init(1);
    PmuResult r;
    volatile uint64_t sink = 0;
    pmu_start();
    for (uint64_t i = 0; i < 10000000; i++)
        sink += i;
    pmu_stop(&r);
    printf("\nSelf test (10M loop iterations):\n");
    for (int i = 0; i < PMU_NEVENTS; i++) {
        if (r.valid[i])
            printf("  %-13s = %.0f\n", pmu_event_name(i), r.value[i]);
        else
            printf("  %-13s = n/a\n", pmu_event_name(i));
    }
    int core_ok = r.valid[PMU_CYCLES] && r.valid[PMU_INSTRUCTIONS] && r.value[PMU_INSTRUCTIONS] > 0;
    printf("\nResult: %s\n", core_ok ? "hardware counters OK" :
           "hardware counters NOT available here");
    (void)ok;
    return core_ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    int lists = 20000, nodes = 200, nq = 100000, hit = 75, markdown = 0;
    int want[NVARIANTS];
    const char *preset = "mid";
    for (int i = 0; i < NVARIANTS; i++)
        want[i] = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--check"))
            return check_mode();
        else if (!strcmp(a, "--markdown"))
            markdown = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (v && !strcmp(a, "--preset")) {
            preset = v;
            i++;
            if (!strcmp(v, "wide")) { lists = 1000000; nodes = 8; nq = 500000; }
            else if (!strcmp(v, "mid")) { lists = 20000; nodes = 200; nq = 100000; }
            else if (!strcmp(v, "long")) { lists = 200; nodes = 10000; nq = 2000; }
            else { fprintf(stderr, "unknown preset %s\n", v); return 2; }
        } else if (v && !strcmp(a, "--lists")) { lists = atoi(v); i++; preset = "custom"; }
        else if (v && !strcmp(a, "--nodes")) { nodes = atoi(v); i++; preset = "custom"; }
        else if (v && !strcmp(a, "--queries")) { nq = atoi(v); i++; }
        else if (v && !strcmp(a, "--hit")) { hit = atoi(v); i++; }
        else if (v && !strcmp(a, "--fixed-size")) { g_fixed_size = atoi(v); i++; }
        else if (v && !strcmp(a, "--seed")) { g_seed = strtoull(v, NULL, 10); i++; }
        else if (v && !strcmp(a, "--variants")) {
            for (int k = 0; k < NVARIANTS; k++)
                want[k] = 0;
            for (const char *p = v; *p; p++)
                if (*p >= '0' && *p < '0' + NVARIANTS)
                    want[*p - '0'] = 1;
            i++;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (lists <= 0 || nodes <= 0 || nq <= 0 || hit < 0 || hit > 100 ||
        (g_fixed_size && (g_fixed_size < 1 || g_fixed_size > 65535))) {
        fprintf(stderr, "invalid arguments\n");
        return 2;
    }

    printf("== Environment\n");
    pmu_init(1);

    /* 生成查询 */
    Query *qs = malloc((size_t)nq * sizeof(Query));
    char *keybuf = malloc((size_t)nq * (g_fixed_size ? (size_t)g_fixed_size : 512));
    void **heads = malloc((size_t)lists * sizeof(void *));
    char *buf = malloc(65536);
    if (!qs || !keybuf || !heads || !buf) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    uint64_t rs = g_seed * 31 + 7, off = 0;
    double visits = 0;
    int nhit = 0;
    for (int i = 0; i < nq; i++) {
        Query *q = &qs[i];
        q->list = (int)(splitmix64(&rs) % (uint64_t)lists);
        if ((int)(splitmix64(&rs) % 100) < hit) {
            q->pos = (int)(splitmix64(&rs) % (uint64_t)nodes);
            q->size = gen_data(q->list, q->pos, keybuf + off);
            visits += q->pos + 1;
            nhit++;
        } else {
            q->pos = -1;
            q->size = gen_data(q->list, nodes + 1 + i % 4096, keybuf + off);
            visits += nodes;
        }
        q->off = off;
        off += (uint64_t)q->size;
    }

    printf("\n== Config\n");
    printf("preset=%s  lists=%d  nodes/list=%d  total nodes=%.0f\n", preset, lists, nodes,
           (double)lists * nodes);
    if (g_fixed_size)
        printf("data size=%d bytes (fixed)", g_fixed_size);
    else
        printf("data size=32~512 bytes (random)");
    printf("  queries=%d (hit %d, miss %d)  node visits=%.0f  seed=%llu\n", nq, nhit, nq - nhit, visits,
           (unsigned long long)g_seed);

    Result res[NVARIANTS];
    memset(res, 0, sizeof res);

    for (int vi = 0; vi < NVARIANTS; vi++) {
        if (!want[vi])
            continue;
        const Variant *v = &variants[vi];
        Result *r = &res[vi];
        fprintf(stderr, "running %s ...\n", v->name);

        double rss0 = rss_mb();
        double t0 = now_sec();
        for (int pos = 0; pos < nodes; pos++)
            for (int l = 0; l < lists; l++) {
                int size = gen_data(l, pos, buf);
                if (pos == 0)
                    heads[l] = v->init(size, buf);
                else
                    v->append(heads[l], size, buf);
            }
        r->build_s = now_sec() - t0;
        r->mem_mb = rss_mb() - rss0;

        pmu_start();
        t0 = now_sec();
        r->found = run_queries(v, heads, qs, nq, keybuf);
        r->search_s = now_sec() - t0;
        pmu_stop(&r->pmu);

        r->errors = verify(v, heads, qs, nq, keybuf);
        if (r->found != (uint64_t)nhit)
            r->errors += 1;
        r->ran = 1;

        for (int l = 0; l < lists; l++)
            v->destroy(heads[l]);
        arena_reset();
        malloc_trim(0);     /* 把释放的内存还给系统，下一个版本的内存统计才准确 */
    }

    print_results(res, visits, markdown);

    int bad = 0;
    for (int i = 0; i < NVARIANTS; i++)
        bad |= res[i].ran && res[i].errors;
    if (bad)
        printf("\nERROR: some FindNode results were wrong.\n");
    pmu_close();
    free(buf);
    free(heads);
    free(keybuf);
    free(qs);
    return bad;
}
