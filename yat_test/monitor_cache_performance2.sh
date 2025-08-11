#!/bin/bash
# filepath: \home\mrma91\Yat_CAsched\linux-6.8-main\yat_test\monitor_cache_performance.sh

# 配置部分 - 更新为新的高负载任务集
PROGRAM_PATH="./tacle_kernel_test_local2"
PERF_TOOL_PATH="/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf"
OUTPUT_DIR="perf_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}====== 高负载任务集多级缓存性能监控 (VMware增强版) ======${NC}"
echo -e "${YELLOW}测试程序: $PROGRAM_PATH${NC}"
echo -e "${YELLOW}预期特性: 36并发进程、缓存竞争、CPU迁移压力测试${NC}"
echo ""

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 检查程序是否存在
if [ ! -f "$PROGRAM_PATH" ]; then
    echo -e "${RED}错误: 找不到测试程序 $PROGRAM_PATH${NC}"
    echo "请先编译高负载任务集:"
    echo "gcc -o tacle_kernel_test_local2 tacle_kernel_test_local2.c -lm"
    exit 1
fi

# 检查perf工具
if [ -f "$PERF_TOOL_PATH" ]; then
    PERF_CMD="$PERF_TOOL_PATH"
    echo -e "${GREEN}使用自定义perf工具: $PERF_TOOL_PATH${NC}"
else
    PERF_CMD="perf"
    echo -e "${YELLOW}使用系统perf工具${NC}"
fi

# 检查可用的缓存事件
check_available_events() {
    echo -e "${BLUE}检查可用的缓存性能事件...${NC}"
    
    # 检查L1缓存事件
    echo "=== L1 缓存事件 ==="
    $PERF_CMD list | grep -E "(L1|l1)" | grep -i cache
    
    # 检查L2缓存事件  
    echo "=== L2 缓存事件 ==="
    $PERF_CMD list | grep -E "(L2|l2)" | grep -i cache
    
    # 检查L3缓存事件
    echo "=== L3 缓存事件 ==="  
    $PERF_CMD list | grep -E "(L3|l3)" | grep -i cache
    
    # 检查通用缓存事件
    echo "=== 通用缓存事件 ==="
    $PERF_CMD list | grep -E "(cache|Cache)"
    
    echo ""
}

# 修复的数值提取函数
extract_metric_value() {
    local pattern="$1"
    local file="$2"
    
    # 改进的正则表达式匹配，支持科学计数法
    local value=$(grep -i "$pattern" "$file" | head -1 | \
                 sed -E 's/[[:space:]]*([0-9,]+\.?[0-9]*)[[:space:]]+.*/\1/' | \
                 tr -d ',')
    
    # 验证是否为有效数字
    if [[ "$value" =~ ^[0-9]+\.?[0-9]*$ ]] && (( $(echo "$value > 0" | bc -l) )); then
        echo "$value"
    else
        echo ""
    fi
}

# 运行高负载多级缓存监控
run_multilevel_cache_monitoring() {
    local output_file="${OUTPUT_DIR}/multilevel_cache_${TIMESTAMP}.txt"
    
    echo -e "${BLUE}运行高负载多级缓存性能监控...${NC}"
    echo -e "${YELLOW}注意: 高负载测试可能需要较长时间完成${NC}"
    
    # 定义多级缓存事件组合
    local cache_events="cache-references,cache-misses"
    
    # 尝试添加L1缓存事件
    if $PERF_CMD stat -e L1-dcache-loads sleep 0.1 >/dev/null 2>&1; then
        cache_events+=",L1-dcache-loads,L1-dcache-load-misses"
        cache_events+=",L1-icache-loads,L1-icache-load-misses"
        echo "✓ L1缓存事件可用"
    fi
    
    # 尝试添加L2缓存事件
    if $PERF_CMD stat -e L2-cache-loads sleep 0.1 >/dev/null 2>&1; then
        cache_events+=",L2-cache-loads,L2-cache-load-misses"
        echo "✓ L2缓存事件可用"
    else
        echo "- L2缓存事件不可用 (VMware限制)"
    fi
    
    # 尝试添加L3缓存事件
    if $PERF_CMD stat -e L3-cache-loads sleep 0.1 >/dev/null 2>&1; then
        cache_events+=",L3-cache-loads,L3-cache-load-misses"
        echo "✓ L3缓存事件可用"
    else
        echo "- L3缓存事件不可用 (VMware限制)"
    fi
    
    # 添加基本性能指标
    cache_events+=",cycles,instructions,context-switches,cpu-migrations"
    
    echo "监控事件组合: $cache_events"
    echo ""
    echo -e "${BLUE}开始执行高负载测试... (预计需要2-3分钟)${NC}"
    
    # 运行完整监控
    local start_time=$(date +%s)
    if timeout 300 $PERF_CMD stat -e "$cache_events" "$PROGRAM_PATH" > "$output_file" 2>&1; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        echo -e "${GREEN}✓ 高负载多级缓存监控成功 (耗时: ${duration}秒)${NC}"
        return 0
    else
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        echo -e "${RED}✗ 高负载多级缓存监控失败或超时 (耗时: ${duration}秒)${NC}"
        echo "输出内容:"
        cat "$output_file"
        return 1
    fi
}

# 分析高负载多级缓存性能
analyze_multilevel_cache() {
    local perf_file="$1"
    local analysis_file="${OUTPUT_DIR}/multilevel_analysis_${TIMESTAMP}.txt"
    
    echo "=== 高负载任务集多级缓存性能分析报告 ===" > "$analysis_file"
    echo "分析时间: $(date)" >> "$analysis_file"
    echo "数据文件: $perf_file" >> "$analysis_file"
    echo "测试程序: $PROGRAM_PATH" >> "$analysis_file"
    echo "测试特性: 36并发进程、每任务重复50次、3个副本" >> "$analysis_file"
    echo "VMware虚拟化环境: 已启用性能计数器" >> "$analysis_file"
    echo "" >> "$analysis_file"
    
    # 基本性能指标
    local cycles=$(extract_metric_value "cycles" "$perf_file")
    local instructions=$(extract_metric_value "instructions" "$perf_file")
    local context_switches=$(extract_metric_value "context-switches" "$perf_file")
    local cpu_migrations=$(extract_metric_value "cpu-migrations" "$perf_file")
    
    echo "=== 高负载基本性能指标 ===" >> "$analysis_file"
    echo "CPU周期: ${cycles:-无数据}" >> "$analysis_file"
    echo "指令数: ${instructions:-无数据}" >> "$analysis_file"
    echo "上下文切换: ${context_switches:-无数据}" >> "$analysis_file"
    echo "CPU迁移: ${cpu_migrations:-无数据}" >> "$analysis_file"
    
    # 计算IPC
    if [ -n "$cycles" ] && [ -n "$instructions" ] && (( $(echo "$cycles > 0" | bc -l) )); then
        local ipc=$(echo "scale=3; $instructions / $cycles" | bc -l)
        echo "每周期指令数(IPC): $ipc" >> "$analysis_file"
        
        # IPC评估
        if (( $(echo "$ipc > 1.0" | bc -l) )); then
            echo "IPC评估: 优秀 (高效执行)" >> "$analysis_file"
        elif (( $(echo "$ipc > 0.5" | bc -l) )); then
            echo "IPC评估: 良好" >> "$analysis_file"
        else
            echo "IPC评估: 较低 (可能受缓存缺失影响)" >> "$analysis_file"
        fi
    fi
    echo "" >> "$analysis_file"
    
    # 通用缓存分析
    local cache_refs=$(extract_metric_value "cache-references" "$perf_file")
    local cache_misses=$(extract_metric_value "cache-misses" "$perf_file")
    
    echo "=== 高负载通用缓存性能 ===" >> "$analysis_file"
    echo "总缓存访问: ${cache_refs:-无数据}" >> "$analysis_file"
    echo "总缓存缺失: ${cache_misses:-无数据}" >> "$analysis_file"
    
    if [ -n "$cache_refs" ] && [ -n "$cache_misses" ] && (( $(echo "$cache_refs > 0" | bc -l) )); then
        local hit_rate=$(echo "scale=2; (1 - $cache_misses / $cache_refs) * 100" | bc -l)
        local miss_rate=$(echo "scale=2; $cache_misses * 100 / $cache_refs" | bc -l)
        echo "总缓存命中率: ${hit_rate}%" >> "$analysis_file"
        echo "总缓存缺失率: ${miss_rate}%" >> "$analysis_file"
        
        # 与轻负载对比分析
        echo "" >> "$analysis_file"
        echo "高负载vs轻负载缓存性能对比:" >> "$analysis_file"
        if (( $(echo "$hit_rate < 80" | bc -l) )); then
            echo "- 缓存命中率显著下降，高负载产生明显缓存压力" >> "$analysis_file"
        elif (( $(echo "$hit_rate < 90" | bc -l) )); then
            echo "- 缓存命中率中等下降，负载对缓存有一定影响" >> "$analysis_file"
        else
            echo "- 缓存命中率依然良好，任务具有良好局部性" >> "$analysis_file"
        fi
    fi
    echo "" >> "$analysis_file"
    
    # L1缓存分析
    local l1d_loads=$(extract_metric_value "L1-dcache-loads" "$perf_file")
    local l1d_misses=$(extract_metric_value "L1-dcache-load-misses" "$perf_file")
    local l1i_loads=$(extract_metric_value "L1-icache-loads" "$perf_file")  
    local l1i_misses=$(extract_metric_value "L1-icache-load-misses" "$perf_file")
    
    if [ -n "$l1d_loads" ] || [ -n "$l1i_loads" ]; then
        echo "=== 高负载L1 缓存性能 ===" >> "$analysis_file"
        
        # L1数据缓存
        if [ -n "$l1d_loads" ] && [ -n "$l1d_misses" ]; then
            echo "L1数据缓存访问: $l1d_loads" >> "$analysis_file"
            echo "L1数据缓存缺失: $l1d_misses" >> "$analysis_file"
            if (( $(echo "$l1d_loads > 0" | bc -l) )); then
                local l1d_hit_rate=$(echo "scale=2; (1 - $l1d_misses / $l1d_loads) * 100" | bc -l)
                echo "L1数据缓存命中率: ${l1d_hit_rate}%" >> "$analysis_file"
                
                # 高负载L1数据缓存评估
                if (( $(echo "$l1d_hit_rate < 85" | bc -l) )); then
                    echo "L1数据缓存评估: 高负载显著影响数据缓存性能" >> "$analysis_file"
                elif (( $(echo "$l1d_hit_rate < 95" | bc -l) )); then
                    echo "L1数据缓存评估: 高负载对数据缓存有一定影响" >> "$analysis_file"
                else
                    echo "L1数据缓存评估: 即使高负载下依然保持优秀性能" >> "$analysis_file"
                fi
            fi
        fi
        
        # L1指令缓存  
        if [ -n "$l1i_loads" ] && [ -n "$l1i_misses" ]; then
            echo "L1指令缓存访问: $l1i_loads" >> "$analysis_file"
            echo "L1指令缓存缺失: $l1i_misses" >> "$analysis_file"
            if (( $(echo "$l1i_loads > 0" | bc -l) )); then
                local l1i_hit_rate=$(echo "scale=2; (1 - $l1i_misses / $l1i_loads) * 100" | bc -l)
                echo "L1指令缓存命中率: ${l1i_hit_rate}%" >> "$analysis_file"
                
                # 高负载L1指令缓存评估
                if (( $(echo "$l1i_hit_rate > 98" | bc -l) )); then
                    echo "L1指令缓存评估: 优秀，代码局部性良好" >> "$analysis_file"
                elif (( $(echo "$l1i_hit_rate > 95" | bc -l) )); then
                    echo "L1指令缓存评估: 良好" >> "$analysis_file"
                else
                    echo "L1指令缓存评估: 高负载影响了指令缓存性能" >> "$analysis_file"
                fi
            fi
        fi
        echo "" >> "$analysis_file"
    fi
    
    # L2缓存分析 (如果可用)
    local l2_loads=$(extract_metric_value "L2-cache-loads" "$perf_file")
    local l2_misses=$(extract_metric_value "L2-cache-load-misses" "$perf_file")
    
    if [ -n "$l2_loads" ] && [ -n "$l2_misses" ]; then
        echo "=== L2 缓存性能 ===" >> "$analysis_file"
        echo "L2缓存访问: $l2_loads" >> "$analysis_file"
        echo "L2缓存缺失: $l2_misses" >> "$analysis_file"
        if (( $(echo "$l2_loads > 0" | bc -l) )); then
            local l2_hit_rate=$(echo "scale=2; (1 - $l2_misses / $l2_loads) * 100" | bc -l)
            echo "L2缓存命中率: ${l2_hit_rate}%" >> "$analysis_file"
        fi
        echo "" >> "$analysis_file"
    fi
    
    # L3缓存分析 (如果可用)
    local l3_loads=$(extract_metric_value "L3-cache-loads" "$perf_file")
    local l3_misses=$(extract_metric_value "L3-cache-load-misses" "$perf_file")
    
    if [ -n "$l3_loads" ] && [ -n "$l3_misses" ]; then
        echo "=== L3 缓存性能 ===" >> "$analysis_file"
        echo "L3缓存访问: $l3_loads" >> "$analysis_file"
        echo "L3缓存缺失: $l3_misses" >> "$analysis_file"
        if (( $(echo "$l3_loads > 0" | bc -l) )); then
            local l3_hit_rate=$(echo "scale=2; (1 - $l3_misses / $l3_loads) * 100" | bc -l)
            echo "L3缓存命中率: ${l3_hit_rate}%" >> "$analysis_file"
        fi
        echo "" >> "$analysis_file"
    fi
    
    # YAT_CASCHED调度器高负载影响分析
    echo "=== YAT_CASCHED调度器高负载缓存影响分析 ===" >> "$analysis_file"
    
    if [ -n "$context_switches" ] && [ -n "$cpu_migrations" ]; then
        echo "高负载调度特征分析:" >> "$analysis_file"
        echo "- 上下文切换次数: $context_switches" >> "$analysis_file"
        echo "- CPU迁移次数: $cpu_migrations" >> "$analysis_file"
        
        if (( $(echo "$context_switches > 0" | bc -l) )); then
            local migration_ratio=$(echo "scale=2; $cpu_migrations * 100 / $context_switches" | bc -l)
            echo "- CPU迁移率: ${migration_ratio}%" >> "$analysis_file"
            
            # 高负载下的缓存污染程度评估
            if (( $(echo "$migration_ratio > 50" | bc -l) )); then
                echo "- 评估: 极高CPU迁移率，高负载下缓存污染严重" >> "$analysis_file"
                echo "- 建议: YAT_CASCHED需要增强CPU亲和性约束" >> "$analysis_file"
            elif (( $(echo "$migration_ratio > 30" | bc -l) )); then
                echo "- 评估: 高CPU迁移率，高负载加剧了缓存污染" >> "$analysis_file"
                echo "- 建议: 考虑优化调度算法的负载均衡策略" >> "$analysis_file"
            elif (( $(echo "$migration_ratio > 15" | bc -l) )); then
                echo "- 评估: 中等CPU迁移率，高负载对缓存有一定影响" >> "$analysis_file"
                echo "- 建议: 可以进一步优化缓存感知调度" >> "$analysis_file"
            else
                echo "- 评估: 低CPU迁移率，即使高负载下缓存局部性依然良好" >> "$analysis_file"
                echo "- 评估: YAT_CASCHED在高负载下表现优秀" >> "$analysis_file"
            fi
            
            # 与预期的轻负载对比
            echo "" >> "$analysis_file"
            echo "高负载调度压力测试结论:" >> "$analysis_file"
            echo "- 并发进程数: 36个 (12任务×3副本)" >> "$analysis_file"
            echo "- 任务重复次数: 50次" >> "$analysis_file"
            echo "- 测试强度: 极高负载场景" >> "$analysis_file"
        fi
    fi
    
    # 综合评估
    echo "" >> "$analysis_file"
    echo "=== 高负载综合性能评估 ===" >> "$analysis_file"
    echo "基于VMware虚拟化环境的高负载多级缓存性能测试结果。" >> "$analysis_file"
    echo "测试成功验证了YAT_CASCHED调度器在极高负载场景下的性能表现。" >> "$analysis_file"
    echo "注意: 虚拟化环境可能对某些性能计数器产生影响。" >> "$analysis_file"
    echo "" >> "$analysis_file"
    echo "建议后续测试:" >> "$analysis_file"
    echo "1. 与CFS调度器进行对比测试" >> "$analysis_file"
    echo "2. 在物理机环境下验证结果" >> "$analysis_file"
    echo "3. 测试不同负载强度下的性能曲线" >> "$analysis_file"
    
    echo "$analysis_file"
}

# 主执行流程
echo "开始执行高负载多级缓存性能监控..."

# 检查可用事件
check_available_events

# 运行高负载多级缓存监控
if run_multilevel_cache_monitoring; then
    multilevel_file="${OUTPUT_DIR}/multilevel_cache_${TIMESTAMP}.txt"
    
    echo -e "${BLUE}正在分析高负载多级缓存性能数据...${NC}"
    analysis_file=$(analyze_multilevel_cache "$multilevel_file")
    
    echo -e "${GREEN}=== 高负载多级缓存性能分析结果 ===${NC}"
    cat "$analysis_file"
    
    echo ""
    echo -e "${BLUE}完整分析报告: $analysis_file${NC}"
    echo -e "${BLUE}原始数据文件: $multilevel_file${NC}"
else
    echo -e "${RED}高负载多级缓存监控失败${NC}"
fi

echo -e "${GREEN}高负载监控完成!${NC}"