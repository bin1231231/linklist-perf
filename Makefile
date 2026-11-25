# 按题目要求使用 gcc -O2（不使用 -O3）；-march=native 针对本机 CPU 生成指令（如 AVX2）
CC      ?= gcc
CFLAGS  ?= -O2 -march=native -g -Wall -Wextra -std=gnu11
LDFLAGS ?=

LIB_SRC = link_v0_baseline.c link_v1_pool.c link_v2_compact.c link_v3_block.c arena.c
HDRS    = linklist.h variants.h arena.h pmu.h

all: linklist_perf test_linklist

linklist_perf: bench.c pmu.c $(LIB_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ bench.c pmu.c $(LIB_SRC) $(LDFLAGS)

test_linklist: test.c $(LIB_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test.c $(LIB_SRC) $(LDFLAGS)

test: test_linklist
	./test_linklist

check: linklist_perf
	./linklist_perf --check

clean:
	rm -f linklist_perf test_linklist

.PHONY: all test check clean
