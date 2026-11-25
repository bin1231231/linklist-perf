#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include "pmu.h"

#ifndef PERF_PMU_TYPE_SHIFT
#define PERF_PMU_TYPE_SHIFT 32
#endif

static int  fds[PMU_NEVENTS] = { -1, -1, -1, -1, -1 };
static char sources[PMU_NEVENTS][48];
static int  last_errno[PMU_NEVENTS];

static int  hybrid;              /* 是否大小核 */
static int  core_pmu_type = -1;  /* cpu_core 的 PMU 类型号 */
static int  pinned_cpu = -1;
static char cpu_model[128] = "unknown";
static char l1d_size[32] = "unknown";
static int  cpu_family, cpu_model_no, cpu_is_intel;

static const char *names[PMU_NEVENTS] = {
    "cycles", "instructions", "l1d_loads", "l1d_misses", "llc_misses"
};

const char *pmu_event_name(int ev) { return names[ev]; }
const char *pmu_event_source(int ev) { return sources[ev]; }
int pmu_pinned_cpu(void) { return pinned_cpu; }
int pmu_is_hybrid(void) { return hybrid; }
const char *pmu_cpu_model(void) { return cpu_model; }
const char *pmu_l1d_size(void) { return l1d_size; }

static long sys_perf_event_open(struct perf_event_attr *attr)
{
    /* pid = 0：本线程；cpu = -1：任意 CPU */
    return syscall(SYS_perf_event_open, attr, 0, -1, -1, 0);
}

static int read_first_line(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)n, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return 0;
}

static void read_cpuinfo(void)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, ':');
        if (!v)
            continue;
        v++;
        while (*v == ' ')
            v++;
        v[strcspn(v, "\n")] = 0;
        if (!strncmp(line, "model name", 10) && !strcmp(cpu_model, "unknown"))
            snprintf(cpu_model, sizeof cpu_model, "%s", v);
        else if (!strncmp(line, "vendor_id", 9))
            cpu_is_intel = strstr(v, "GenuineIntel") != NULL;
        else if (!strncmp(line, "cpu family", 10))
            cpu_family = atoi(v);
        else if (!strncmp(line, "model\t", 6))   /* "model\t\t: 154"，区别于 "model name" */
            cpu_model_no = atoi(v);
        if (cpu_family && cpu_model_no && strcmp(cpu_model, "unknown"))
            break;
    }
    fclose(f);
}

/* 选择要绑定的 CPU：大小核时选第一个性能核，否则选当前 CPU */
static void pin_cpu(void)
{
    char buf[256];
    int cpu = -1;
    if (read_first_line("/sys/bus/event_source/devices/cpu_core/type", buf, sizeof buf) == 0) {
        hybrid = 1;
        core_pmu_type = atoi(buf);
        if (read_first_line("/sys/bus/event_source/devices/cpu_core/cpus", buf, sizeof buf) == 0)
            cpu = atoi(buf);   /* 形如 "0-11"，取第一个 */
    }
    if (cpu < 0)
        cpu = sched_getcpu();
    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (sched_setaffinity(0, sizeof set, &set) == 0)
            pinned_cpu = cpu;
    }
    if (pinned_cpu >= 0) {
        for (int idx = 0; idx < 4; idx++) {
            char path[128], type[32];
            snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cache/index%d/type", pinned_cpu, idx);
            if (read_first_line(path, type, sizeof type) != 0)
                break;
            if (!strcmp(type, "Data")) {
                snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cache/index%d/size", pinned_cpu, idx);
                read_first_line(path, l1d_size, sizeof l1d_size);
                break;
            }
        }
    }
}

static int try_open(int ev, uint32_t type, uint64_t config, const char *label)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = type;
    a.config = config;
    a.disabled = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    long fd = sys_perf_event_open(&a);
    if (fd < 0) {
        last_errno[ev] = errno;
        return -1;
    }
    fds[ev] = (int)fd;
    snprintf(sources[ev], sizeof sources[ev], "%s", label);
    return 0;
}

/* 通用事件：大小核时先尝试带 cpu_core 类型的扩展写法，再退回普通写法 */
static int open_generic(int ev, uint32_t type, uint64_t config)
{
    if (hybrid && core_pmu_type > 0 &&
        try_open(ev, type, config | ((uint64_t)core_pmu_type << PERF_PMU_TYPE_SHIFT),
                 "generic (cpu_core)") == 0)
        return 0;
    return try_open(ev, type, config, "generic");
}

/* Intel 大核（Skylake ~ Golden Cove/Raptor Cove）的原始事件编码，通用 L1D 事件不可用时使用 */
static int open_raw_intel(int ev, uint64_t raw, const char *label)
{
    if (!cpu_is_intel || cpu_family != 6)
        return -1;
    uint32_t type = (hybrid && core_pmu_type > 0) ? (uint32_t)core_pmu_type : PERF_TYPE_RAW;
    return try_open(ev, type, raw, label);
}

int pmu_init(int verbose)
{
    read_cpuinfo();
    pin_cpu();

    const uint64_t l1d_read_access = PERF_COUNT_HW_CACHE_L1D |
        (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    const uint64_t l1d_read_miss = PERF_COUNT_HW_CACHE_L1D |
        (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);

    open_generic(PMU_CYCLES, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
    open_generic(PMU_INSTRUCTIONS, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
    if (open_generic(PMU_L1D_LOADS, PERF_TYPE_HW_CACHE, l1d_read_access) != 0)
        open_raw_intel(PMU_L1D_LOADS, 0x81d0, "raw 0x81d0 MEM_INST_RETIRED.ALL_LOADS");
    if (open_generic(PMU_L1D_MISSES, PERF_TYPE_HW_CACHE, l1d_read_miss) != 0)
        open_raw_intel(PMU_L1D_MISSES, 0x08d1, "raw 0x08d1 MEM_LOAD_RETIRED.L1_MISS");
    open_generic(PMU_LLC_MISSES, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);

    int ok = 0, denied = 0;
    for (int i = 0; i < PMU_NEVENTS; i++) {
        if (fds[i] >= 0)
            ok++;
        else {
            snprintf(sources[i], sizeof sources[i], "unavailable (%s)", strerror(last_errno[i]));
            if (last_errno[i] == EACCES || last_errno[i] == EPERM)
                denied = 1;
        }
    }

    if (verbose) {
        char paranoid[16] = "?";
        read_first_line("/proc/sys/kernel/perf_event_paranoid", paranoid, sizeof paranoid);
        printf("CPU            : %s (family %d, model 0x%x)\n", cpu_model, cpu_family, cpu_model_no);
        printf("Hybrid (P/E)   : %s\n", hybrid ? "yes, pinned to a P-core (cpu_core PMU)" : "no");
        printf("Pinned CPU     : %d   L1d size: %s\n", pinned_cpu, l1d_size);
        printf("perf_event_paranoid = %s\n", paranoid);
        for (int i = 0; i < PMU_NEVENTS; i++)
            printf("  %-13s: %s\n", names[i], sources[i]);
        if (denied)
            printf("Hint: permission denied. Run with sudo, or: sudo sysctl kernel.perf_event_paranoid=1\n");
        if (ok == 0 && !denied)
            printf("Hint: no hardware counters in this environment (virtual machine without vPMU?).\n");
    }
    return ok;
}

void pmu_start(void)
{
    for (int i = 0; i < PMU_NEVENTS; i++)
        if (fds[i] >= 0)
            ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
    for (int i = 0; i < PMU_NEVENTS; i++)
        if (fds[i] >= 0)
            ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
}

void pmu_stop(PmuResult *r)
{
    for (int i = 0; i < PMU_NEVENTS; i++)
        if (fds[i] >= 0)
            ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);

    memset(r, 0, sizeof *r);
    for (int i = 0; i < PMU_NEVENTS; i++) {
        uint64_t buf[3];   /* value, time_enabled, time_running */
        if (fds[i] < 0 || read(fds[i], buf, sizeof buf) != (ssize_t)sizeof buf)
            continue;
        r->valid[i] = buf[2] > 0;
        if (!r->valid[i])
            continue;
        r->value[i] = (double)buf[0];
        if (buf[2] < buf[1]) {
            r->value[i] *= (double)buf[1] / (double)buf[2];
            r->multiplexed = 1;
        }
    }
}

void pmu_close(void)
{
    for (int i = 0; i < PMU_NEVENTS; i++)
        if (fds[i] >= 0) {
            close(fds[i]);
            fds[i] = -1;
        }
}
