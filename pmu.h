/*
 * pmu.h - 用 perf_event_open() 读取 CPU 硬件性能计数器（PMC）
 *
 * 统计的事件：
 *   cycles        CPU 周期数                       PERF_COUNT_HW_CPU_CYCLES
 *   instructions  退休指令数                       PERF_COUNT_HW_INSTRUCTIONS
 *   l1d_loads     L1D 读访问次数（即 load 访存次数）  L1D | OP_READ | RESULT_ACCESS
 *   l1d_misses    L1D 读未命中次数                  L1D | OP_READ | RESULT_MISS
 *   llc_misses    末级 Cache 未命中次数（访问内存）    PERF_COUNT_HW_CACHE_MISSES
 * L1 Cache 命中率 = 1 - l1d_misses / l1d_loads。
 *
 * 只统计用户态（exclude_kernel = 1），只统计本线程。
 * 在 Intel 大小核（如 12 代酷睿）上，自动把进程绑定到性能核（cpu_core），
 * 并在 config 高 32 位填入 cpu_core 的 PMU 类型，保证事件计在性能核的 PMU 上。
 */
#ifndef PMU_H
#define PMU_H

#include <stdint.h>

enum {
    PMU_CYCLES,
    PMU_INSTRUCTIONS,
    PMU_L1D_LOADS,
    PMU_L1D_MISSES,
    PMU_LLC_MISSES,
    PMU_NEVENTS
};

typedef struct {
    double value[PMU_NEVENTS];  /* 计数值（若发生分时复用，已按运行时间比例换算） */
    int    valid[PMU_NEVENTS];  /* 该事件是否可用 */
    int    multiplexed;         /* 是否有事件发生了分时复用 */
} PmuResult;

/* 初始化：检测大小核并绑核、打开各事件；verbose 时打印每个事件的打开情况。
 * 返回成功打开的事件数。 */
int  pmu_init(int verbose);
void pmu_start(void);
void pmu_stop(PmuResult *r);
void pmu_close(void);

const char *pmu_event_name(int ev);
const char *pmu_event_source(int ev);   /* "generic" / "raw 0x..." / "unavailable" */

/* 环境信息 */
int         pmu_pinned_cpu(void);
int         pmu_is_hybrid(void);
const char *pmu_cpu_model(void);
const char *pmu_l1d_size(void);

#endif
