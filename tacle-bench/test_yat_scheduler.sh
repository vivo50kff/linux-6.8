#!/bin/bash

# YAT_CASCHED 调度器 TACLeBench 测试脚本
# 专门用于测试缓存感知调度器的性能

set -e

echo "=================================================="
echo "    YAT_CASCHED 调度器 TACLeBench 性能测试"
echo "=================================================="
echo "测试时间: $(date)"
echo "内核版本: $(uname -r)"
echo

# 配置参数
TACLEBENCH_DIR="/home/yat/Desktop/linux-6.8/tacle-bench"
RESULTS_DIR="$TACLEBENCH_DIR/yat_test_results"
TEST_DATE=$(date +%Y%m%d_%H%M%S)

# 创建结果目录
mkdir -p "$RESULTS_DIR"

# YAT调度器相关定义
SCHED_YAT_CASCHED=8  # YAT_CASCHED调度策略编号

echo "🔧 测试环境检查..."
echo "TACLeBench 路径: $TACLEBENCH_DIR"
echo "结果保存路径: $RESULTS_DIR"

# 检查必要工具
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "❌ 缺少工具: $1"
        echo "请安装: sudo apt install $2"
        exit 1
    fi
}

check_tool "perf" "linux-tools-common linux-tools-generic"
check_tool "bc" "bc"
check_tool "timeout" "coreutils"

echo "✅ 环境检查完成"
echo

# 进入TACLeBench目录
cd "$TACLEBENCH_DIR"

echo "🔨 检查并编译测试程序..."

# 检查makefile
if [ ! -f "makefile" ]; then
    echo "❌ 找不到makefile，请确认TACLeBench目录正确"
    exit 1
fi

# 编译推荐的测试程序
echo "编译kernel类程序..."
make kernel-host || echo "⚠️ 部分kernel程序编译失败（正常现象）"

echo "编译sequential类程序..."  
make sequential-host || echo "⚠️ 部分sequential程序编译失败（正常现象）"

echo "✅ 编译完成"
echo

# 查找所有可用的编译成功的程序
echo "🔍 扫描可用的测试程序..."
AVAILABLE_PROGRAMS=$(find bench -name "*.host" -type f 2>/dev/null | sort)

if [ -z "$AVAILABLE_PROGRAMS" ]; then
    echo "❌ 没有找到编译成功的程序"
    echo "请检查编译过程或手动编译特定程序"
    exit 1
fi

echo "找到 $(echo "$AVAILABLE_PROGRAMS" | wc -l) 个可用测试程序:"
echo "$AVAILABLE_PROGRAMS" | sed 's/^/  ✓ /'
echo

# 创建测试结果文件
RESULT_FILE="$RESULTS_DIR/yat_performance_$TEST_DATE.log"
echo "📊 开始性能测试，结果保存到: $RESULT_FILE"
echo

# 初始化结果文件
cat > "$RESULT_FILE" << EOF
YAT_CASCHED调度器性能测试报告
============================
测试时间: $(date)
内核版本: $(uname -r)
测试机器: $(hostname)
CPU信息: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
CPU核心数: $(nproc)

测试程序列表:
EOF

echo "$AVAILABLE_PROGRAMS" | sed 's/^/  /' >> "$RESULT_FILE"
echo "" >> "$RESULT_FILE"

# 测试函数
test_program() {
    local prog="$1"
    local name=$(basename "$prog" .host)
    local category=$(echo "$prog" | cut -d'/' -f2)
    
    echo "🧪 测试 $name ($category)"
    
    # 创建临时文件记录本次测试
    local temp_result="/tmp/yat_test_$name.tmp"
    
    echo "--- 测试程序: $name (类别: $category) ---" >> "$RESULT_FILE"
    echo "测试时间: $(date)" >> "$RESULT_FILE"
    
    # 运行5次取最佳和平均性能
    local times=()
    local success_count=0
    
    for i in {1..5}; do
        echo -n "  运行 $i/5: "
        
        # 记录开始时间
        local start_time=$(date +%s.%N)
        
        # 运行程序（超时15秒）
        if timeout 15s "$prog" >/dev/null 2>&1; then
            local end_time=$(date +%s.%N)
            local duration=$(echo "$end_time - $start_time" | bc -l)
            times+=("$duration")
            success_count=$((success_count + 1))
            printf "%.6fs ✓\n" "$duration"
        else
            echo "失败/超时 ❌"
        fi
    done
    
    if [ $success_count -gt 0 ]; then
        # 计算统计数据
        local total=0
        local min_time=${times[0]}
        local max_time=${times[0]}
        
        for time in "${times[@]}"; do
            total=$(echo "$total + $time" | bc -l)
            if (( $(echo "$time < $min_time" | bc -l) )); then
                min_time=$time
            fi
            if (( $(echo "$time > $max_time" | bc -l) )); then
                max_time=$time
            fi
        done
        
        local avg_time=$(echo "scale=6; $total / $success_count" | bc -l)
        
        # 性能分级
        local performance_grade=""
        if (( $(echo "$avg_time < 0.001" | bc -l) )); then
            performance_grade="🚀 超快 (< 1ms)"
        elif (( $(echo "$avg_time < 0.01" | bc -l) )); then
            performance_grade="⚡ 极快 (< 10ms)"
        elif (( $(echo "$avg_time < 0.1" | bc -l) )); then
            performance_grade="🔥 快速 (< 100ms)"
        elif (( $(echo "$avg_time < 1.0" | bc -l) )); then
            performance_grade="⭐ 中等 (< 1s)"
        else
            performance_grade="🐢 较慢 (> 1s)"
        fi
        
        echo "  ✅ 测试成功 ($success_count/5 次)"
        printf "  📊 平均时间: %.6fs\n" "$avg_time"
        printf "  ⏱️  最快时间: %.6fs\n" "$min_time"
        printf "  ⏳ 最慢时间: %.6fs\n" "$max_time"
        echo "  $performance_grade"
        
        # 记录到结果文件
        {
            echo "成功运行: $success_count/5 次"
            printf "平均执行时间: %.6f 秒\n" "$avg_time"
            printf "最快执行时间: %.6f 秒\n" "$min_time"
            printf "最慢执行时间: %.6f 秒\n" "$max_time"
            echo "性能等级: $performance_grade"
            echo ""
        } >> "$RESULT_FILE"
        
        # 进行详细的perf分析（仅对成功的程序）
        echo "  🔬 性能分析..."
        {
            echo "=== 详细性能分析 ==="
            perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses "$prog" 2>&1 | head -10
            echo ""
        } >> "$RESULT_FILE"
        
    else
        echo "  ❌ 所有测试都失败"
        echo "测试失败: 所有运行都失败" >> "$RESULT_FILE"
        echo "" >> "$RESULT_FILE"
    fi
    
    echo ""
}

# 运行测试
echo "📈 开始逐个测试程序..."
echo

# 优先测试推荐的程序（适合缓存测试）
PRIORITY_PROGRAMS=(
    "bench/kernel/matrix1/matrix1.host"
    "bench/kernel/sha/sha.host" 
    "bench/kernel/md5/md5.host"
    "bench/kernel/fft/fft.host"
    "bench/kernel/quicksort/quicksort.host"
    "bench/sequential/dijkstra/dijkstra.host"
    "bench/sequential/susan/susan.host"
)

echo "🎯 优先测试缓存敏感程序..."
for prog in "${PRIORITY_PROGRAMS[@]}"; do
    if [ -f "$prog" ]; then
        test_program "$prog"
    fi
done

echo "🔄 测试其他可用程序..."
for prog in $AVAILABLE_PROGRAMS; do
    # 跳过已经测试过的优先程序
    skip=false
    for priority_prog in "${PRIORITY_PROGRAMS[@]}"; do
        if [ "$prog" = "$priority_prog" ]; then
            skip=true
            break
        fi
    done
    
    if [ "$skip" = false ]; then
        test_program "$prog"
    fi
done

# 生成测试总结
echo "=================================================="
echo "              测试完成！"
echo "=================================================="
echo "📊 详细结果已保存到: $RESULT_FILE"
echo "🔗 查看结果: cat $RESULT_FILE"
echo

# 生成简要总结
{
    echo ""
    echo "=== 测试总结 ==="
    echo "测试完成时间: $(date)"
    echo "总测试程序数: $(echo "$AVAILABLE_PROGRAMS" | wc -l)"
    echo "测试结果文件: $RESULT_FILE"
} >> "$RESULT_FILE"

echo "✅ YAT_CASCHED调度器测试完成！"
echo "👀 快速查看结果:"
echo "   tail -20 $RESULT_FILE"
