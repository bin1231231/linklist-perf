#!/bin/bash
# 编译、运行正确性测试，并在几组规模下测试四个版本的搜索性能。
# 结果保存在 results/ 目录，results/summary.md 汇总全部表格。
set -e
cd "$(dirname "$0")"

make
./test_linklist

# Ubuntu 默认 perf_event_paranoid=4，普通用户无法打开计数器；调到 2（只允许统计用户态）
paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 2)
if [ "$(id -u)" != 0 ] && [ "$paranoid" -gt 2 ]; then
    echo "perf_event_paranoid=$paranoid, setting it to 2 (needs sudo)"
    sudo sysctl -w kernel.perf_event_paranoid=2
fi

mkdir -p results
./linklist_perf --check | tee results/check.txt || {
    echo
    echo "Hardware counters are not readable here; timings will still be measured."
}

out=results/summary.md
{
    echo "# Results"
    echo
    echo '```'
    sed -n '1,20p' results/check.txt
    echo '```'
} > "$out"

run() {
    name=$1; shift
    echo
    echo "### $name"
    ./linklist_perf "$@" --markdown | tee "results/$name.md"
    {
        echo
        echo "## $name"
        echo
        sed -n '/== Config/,$p' "results/$name.md"
    } >> "$out"
}

run wide_random --preset wide
run mid_random  --preset mid
run long_random --preset long
run mid_fixed64  --preset mid  --fixed-size 64
run long_fixed64 --preset long --fixed-size 64

echo
echo "All results written to $out"
