#!/bin/bash

# 批量运行 tacle-bench app 类所有 .host 程序
# 结果输出到 results_日期/summary.txt

TACLEBENCH_APP_DIR="/home/yat/Desktop/linux-6.8/tacle-bench/bench/app"
RESULT_DIR="$(dirname "$0")/results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULT_DIR"

cd "$TACLEBENCH_APP_DIR"
echo "TACLeBench app类任务批量测试"
echo "结果保存目录: $RESULT_DIR"
echo

for prog in */*.host; do
    [ -x "$prog" ] || continue
    name=$(basename "$prog")
    echo "运行: $name"
    start=$(date +%s.%N)
    "./$prog" > "$RESULT_DIR/${name}.out" 2>&1
    end=$(date +%s.%N)
    elapsed=$(echo "$end - $start" | bc)
    echo "$name: $elapsed 秒" | tee -a "$RESULT_DIR/summary.txt"
done

echo
echo "所有app类任务测试完成，详见 $RESULT_DIR/summary.txt"
