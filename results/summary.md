# Results

```
CPU            : 12th Gen Intel(R) Core(TM) i7-12700H (family 6, model 0x9a)
Hybrid (P/E)   : yes, pinned to a P-core (cpu_core PMU)
Pinned CPU     : 0   L1d size: 48K
perf_event_paranoid = 2
  cycles       : generic (cpu_core)
  instructions : generic (cpu_core)
  l1d_loads    : generic (cpu_core)
  l1d_misses   : generic (cpu_core)
  llc_misses   : generic (cpu_core)

Self test (10M loop iterations):
  cycles        = 10031046
  instructions  = 60000209
  l1d_loads     = 10000043
  l1d_misses    = 6
  llc_misses    = 6

Result: hardware counters OK
```

## wide_random

== Config
preset=wide  lists=1000000  nodes/list=8  total nodes=8000000
data size=32~512 bytes (random)  queries=500000 (hit 375109, miss 124891)  node visits=2687805  seed=20251124

[1] Search time

| variant      |  build(s) |   mem(MB) |  search(ms) |   ns/node |  speedup |  errors |
|---|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      1.35 |      2568 |        90.9 |     33.82 |     1.0x |       0 |
| V1 pool      |      1.14 |      2530 |        50.5 |     18.78 |     1.8x |       0 |
| V2 compact   |      1.15 |      2530 |        58.3 |     21.69 |     1.6x |       0 |
| V3 block     |      1.19 |      2530 |        53.1 |     19.77 |     1.7x |       0 |

[2] Hardware counters, totals over all queries

| variant      |         cycles |   instructions |    IPC |      L1D loads |   L1D misses |  L1 hit% |   LLC misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      320613804 |       53125329 |   0.17 |       18955902 |     10478349 |    44.72 |     17316111 |
| V1 pool      |      184458361 |       53231738 |   0.29 |       19005857 |      6653560 |    64.99 |     12001554 |
| V2 compact   |      213971257 |       72122186 |   0.34 |       20971005 |      6295027 |    69.98 |     12454687 |
| V3 block     |      204314938 |       89750723 |   0.44 |       28097710 |      5079528 |    81.92 |     11592021 |

[3] Per node visited

| variant      |   cycles |    instr |  L1D loads |   L1D misses |   LLC misses |
|---|---:|---:|---:|---:|---:|
| V0 baseline  |   119.28 |    19.77 |       7.05 |         3.90 |         6.44 |
| V1 pool      |    68.63 |    19.80 |       7.07 |         2.48 |         4.47 |
| V2 compact   |    79.61 |    26.83 |       7.80 |         2.34 |         4.63 |
| V3 block     |    76.02 |    33.39 |      10.45 |         1.89 |         4.31 |

## mid_random

== Config
preset=mid  lists=20000  nodes/list=200  total nodes=4000000
data size=32~512 bytes (random)  queries=100000 (hit 74808, miss 25192)  node visits=12547113  seed=20251124

[1] Search time

| variant      |  build(s) |   mem(MB) |  search(ms) |   ns/node |  speedup |  errors |
|---|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      0.61 |      1280 |      1487.7 |    118.57 |     1.0x |       0 |
| V1 pool      |      0.62 |      1208 |       103.3 |      8.23 |    14.4x |       0 |
| V2 compact   |      0.59 |      1208 |        97.7 |      7.78 |    15.2x |       0 |
| V3 block     |      0.62 |      1252 |        38.2 |      3.05 |    38.9x |       0 |

[2] Hardware counters, totals over all queries

| variant      |         cycles |   instructions |    IPC |      L1D loads |   L1D misses |  L1 hit% |   LLC misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| V0 baseline  |     5403008783 |       71205122 |   0.01 |       27979487 |     29703572 |    -6.16 |     45054006 |
| V1 pool      |      465458739 |       71224940 |   0.15 |       27988094 |      2335776 |    91.65 |     13230324 |
| V2 compact   |      446870002 |       74451439 |   0.17 |       28205613 |      2116616 |    92.50 |     12627709 |
| V3 block     |      151867087 |       52492005 |   0.35 |       21021197 |      2396672 |    88.60 |      5567435 |

[3] Per node visited

| variant      |   cycles |    instr |  L1D loads |   L1D misses |   LLC misses |
|---|---:|---:|---:|---:|---:|
| V0 baseline  |   430.62 |     5.68 |       2.23 |         2.37 |         3.59 |
| V1 pool      |    37.10 |     5.68 |       2.23 |         0.19 |         1.05 |
| V2 compact   |    35.62 |     5.93 |       2.25 |         0.17 |         1.01 |
| V3 block     |    12.10 |     4.18 |       1.68 |         0.19 |         0.44 |

## long_random

== Config
preset=long  lists=200  nodes/list=10000  total nodes=2000000
data size=32~512 bytes (random)  queries=2000 (hit 1522, miss 478)  node visits=12018056  seed=20251124

[1] Search time

| variant      |  build(s) |   mem(MB) |  search(ms) |   ns/node |  speedup |  errors |
|---|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      0.31 |       640 |      1233.1 |    102.60 |     1.0x |       0 |
| V1 pool      |      0.28 |       587 |        34.9 |      2.90 |    35.4x |       0 |
| V2 compact   |      0.28 |       587 |        32.3 |      2.69 |    38.1x |       0 |
| V3 block     |      0.28 |       604 |         5.3 |      0.44 |   233.8x |       0 |

[2] Hardware counters, totals over all queries

| variant      |         cycles |   instructions |    IPC |      L1D loads |   L1D misses |  L1 hit% |   LLC misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| V0 baseline  |     4328322517 |       60797766 |   0.01 |       24265819 |     27092200 |   -11.65 |     36986668 |
| V1 pool      |      152208488 |       60796964 |   0.40 |       24264840 |       257460 |    98.94 |      4310633 |
| V2 compact   |      145872949 |       60326813 |   0.41 |       24099064 |       177034 |    99.27 |      4070467 |
| V3 block     |       24116224 |       28628205 |   1.19 |       12668246 |       583365 |    95.40 |       904651 |

[3] Per node visited

| variant      |   cycles |    instr |  L1D loads |   L1D misses |   LLC misses |
|---|---:|---:|---:|---:|---:|
| V0 baseline  |   360.15 |     5.06 |       2.02 |         2.25 |         3.08 |
| V1 pool      |    12.66 |     5.06 |       2.02 |         0.02 |         0.36 |
| V2 compact   |    12.14 |     5.02 |       2.01 |         0.01 |         0.34 |
| V3 block     |     2.01 |     2.38 |       1.05 |         0.05 |         0.08 |

## mid_fixed64

== Config
preset=mid  lists=20000  nodes/list=200  total nodes=4000000
data size=64 bytes (fixed)  queries=100000 (hit 74808, miss 25192)  node visits=12547113  seed=20251124

[1] Search time

| variant      |  build(s) |   mem(MB) |  search(ms) |   ns/node |  speedup |  errors |
|---|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      0.22 |       488 |      1450.7 |    115.62 |     1.0x |       0 |
| V1 pool      |      0.20 |       402 |       263.1 |     20.97 |     5.5x |       0 |
| V2 compact   |      0.19 |       402 |        95.3 |      7.60 |    15.2x |       0 |
| V3 block     |      0.25 |       446 |        34.3 |      2.73 |    42.3x |       0 |

[2] Hardware counters, totals over all queries

| variant      |         cycles |   instructions |    IPC |      L1D loads |   L1D misses |  L1 hit% |   LLC misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| V0 baseline  |     5202910978 |      341899121 |   0.07 |      113950763 |     36556244 |    67.92 |     26062722 |
| V1 pool      |      922581376 |      341897931 |   0.37 |      113949573 |     35311476 |    69.01 |     72243327 |
| V2 compact   |      436366009 |       71307053 |   0.16 |       27043270 |      1683479 |    93.77 |     11813520 |
| V3 block     |      138529829 |       49347618 |   0.36 |       19858853 |      2035938 |    89.75 |      4704631 |

[3] Per node visited

| variant      |   cycles |    instr |  L1D loads |   L1D misses |   LLC misses |
|---|---:|---:|---:|---:|---:|
| V0 baseline  |   414.67 |    27.25 |       9.08 |         2.91 |         2.08 |
| V1 pool      |    73.53 |    27.25 |       9.08 |         2.81 |         5.76 |
| V2 compact   |    34.78 |     5.68 |       2.16 |         0.13 |         0.94 |
| V3 block     |    11.04 |     3.93 |       1.58 |         0.16 |         0.37 |

## long_fixed64

== Config
preset=long  lists=200  nodes/list=10000  total nodes=2000000
data size=64 bytes (fixed)  queries=2000 (hit 1522, miss 478)  node visits=12018056  seed=20251124

[1] Search time

| variant      |  build(s) |   mem(MB) |  search(ms) |   ns/node |  speedup |  errors |
|---|---:|---:|---:|---:|---:|---:|
| V0 baseline  |      0.11 |       244 |       755.2 |     62.84 |     1.0x |       0 |
| V1 pool      |      0.07 |       184 |       166.5 |     13.86 |     4.5x |       0 |
| V2 compact   |      0.08 |       184 |        31.8 |      2.65 |    23.8x |       0 |
| V3 block     |      0.08 |       200 |         5.0 |      0.42 |   150.5x |       0 |

[2] Hardware counters, totals over all queries

| variant      |         cycles |   instructions |    IPC |      L1D loads |   L1D misses |  L1 hit% |   LLC misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| V0 baseline  |     3124499360 |      324551171 |   0.10 |      108183839 |     19014243 |    82.42 |     42475728 |
| V1 pool      |      603100595 |      324550581 |   0.54 |      108183249 |     23572542 |    78.21 |     49883593 |
| V2 compact   |      145553587 |       60262699 |   0.41 |       24075377 |       161151 |    99.33 |      3807666 |
| V3 block     |       22928484 |       28564092 |   1.25 |       12644560 |       607344 |    95.20 |       899403 |

[3] Per node visited

| variant      |   cycles |    instr |  L1D loads |   L1D misses |   LLC misses |
|---|---:|---:|---:|---:|---:|
| V0 baseline  |   259.98 |    27.01 |       9.00 |         1.58 |         3.53 |
| V1 pool      |    50.18 |    27.01 |       9.00 |         1.96 |         4.15 |
| V2 compact   |    12.11 |     5.01 |       2.00 |         0.01 |         0.32 |
| V3 block     |     1.91 |     2.38 |       1.05 |         0.05 |         0.07 |