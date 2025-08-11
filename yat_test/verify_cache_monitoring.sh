#!/bin/bash
# filepath: \home\mrma91\Yat_CAsched\linux-6.8-main\yat_test\verify_cache_monitoring.sh

PROGRAM_PATH="./tacle_kernel_test_local"
PERF_TOOL_PATH="/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf"
OUTPUT_DIR="perf_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# 检查perf工具
if [ -f "$PERF_TOOL_PATH" ]; then
    PERF_CMD="$PERF_TOOL_PATH"
else
    PERF_CMD="perf"
fi

mkdir -p "$OUTPUT_DIR"

echo "=== 验证缓存监控数据的准确性 ==="

# 1. 先运行一个简单的测试验证perf输出格式
echo "1. 验证perf输出格式..."
simple_output="${OUTPUT_DIR}/simple_test_${TIMESTAMP}.txt"
$PERF_CMD stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses sleep 1 > "$simple_output" 2>&1

echo "=== 简单测试的原始perf输出 ==="
cat "$simple_output"
echo "================================"

# 2. 运行完整的测试并保存原始输出
echo "2. 运行完整测试..."
full_output="${OUTPUT_DIR}/full_test_raw_${TIMESTAMP}.txt"
$PERF_CMD stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,cycles,instructions,context-switches,cpu-migrations "$PROGRAM_PATH" > "$full_output" 2>&1

echo "=== 完整测试的原始perf输出 ==="
cat "$full_output"
echo "================================"

# 3. 手动解析关键数据
echo "3. 手动提取关键数据..."
echo "cache-references:" $(grep -i "cache-references" "$full_output")
echo "cache-misses:" $(grep -i "cache-misses" "$full_output")
echo "L1-dcache-loads:" $(grep -i "L1-dcache-loads" "$full_output")
echo "L1-dcache-load-misses:" $(grep -i "L1-dcache-load-misses" "$full_output")
echo "context-switches:" $(grep -i "context-switches" "$full_output")
echo "cpu-migrations:" $(grep -i "cpu-migrations" "$full_output")

# 4. 验证数据合理性
echo ""
echo "4. 数据合理性检查..."

# 提取数值进行验证
cache_refs=$(grep -i "cache-references" "$full_output" | grep -oE '[0-9,]+' | head -1 | tr -d ',')
l1_dcache_loads=$(grep -i "L1-dcache-loads" "$full_output" | grep -oE '[0-9,]+' | head -1 | tr -d ',')
context_switches=$(grep -i "context-switches" "$full_output" | grep -oE '[0-9,]+' | head -1 | tr -d ',')
cpu_migrations=$(grep -i "cpu-migrations" "$full_output" | grep -oE '[0-9,]+' | head -1 | tr -d ',')

echo "提取的数值:"
echo "- cache-references: $cache_refs"
echo "- L1-dcache-loads: $l1_dcache_loads"
echo "- context-switches: $context_switches"  
echo "- cpu-migrations: $cpu_migrations"

# 5. 数据一致性检查
if [ -n "$cache_refs" ] && [ -n "$l1_dcache_loads" ]; then
    if [ "$l1_dcache_loads" -gt "$cache_refs" ]; then
        echo "✓ L1访问次数 > 通用缓存访问次数 (合理)"
    else
        echo "⚠ L1访问次数 <= 通用缓存访问次数 (需要验证)"
    fi
fi

if [ -n "$context_switches" ] && [ -n "$cpu_migrations" ]; then
    if [ "$context_switches" -gt 0 ]; then
        migration_ratio=$((cpu_migrations * 100 / context_switches))
        echo "CPU迁移率: ${migration_ratio}%"
        if [ "$migration_ratio" -gt 40 ]; then
            echo "⚠ CPU迁移率异常高，可能的原因:"
            echo "  1. VMware虚拟化环境影响"
            echo "  2. 多进程并发执行"
            echo "  3. 缺少CPU亲和性设置"
            echo "  4. YAT_CASCHED调度器实现问题"
        fi
    fi
fi

echo ""
echo "=== 建议的验证步骤 ==="
echo "1. 检查原始perf输出格式是否正确"
echo "2. 对比单任务vs多任务的缓存性能"
echo "3. 验证YAT_CASCHED调度器的CPU亲和性实现"
echo "4. 在物理机上测试对比"