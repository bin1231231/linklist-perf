# 双向链表搜索的性能优化与硬件计数器统计

PG 内核课程作业 1-2 开发与分析题：用 C 实现双向链表的创建、追加和搜索，在不使用哈希的前提下优化搜索性能，并用 `perf_event_open()` 统计链表搜索过程的指令数、周期数、访存次数和 L1 Cache 命中率。

## 1. 题目要求

- 环境：Linux，gcc，编译优化级别 -O2（不使用 -O3）。
- 数据大小 32~512 字节；单条链表可达上万个节点；链表条数可达数百万到上千万条。
- 必须实现：
  - `LinkData *InitLink(int size, char *mem)`：创建链表头，头节点的 prev、next 指向自己；
  - `void AppendNode(LinkData *firstNode, int size, char *mem)`：追加到尾部，`firstNode->prev` 始终指向最后一个节点；
  - `LinkData *FindNode(LinkData *firstNode, int size, char *mem)`：从头遍历，返回目标节点地址。
- 可以重构结构体（保留 dataSize 和 data），可以针对本机 CPU 优化，可以用空间换时间；不能用哈希加速搜索。

## 2. 文件说明

| 文件 | 内容 |
|---|---|
| `linklist.h` | 题目要求的 `LinkData` 结构体和三个接口；节点“键”的定义 |
| `link_v3_block.c` | 正式实现（V3）：`InitLink` / `AppendNode` / `FindNode` |
| `link_v0_baseline.c` | V0：题目的直接写法，作为对比基准 |
| `link_v1_pool.c` | V1：只改内存布局（每条链表一个节点池） |
| `link_v2_compact.c` | V2：节点池 + 重构节点，节点内存放 8 字节键 |
| `variants.h` | V0~V2 的接口声明 |
| `arena.c/.h` | 数据区的顺序分配器 |
| `pmu.c/.h` | 用 `perf_event_open()` 读取硬件性能计数器 |
| `bench.c` | 性能测试程序 `linklist_perf` |
| `test.c` | 正确性测试程序 `test_linklist` |
| `run.sh` | 一键编译、测试并在多组规模下运行，结果写入 `results/` |

## 3. 编译与运行

需要 Linux（物理机，或支持虚拟 PMU 的虚拟机）、gcc 和 make。

```bash
make                      # gcc -O2 -march=native
make test                 # 正确性测试
./linklist_perf --check   # 检查能否读取硬件计数器
./run.sh                  # 完整测试，结果汇总在 results/summary.md
```

单独运行时可以指定规模：

```bash
./linklist_perf --preset mid                  # 2 万条链表 x 每条 200 个节点
./linklist_perf --lists 100000 --nodes 50 --queries 200000 --fixed-size 64
./linklist_perf --help
```

注意事项：

- Ubuntu 默认 `kernel.perf_event_paranoid=4`，普通用户无法打开计数器。`run.sh` 会自动执行 `sudo sysctl -w kernel.perf_event_paranoid=2`，只允许统计用户态，程序本身也只统计用户态。
- 计数器由 CPU 硬件提供。没有虚拟 PMU 的虚拟机、容器和 macOS 都读不到；`--check` 会给出提示，此时程序仍会输出耗时。
- Intel 12 代及以后的大小核处理器有性能核（P-core）和能效核（E-core）两种 PMU，L1 Cache 大小也不同。程序检测到 `cpu_core` PMU 时，会把自己绑定到第一个性能核，并把事件指定在性能核的 PMU 上。

## 4. 实现

### 4.1 基准版本 V0 的瓶颈

V0 按题目直接实现：每个节点、每份数据各 `malloc` 一次；`FindNode` 沿 `next` 逐个节点比较，先比 `dataSize`，相等时再 `memcmp`。

链表条数很多时，各条链表的节点交替追加，同一条链表的相邻节点在内存中相隔很远。遍历时每访问一个节点几乎都是一次 Cache 未命中，而且下一个节点的地址要等当前节点读回来才知道（指针追逐），CPU 无法并行发出访存请求，也无法预取。搜索时间主要花在等待内存上，而不是比较本身。

### 4.2 优化思路

在不使用哈希的前提下，优化的方向是减少每访问一个节点要读的 Cache 行数，并让访存地址连续，以便硬件预取：

| 版本 | 改动 | 解决的问题 |
|---|---|---|
| V1 pool | 结构体和 `FindNode` 不变。每条链表有自己的节点池，节点按追加顺序连续存放（块容量 4、8 … 256 递增）；数据放到公共数据区 | 节点在内存中连续，遍历变为顺序访存，硬件预取器生效 |
| V2 compact | 去掉 `type`，节点仍为 32 字节；`dataSize` 后面紧跟数据的前 4 字节，二者组成 8 字节的“键”，一次比较即可筛选 | 数据长度相同时（如定长记录），V0/V1 每个节点都要经 `data` 指针读数据；V2 只有键相同才读数据 |
| V3 block | `LinkData` 保持题目原样。节点按块存放，每块在节点数组之前另存一个键数组；`FindNode` 按块从头遍历，只扫描键数组，用 AVX2 一次比较 4 个键，键相同才比较完整数据；扫描一块时预取下一块 | 遍历时每个节点只读 8 字节，一条 Cache 行覆盖 8 个节点；比较由 SIMD 完成，指令数大幅减少 |

键的定义：低 32 位为数据长度，高 32 位为数据的前 4 个字节（不足补 0）。键是数据本身的拷贝，不是哈希值：键不同则数据一定不同，键相同时仍要比较完整数据，所以结果与逐字节比较完全一致。

V3 的块内存布局（64 字节对齐）：

```
[块头 32B: next, tail, nodes, count, cap][keys[cap]: 8B x cap][nodes[cap]: 32B x cap]
```

- 头节点是第一块的 `nodes[0]`，第一块容量固定为 8，由头节点地址可以直接算出第一块的地址，因此接口仍然只需要传 `firstNode`；
- 第一块块头记录当前最后一块，`AppendNode` 直接在最后一块追加，不需要遍历；
- 节点的 `prev`、`next` 照常维护：沿 `next` 走一圈得到的顺序就是追加顺序，`firstNode->prev` 始终是最后一个节点；
- `FindNode` 按块的顺序、块内按下标顺序扫描，返回的是链表中第一个匹配的节点，与逐个遍历的结果相同。

### 4.3 正确性

`test.c` 对四个版本建立相同内容的链表，检查链表结构（`prev`/`next` 对应、头节点 `prev` 指向尾节点、遍历顺序与追加顺序一致），并比较 `FindNode` 返回节点的位置。测试数据包括：命中头节点、中间节点、尾节点；数据重复时返回第一个；前 4 字节相同而后面不同；前缀相同而长度不同；1~7 字节的短数据、32 字节、512 字节和 1000 字节以上的数据；以及 60 轮随机数据（字符种类少，重复和公共前缀很多）。四个版本的结果必须与逐个比较的结果完全一致。

## 5. 用 perf_event_open 统计指标

`pmu.c` 为每个事件调用一次 `perf_event_open()`（`pid = 0, cpu = -1`，即统计本线程在任意 CPU 上的执行），设置 `disabled = 1`、`exclude_kernel = 1`、`exclude_hv = 1`。测量时先 `PERF_EVENT_IOC_RESET`、`PERF_EVENT_IOC_ENABLE`，执行全部查询后 `PERF_EVENT_IOC_DISABLE`，再用 `read()` 读出计数值。

| 指标 | 事件 |
|---|---|
| 周期数 | `PERF_TYPE_HARDWARE / PERF_COUNT_HW_CPU_CYCLES` |
| 指令数 | `PERF_TYPE_HARDWARE / PERF_COUNT_HW_INSTRUCTIONS` |
| 访存次数 | L1D 读访问次数：`PERF_TYPE_HW_CACHE`，`L1D \| OP_READ \| RESULT_ACCESS`，即执行的 load 次数 |
| L1 未命中次数 | `PERF_TYPE_HW_CACHE`，`L1D \| OP_READ \| RESULT_MISS` |
| L1 命中率 | 1 - L1D 读未命中 / L1D 读访问 |
| 末级 Cache 未命中 | `PERF_TYPE_HARDWARE / PERF_COUNT_HW_CACHE_MISSES`，即真正访问内存的次数 |

实现细节：

- `read_format` 包含 `TOTAL_TIME_ENABLED` 和 `TOTAL_TIME_RUNNING`。若事件数超过硬件计数器数量、内核分时复用计数器，按 `enabled / running` 换算，并在输出中注明。
- 大小核处理器上，事件的 `config` 高 32 位填入 `cpu_core` 的 PMU 类型号（`PERF_PMU_TYPE_SHIFT`），保证事件计在性能核上；同时用 `sched_setaffinity` 把进程绑定到性能核。
- 若内核不支持通用 L1D 事件（例如部分虚拟机），在 Intel 处理器上改用原始事件：`MEM_INST_RETIRED.ALL_LOADS`（0x81d0）和 `MEM_LOAD_RETIRED.L1_MISS`（0x08d1）。`--check` 会列出每个事件实际使用的编码。

## 6. 测试方法

- 建表：先把第 0 个节点追加到每条链表，再追加第 1 个……这样多条链表交替增长，接近大量链表同时写入的实际情况。数据由（链表号，位置）确定性生成，四个版本的链表内容完全相同。
- 查询：随机选一条链表，75% 查询链表中已有的数据（位置随机），25% 查询不存在的数据（需要遍历整条链表）。查询参数预先生成，四个版本使用同一组查询。
- 计数范围：整个查询循环。“每节点”指标 = 总计数 / 查询过程中访问的节点总数（命中时访问到目标位置为止，未命中时访问整条链表）。
- 规模（`run.sh`）：

| 名称 | 链表条数 | 每条节点数 | 节点总数 | 查询次数 | 数据长度 |
|---|---:|---:|---:|---:|---|
| wide_random | 1,000,000 | 8 | 800 万 | 500,000 | 32~512 随机 |
| mid_random | 20,000 | 200 | 400 万 | 100,000 | 32~512 随机 |
| long_random | 200 | 10,000 | 200 万 | 2,000 | 32~512 随机 |
| mid_fixed64 | 20,000 | 200 | 400 万 | 100,000 | 固定 64 |
| long_fixed64 | 200 | 10,000 | 200 万 | 2,000 | 固定 64 |

## 7. 测试结果

测试机上的结果整理后补充于此，原始输出保存在 `results/` 目录。
