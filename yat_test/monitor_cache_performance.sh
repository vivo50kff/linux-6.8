#!/bin/bash

# 配置部分
PROGRAM_PATH="./tacle_kernel_test_local"
PERF_TOOL_PATH="/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf"
OUTPUT_DIR="perf_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}====== 修复版缓存性能监控脚本 ======${NC}"

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 检查perf工具
if [ -f "$PERF_TOOL_PATH" ]; then
    PERF_CMD="$PERF_TOOL_PATH"
else
    PERF_CMD="perf"
fi

# 修复的数值提取函数
extract_metric_value() {
    local pattern="$1"
    local file="$2"
    
    # 改进的正则表达式匹配
    local value=$(grep -i "$pattern" "$file" | head -1 | \
                 sed -E 's/[[:space:]]*([0-9,]+)[[:space:]]+.*/\1/' | \
                 tr -d ',')
    
    # 验证是否为有效数字
    if [[ "$value" =~ ^[0-9]+$ ]] && [ "$value" -gt 0 ]; then
        echo "$value"
    else
        echo ""
    fi
}

# 运行单个监控任务
run_single_monitoring() {
    local events="$1"
    local description="$2"
    local output_file="${OUTPUT_DIR}/${3}_${TIMESTAMP}.txt"
    
    echo -e "${BLUE}监控: $description${NC}"
    echo "事件: $events"
    
    # 运行perf监控
    if $PERF_CMD stat -e "$events" "$PROGRAM_PATH" > "$output_file" 2>&1; then
        echo -e "${GREEN}✓ 监控成功${NC}"
        
        # 显示原始输出的关键部分
        echo "原始数据:"
        grep -E "[0-9,]+.*[a-zA-Z]" "$output_file" | head -5
        echo ""
        
        return 0
    else
        echo -e "${RED}✗ 监控失败${NC}"
        echo "错误信息:"
        cat "$output_file"
        echo ""
        return 1
    fi
}

# 分析缓存性能
analyze_cache_performance() {
    local perf_file="$1"
    local analysis_file="${OUTPUT_DIR}/analysis_${TIMESTAMP}.txt"
    
    echo "=== 缓存性能分析报告 ===" > "$analysis_file"
    echo "分析时间: $(date)" >> "$analysis_file"
    echo "数据文件: $perf_file" >> "$analysis_file"
    echo "" >> "$analysis_file"
    
    # 提取基本指标
    local cycles=$(extract_metric_value "cycles" "$perf_file")
    local instructions=$(extract_metric_value "instructions" "$perf_file")
    local cache_refs=$(extract_metric_value "cache-references" "$perf_file")
    local cache_misses=$(extract_metric_value "cache-misses" "$perf_file")
    local context_switches=$(extract_metric_value "context-switches" "$perf_file")
    local cpu_migrations=$(extract_metric_value "cpu-migrations" "$perf_file")
    
    echo "=== 基本性能指标 ===" >> "$analysis_file"
    echo "CPU周期: ${cycles:-无数据}" >> "$analysis_file"
    echo "指令数: ${instructions:-无数据}" >> "$analysis_file"
    echo "上下文切换: ${context_switches:-无数据}" >> "$analysis_file"
    echo "CPU迁移: ${cpu_migrations:-无数据}" >> "$analysis_file"
    
    # 计算IPC
    if [ -n "$cycles" ] && [ -n "$instructions" ] && [ "$cycles" -gt 0 ]; then
        local ipc=$(echo "scale=3; $instructions / $cycles" | bc -l)
        echo "每周期指令数(IPC): $ipc" >> "$analysis_file"
    fi
    echo "" >> "$analysis_file"
    
    echo "=== 缓存性能分析 ===" >> "$analysis_file"
    echo "总缓存访问: ${cache_refs:-无数据}" >> "$analysis_file"
    echo "总缓存缺失: ${cache_misses:-无数据}" >> "$analysis_file"
    
    # 计算缓存命中率
    if [ -n "$cache_refs" ] && [ -n "$cache_misses" ] && [ "$cache_refs" -gt 0 ]; then
        local hit_rate=$(echo "scale=2; (1 - $cache_misses / $cache_refs) * 100" | bc -l)
        echo "总缓存命中率: ${hit_rate}%" >> "$analysis_file"
    fi
    echo "" >> "$analysis_file"
    
    # YAT_CASCHED调度器分析
    echo "=== YAT_CASCHED调度器影响分析 ===" >> "$analysis_file"
    if [ -n "$context_switches" ] && [ -n "$cpu_migrations" ]; then
        echo "上下文切换次数: $context_switches" >> "$analysis_file"
        echo "CPU迁移次数: $cpu_migrations" >> "$analysis_file"
        
        if [ "$context_switches" -gt 0 ]; then
            local migration_ratio=$(echo "scale=2; $cpu_migrations * 100 / $context_switches" | bc -l)
            echo "CPU迁移率: ${migration_ratio}%" >> "$analysis_file"
        fi
    fi
    echo "" >> "$analysis_file"
    
    # VMware环境说明
    echo "=== VMware环境限制说明 ===" >> "$analysis_file"
    echo "由于运行在VMware虚拟环境中，L2/L3缓存的具体性能计数器不可用。" >> "$analysis_file"
    echo "建议：" >> "$analysis_file"
    echo "1. 在物理机上运行获得完整的缓存性能数据" >> "$analysis_file"
    echo "2. 在VMware中启用性能虚拟化功能" >> "$analysis_file"
    echo "3. 使用基于软件的缓存性能推断方法" >> "$analysis_file"
    
    echo "$analysis_file"
}

# 主执行流程
echo "开始执行修复版性能监控..."

# 1. 基本性能监控
run_single_monitoring "cycles,instructions,context-switches,cpu-migrations" \
                     "基本性能指标" "basic"

# 2. 缓存性能监控  
run_single_monitoring "cache-references,cache-misses" \
                     "缓存性能指标" "cache"

# 3. 综合监控
run_single_monitoring "cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations" \
                     "综合性能分析" "comprehensive"

# 4. 分析结果
echo -e "${BLUE}正在分析性能数据...${NC}"
comprehensive_file="${OUTPUT_DIR}/comprehensive_${TIMESTAMP}.txt"

if [ -f "$comprehensive_file" ]; then
    analysis_file=$(analyze_cache_performance "$comprehensive_file")
    
    echo -e "${GREEN}=== 性能分析结果 ===${NC}"
    cat "$analysis_file"
    
    echo ""
    echo -e "${BLUE}完整分析报告: $analysis_file${NC}"
else
    echo -e "${RED}错误: 找不到综合监控文件${NC}"
fi

echo -e "${GREEN}监控完成!${NC}"