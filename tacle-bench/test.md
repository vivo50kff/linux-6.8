# Yat-CAsched 调度算法测试指南

## 目录
- [测试概述](#测试概述)
- [环境准备](#环境准备)
- [内核配置验证](#内核配置验证)
- [调度算法启用](#调度算法启用)
- [TACLeBench 测试准备](#taclebench-测试准备)
- [测试执行](#测试执行)
- [数据收集与分析](#数据收集与分析)
- [对比基准测试](#对比基准测试)
- [故障排除](#故障排除)
- [测试报告模板](#测试报告模板)

## 测试概述

### 测试目标
使用 TACLeBench 基准测试套件评估 Yat-CAsched 调度算法的性能，主要关注：
- **实时性能**：任务响应时间、截止时间满足率
- **系统吞吐量**：单位时间内完成的任务数量
- **资源利用率**：CPU、内存使用效率
- **可预测性**：执行时间的稳定性和可预测性

### 测试策略
1. **基线对比**：与标准调度算法（CFS、RT）对比
2. **负载分级**：轻载、中载、重载场景测试
3. **任务类型**：不同类型基准测试的表现
4. **长期稳定性**：长时间运行的稳定性测试

## 环境准备

### 系统要求
```bash
# 确认系统环境
uname -a
cat /proc/version
cat /proc/cpuinfo | grep "model name" | head -1
free -h
```

### 必要工具安装
```bash
# 性能监控工具
sudo apt update
sudo apt install -y \
    linux-tools-common \
    linux-tools-generic \
    linux-tools-$(uname -r) \
    htop \
    iotop \
    sysstat \
    trace-cmd \
    kernelshark \
    stress-ng \
    rt-tests

# 时间同步
sudo apt install -y chrony
sudo systemctl enable chrony
sudo systemctl start chrony

# 数据分析工具
sudo apt install -y \
    python3-pip \
    python3-matplotlib \
    python3-numpy \
    python3-pandas \
    gnuplot
```

### 测试环境优化
```bash
#!/bin/bash
# 创建测试环境优化脚本：setup_test_env.sh

echo "配置测试环境..."

# 禁用不必要的服务
sudo systemctl stop bluetooth
sudo systemctl stop cups
sudo systemctl stop networkd-dispatcher

# 设置 CPU 性能模式
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# 禁用 NUMA balancing（如果适用）
echo 0 | sudo tee /proc/sys/kernel/numa_balancing

# 设置实时优先级限制
echo "* soft rtprio 99" | sudo tee -a /etc/security/limits.conf
echo "* hard rtprio 99" | sudo tee -a /etc/security/limits.conf

# 关闭 swap
sudo swapoff -a

# 设置内核参数
echo 'kernel.sched_rt_runtime_us = -1' | sudo tee -a /etc/sysctl.conf
echo 'kernel.sched_rt_period_us = 1000000' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

echo "测试环境配置完成"
```

## 内核配置验证

### 验证自定义内核
```bash
# 检查当前运行的内核
uname -r

# 验证调度算法是否编译进内核
cat /proc/sched_debug | head -20

# 检查可用的调度类
ls /sys/kernel/debug/sched/

# 验证内核配置
zcat /proc/config.gz | grep -i sched
# 或者
cat /boot/config-$(uname -r) | grep -i sched
```

### 关键配置项检查
```bash
# 检查这些配置是否启用
CONFIG_SCHED_DEBUG=y
CONFIG_SCHEDSTATS=y
CONFIG_SCHED_INFO=y
CONFIG_PREEMPT=y
CONFIG_HIGH_RES_TIMERS=y
CONFIG_NO_HZ_FULL=y
```

## 调度算法启用

### 查看当前调度策略
```bash
# 查看系统默认调度策略
cat /proc/sys/kernel/sched_domain/cpu0/domain0/name

# 查看进程调度信息
cat /proc/sched_debug

# 查看调度统计
cat /proc/schedstat
```

### 启用 Yat-CAsched
```bash
# 方法1：通过内核参数（如果支持）
# 在 GRUB 中添加：yat_casched=1

# 方法2：通过 sysfs（如果实现了接口）
echo 1 | sudo tee /sys/kernel/yat_casched/enable

# 方法3：通过调度策略设置（针对特定进程）
# 将在测试脚本中实现

# 验证调度算法状态
dmesg | grep -i yat
cat /proc/sched_debug | grep -i yat
```

### 进程调度策略设置
```bash
# 查看进程调度策略
chrt -p $$

# 设置进程为实时调度（如果需要）
# SCHED_FIFO: 1, SCHED_RR: 2, SCHED_NORMAL: 0
chrt -f 50 command    # FIFO，优先级50
chrt -r 50 command    # Round Robin，优先级50
```

## TACLeBench 测试准备

### 编译优化的测试版本
```bash
# 编译高精度计时版本
make clean
make all-host CFLAGS="-O2 -Wall -Wextra -Wno-unknown-pragmas -DMEASURE_TIMING -g"

# 验证编译结果
ls -la bench/*/*.host
```

### 创建测试配置文件
```bash
# 创建测试配置：test_config.conf
cat > test_config.conf << 'EOF'
# TACLeBench 测试配置

# 测试基准选择
SEQUENTIAL_TESTS="susan dijkstra crc adpcm"
KERNEL_TESTS="sha matrix1 prime bitcount"
APP_TESTS="powerwindow"
PARALLEL_TESTS=""  # 根据需要启用

# 测试参数
ITERATIONS=10           # 每个测试重复次数
WARMUP_RUNS=3          # 预热运行次数
TIMEOUT=300            # 单个测试超时时间（秒）

# 系统监控
MONITOR_CPU=true
MONITOR_MEMORY=true
MONITOR_IO=true
MONITOR_SCHED=true

# 输出目录
OUTPUT_DIR="./test_results/$(date +%Y%m%d_%H%M%S)"
EOF
```

## 测试执行

### 主测试脚本
```bash
#!/bin/bash
# 创建主测试脚本：run_taclebench_test.sh

set -e

# 加载配置
source test_config.conf

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 日志函数
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$OUTPUT_DIR/test.log"
}

# 系统信息收集
collect_system_info() {
    log "收集系统信息..."
    
    {
        echo "=== 系统信息 ==="
        uname -a
        echo
        echo "=== CPU 信息 ==="
        cat /proc/cpuinfo
        echo
        echo "=== 内存信息 ==="
        cat /proc/meminfo
        echo
        echo "=== 调度信息 ==="
        cat /proc/sched_debug
        echo
        echo "=== 内核参数 ==="
        sysctl -a | grep sched
    } > "$OUTPUT_DIR/system_info.txt"
}

# 启动系统监控
start_monitoring() {
    log "启动系统监控..."
    
    # CPU 使用率监控
    if [ "$MONITOR_CPU" = "true" ]; then
        sar -u 1 > "$OUTPUT_DIR/cpu_usage.log" &
        echo $! > "$OUTPUT_DIR/sar_cpu.pid"
    fi
    
    # 内存监控
    if [ "$MONITOR_MEMORY" = "true" ]; then
        sar -r 1 > "$OUTPUT_DIR/memory_usage.log" &
        echo $! > "$OUTPUT_DIR/sar_mem.pid"
    fi
    
    # 调度监控
    if [ "$MONITOR_SCHED" = "true" ]; then
        # 定期收集调度统计
        while true; do
            echo "$(date): $(cat /proc/schedstat)" >> "$OUTPUT_DIR/schedstat.log"
            sleep 1
        done &
        echo $! > "$OUTPUT_DIR/schedstat.pid"
    fi
}

# 停止监控
stop_monitoring() {
    log "停止系统监控..."
    
    for pidfile in "$OUTPUT_DIR"/*.pid; do
        if [ -f "$pidfile" ]; then
            pid=$(cat "$pidfile")
            kill $pid 2>/dev/null || true
            rm "$pidfile"
        fi
    done
}

# 运行单个基准测试
run_benchmark() {
    local benchmark=$1
    local category=$2
    local test_name=$(basename "$benchmark" .host)
    
    log "运行测试: $test_name (类别: $category)"
    
    local result_file="$OUTPUT_DIR/${category}_${test_name}_results.txt"
    
    # 预热运行
    for i in $(seq 1 $WARMUP_RUNS); do
        log "预热运行 $i/$WARMUP_RUNS: $test_name"
        timeout $TIMEOUT "$benchmark" > /dev/null 2>&1 || true
        sleep 1
    done
    
    # 正式测试
    {
        echo "=== $test_name 测试结果 ==="
        echo "测试时间: $(date)"
        echo "迭代次数: $ITERATIONS"
        echo
    } > "$result_file"
    
    for i in $(seq 1 $ITERATIONS); do
        log "正式运行 $i/$ITERATIONS: $test_name"
        
        # 记录测试前的系统状态
        echo "--- 迭代 $i ---" >> "$result_file"
        echo "开始时间: $(date +%s.%N)" >> "$result_file"
        
        # 使用 perf 进行详细性能分析
        perf stat -e cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations \
            timeout $TIMEOUT "$benchmark" >> "$result_file" 2>&1
        
        local exit_code=$?
        echo "结束时间: $(date +%s.%N)" >> "$result_file"
        echo "退出代码: $exit_code" >> "$result_file"
        echo "" >> "$result_file"
        
        if [ $exit_code -ne 0 ] && [ $exit_code -ne 124 ]; then
            log "警告: $test_name 迭代 $i 异常退出 (代码: $exit_code)"
        fi
        
        sleep 2  # 避免系统过载
    done
}

# 运行特定类别的测试
run_category_tests() {
    local category=$1
    local tests=$2
    
    log "开始运行 $category 类别测试..."
    
    for test in $tests; do
        local benchmark_path="bench/$category/$test/$test.host"
        
        if [ -x "$benchmark_path" ]; then
            run_benchmark "$benchmark_path" "$category"
        else
            log "警告: 找不到可执行文件 $benchmark_path"
        fi
    done
}

# 主测试流程
main() {
    log "开始 TACLeBench 测试..."
    
    # 收集系统信息
    collect_system_info
    
    # 启动监控
    start_monitoring
    
    # 注册清理函数
    trap stop_monitoring EXIT
    
    # 运行各类别测试
    [ -n "$SEQUENTIAL_TESTS" ] && run_category_tests "sequential" "$SEQUENTIAL_TESTS"
    [ -n "$KERNEL_TESTS" ] && run_category_tests "kernel" "$KERNEL_TESTS"
    [ -n "$APP_TESTS" ] && run_category_tests "app" "$APP_TESTS"
    [ -n "$PARALLEL_TESTS" ] && run_category_tests "parallel" "$PARALLEL_TESTS"
    
    log "测试完成，结果保存在: $OUTPUT_DIR"
}

# 执行主函数
main "$@"
```

### 性能对比测试脚本
```bash
#!/bin/bash
# 创建对比测试脚本：compare_schedulers.sh

# 测试不同调度算法的性能
SCHEDULERS=("yat-casched" "cfs" "rt")
TEST_SETS=("sequential" "kernel")

for scheduler in "${SCHEDULERS[@]}"; do
    echo "测试调度算法: $scheduler"
    
    # 切换调度算法（根据实现方式调整）
    case $scheduler in
        "yat-casched")
            echo 1 | sudo tee /sys/kernel/yat_casched/enable
            ;;
        "cfs")
            echo 0 | sudo tee /sys/kernel/yat_casched/enable
            ;;
        "rt")
            # 设置实时调度策略
            ;;
    esac
    
    # 运行测试
    OUTPUT_DIR="./results_$scheduler" ./run_taclebench_test.sh
    
    sleep 10  # 等待系统稳定
done
```

## 数据收集与分析

### 关键性能指标

#### 1. 时序性能指标
- **执行时间**：平均、最小、最大、标准差
- **响应时间**：从任务到达到开始执行的时间
- **完成时间**：从任务开始到完成的总时间
- **时间抖动**：执行时间的变异性

#### 2. 系统性能指标
- **CPU 利用率**：用户态、内核态、空闲时间
- **上下文切换**：切换次数和开销
- **缓存性能**：命中率、缺失率
- **内存使用**：RSS、VSZ、页面错误

#### 3. 调度相关指标
- **调度延迟**：调度决策的时间开销
- **任务等待时间**：在就绪队列中的等待时间
- **抢占次数**：任务被抢占的频率
- **负载均衡**：多核场景下的负载分布

### 数据分析脚本
```python
#!/usr/bin/env python3
# 创建数据分析脚本：analyze_results.py

import os
import re
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import argparse

class TACLeBenchAnalyzer:
    def __init__(self, result_dir):
        self.result_dir = Path(result_dir)
        self.data = {}
        
    def parse_perf_output(self, file_path):
        """解析 perf stat 输出"""
        results = []
        current_iteration = {}
        
        with open(file_path, 'r') as f:
            content = f.read()
            
        # 解析每次迭代的结果
        iterations = re.split(r'--- 迭代 (\d+) ---', content)
        
        for i in range(1, len(iterations), 2):
            iteration_num = int(iterations[i])
            iteration_data = iterations[i + 1]
            
            # 解析时间
            start_time = re.search(r'开始时间: ([\d.]+)', iteration_data)
            end_time = re.search(r'结束时间: ([\d.]+)', iteration_data)
            
            if start_time and end_time:
                duration = float(end_time.group(1)) - float(start_time.group(1))
                current_iteration['duration'] = duration
            
            # 解析 perf 统计
            cycles = re.search(r'([\d,]+)\s+cycles', iteration_data)
            instructions = re.search(r'([\d,]+)\s+instructions', iteration_data)
            context_switches = re.search(r'([\d,]+)\s+context-switches', iteration_data)
            
            if cycles:
                current_iteration['cycles'] = int(cycles.group(1).replace(',', ''))
            if instructions:
                current_iteration['instructions'] = int(instructions.group(1).replace(',', ''))
            if context_switches:
                current_iteration['context_switches'] = int(context_switches.group(1).replace(',', ''))
                
            results.append(current_iteration.copy())
            
        return results
    
    def analyze_benchmark(self, benchmark_file):
        """分析单个基准测试的结果"""
        results = self.parse_perf_output(benchmark_file)
        if not results:
            return None
            
        df = pd.DataFrame(results)
        
        stats = {
            'count': len(results),
            'duration_mean': df['duration'].mean() if 'duration' in df else 0,
            'duration_std': df['duration'].std() if 'duration' in df else 0,
            'duration_min': df['duration'].min() if 'duration' in df else 0,
            'duration_max': df['duration'].max() if 'duration' in df else 0,
            'cycles_mean': df['cycles'].mean() if 'cycles' in df else 0,
            'instructions_mean': df['instructions'].mean() if 'instructions' in df else 0,
            'context_switches_mean': df['context_switches'].mean() if 'context_switches' in df else 0,
        }
        
        return stats
    
    def analyze_all_results(self):
        """分析所有测试结果"""
        for result_file in self.result_dir.glob("*_results.txt"):
            benchmark_name = result_file.stem.replace('_results', '')
            stats = self.analyze_benchmark(result_file)
            if stats:
                self.data[benchmark_name] = stats
    
    def generate_report(self, output_file):
        """生成分析报告"""
        if not self.data:
            print("没有找到可分析的数据")
            return
            
        # 创建汇总表
        df = pd.DataFrame(self.data).T
        df.to_csv(output_file.replace('.txt', '.csv'))
        
        # 生成文本报告
        with open(output_file, 'w') as f:
            f.write("TACLeBench 测试结果分析报告\n")
            f.write("=" * 50 + "\n\n")
            
            f.write("测试汇总:\n")
            f.write(f"总测试数量: {len(self.data)}\n\n")
            
            f.write("各基准测试结果:\n")
            f.write("-" * 40 + "\n")
            
            for benchmark, stats in self.data.items():
                f.write(f"\n{benchmark}:\n")
                f.write(f"  平均执行时间: {stats['duration_mean']:.6f}s\n")
                f.write(f"  时间标准差: {stats['duration_std']:.6f}s\n")
                f.write(f"  最小执行时间: {stats['duration_min']:.6f}s\n")
                f.write(f"  最大执行时间: {stats['duration_max']:.6f}s\n")
                f.write(f"  平均 CPU 周期: {stats['cycles_mean']:.0f}\n")
                f.write(f"  平均指令数: {stats['instructions_mean']:.0f}\n")
                f.write(f"  平均上下文切换: {stats['context_switches_mean']:.1f}\n")
    
    def plot_results(self, output_dir):
        """生成性能图表"""
        if not self.data:
            return
            
        # 执行时间对比图
        benchmarks = list(self.data.keys())
        durations = [self.data[b]['duration_mean'] for b in benchmarks]
        std_devs = [self.data[b]['duration_std'] for b in benchmarks]
        
        plt.figure(figsize=(12, 6))
        bars = plt.bar(benchmarks, durations, yerr=std_devs, capsize=5)
        plt.title('基准测试执行时间对比')
        plt.xlabel('基准测试')
        plt.ylabel('执行时间 (秒)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        plt.savefig(f"{output_dir}/execution_times.png")
        plt.close()
        
        # 性能稳定性图
        plt.figure(figsize=(12, 6))
        coefficients = [self.data[b]['duration_std']/self.data[b]['duration_mean'] * 100 
                       for b in benchmarks]
        plt.bar(benchmarks, coefficients)
        plt.title('执行时间变异系数 (稳定性指标)')
        plt.xlabel('基准测试')
        plt.ylabel('变异系数 (%)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        plt.savefig(f"{output_dir}/stability.png")
        plt.close()

def main():
    parser = argparse.ArgumentParser(description='分析 TACLeBench 测试结果')
    parser.add_argument('result_dir', help='测试结果目录')
    parser.add_argument('-o', '--output', default='analysis_report.txt', 
                       help='输出报告文件名')
    
    args = parser.parse_args()
    
    analyzer = TACLeBenchAnalyzer(args.result_dir)
    analyzer.analyze_all_results()
    analyzer.generate_report(args.output)
    analyzer.plot_results(os.path.dirname(args.output) or '.')
    
    print(f"分析完成，报告保存为: {args.output}")

if __name__ == "__main__":
    main()
```

## 对比基准测试

### 与标准调度算法对比
```bash
#!/bin/bash
# 创建对比分析脚本：benchmark_comparison.sh

echo "执行调度算法对比测试..."

# 测试配置
BASELINE_SCHEDULER="cfs"
TEST_SCHEDULER="yat-casched"
TESTS="susan dijkstra sha matrix1"

# 运行基线测试
echo "运行基线调度算法测试 ($BASELINE_SCHEDULER)..."
# 切换到基线调度算法
# ... 调度算法切换逻辑 ...

OUTPUT_DIR="./baseline_results" ./run_taclebench_test.sh

# 运行测试调度算法
echo "运行测试调度算法 ($TEST_SCHEDULER)..."
# 切换到测试调度算法
# ... 调度算法切换逻辑 ...

OUTPUT_DIR="./test_results" ./run_taclebench_test.sh

# 生成对比报告
python3 compare_schedulers.py baseline_results test_results
```

### 性能对比分析脚本
```python
#!/usr/bin/env python3
# compare_schedulers.py - 调度算法性能对比

import pandas as pd
import matplotlib.pyplot as plt
import sys
from pathlib import Path

def compare_results(baseline_dir, test_dir):
    """对比两个调度算法的测试结果"""
    
    baseline_analyzer = TACLeBenchAnalyzer(baseline_dir)
    test_analyzer = TACLeBenchAnalyzer(test_dir)
    
    baseline_analyzer.analyze_all_results()
    test_analyzer.analyze_all_results()
    
    # 找到共同的基准测试
    common_benchmarks = set(baseline_analyzer.data.keys()) & set(test_analyzer.data.keys())
    
    if not common_benchmarks:
        print("没有找到共同的基准测试结果")
        return
    
    # 创建对比数据
    comparison_data = []
    
    for benchmark in common_benchmarks:
        baseline_stats = baseline_analyzer.data[benchmark]
        test_stats = test_analyzer.data[benchmark]
        
        speedup = baseline_stats['duration_mean'] / test_stats['duration_mean']
        
        comparison_data.append({
            'benchmark': benchmark,
            'baseline_time': baseline_stats['duration_mean'],
            'test_time': test_stats['duration_mean'],
            'speedup': speedup,
            'baseline_std': baseline_stats['duration_std'],
            'test_std': test_stats['duration_std']
        })
    
    df = pd.DataFrame(comparison_data)
    
    # 生成对比报告
    with open('scheduler_comparison.txt', 'w') as f:
        f.write("调度算法性能对比报告\n")
        f.write("=" * 50 + "\n\n")
        
        f.write(f"基线调度算法结果目录: {baseline_dir}\n")
        f.write(f"测试调度算法结果目录: {test_dir}\n")
        f.write(f"对比基准测试数量: {len(common_benchmarks)}\n\n")
        
        f.write("详细对比结果:\n")
        f.write("-" * 40 + "\n")
        
        for _, row in df.iterrows():
            f.write(f"\n{row['benchmark']}:\n")
            f.write(f"  基线时间: {row['baseline_time']:.6f}s (±{row['baseline_std']:.6f})\n")
            f.write(f"  测试时间: {row['test_time']:.6f}s (±{row['test_std']:.6f})\n")
            f.write(f"  加速比: {row['speedup']:.3f}x\n")
            if row['speedup'] > 1:
                f.write(f"  改进: {(row['speedup']-1)*100:.1f}% 更快\n")
            else:
                f.write(f"  退化: {(1-row['speedup'])*100:.1f}% 更慢\n")
        
        f.write(f"\n总体统计:\n")
        f.write(f"  平均加速比: {df['speedup'].mean():.3f}x\n")
        f.write(f"  最大加速比: {df['speedup'].max():.3f}x\n")
        f.write(f"  最小加速比: {df['speedup'].min():.3f}x\n")
    
    # 生成对比图表
    plt.figure(figsize=(12, 8))
    
    # 加速比图
    plt.subplot(2, 1, 1)
    bars = plt.bar(df['benchmark'], df['speedup'])
    plt.axhline(y=1, color='r', linestyle='--', alpha=0.7, label='基线性能')
    plt.title('调度算法性能对比 (加速比)')
    plt.ylabel('加速比')
    plt.legend()
    
    # 为柱状图添加数值标签
    for bar, speedup in zip(bars, df['speedup']):
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 0.01,
                f'{speedup:.2f}x', ha='center', va='bottom')
    
    # 执行时间对比图
    plt.subplot(2, 1, 2)
    x = range(len(df))
    width = 0.35
    
    plt.bar([i - width/2 for i in x], df['baseline_time'], width, 
            label='基线调度算法', alpha=0.8)
    plt.bar([i + width/2 for i in x], df['test_time'], width, 
            label='测试调度算法', alpha=0.8)
    
    plt.title('执行时间对比')
    plt.xlabel('基准测试')
    plt.ylabel('执行时间 (秒)')
    plt.xticks(x, df['benchmark'], rotation=45)
    plt.legend()
    
    plt.tight_layout()
    plt.savefig('scheduler_comparison.png', dpi=300, bbox_inches='tight')
    plt.close()
    
    print("对比分析完成:")
    print(f"- 报告: scheduler_comparison.txt")
    print(f"- 图表: scheduler_comparison.png")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("用法: python3 compare_schedulers.py <baseline_dir> <test_dir>")
        sys.exit(1)
    
    compare_results(sys.argv[1], sys.argv[2])
```

## 故障排除

### 常见问题及解决方案

#### 1. 调度算法未生效
```bash
# 检查内核模块加载
lsmod | grep yat

# 检查内核日志
dmesg | grep -i yat
journalctl -k | grep -i yat

# 验证调度策略
cat /proc/sched_debug | grep -A 10 -B 10 yat
```

#### 2. 测试结果异常
```bash
# 检查系统负载
uptime
ps aux | head -20

# 检查资源限制
ulimit -a

# 验证测试环境
./setup_test_env.sh
```

#### 3. 性能监控失效
```bash
# 检查 perf 工具
perf --version
perf list | head -10

# 检查调试文件系统
mount | grep debugfs
ls /sys/kernel/debug/
```

### 调试工具使用

#### ftrace 跟踪
```bash
# 启用调度跟踪
echo 1 > /sys/kernel/debug/tracing/events/sched/enable
echo 1 > /sys/kernel/debug/tracing/tracing_on

# 运行测试
./bench/kernel/sha/sha.host

# 查看跟踪结果
cat /sys/kernel/debug/tracing/trace | head -50

# 禁用跟踪
echo 0 > /sys/kernel/debug/tracing/tracing_on
```

#### 实时跟踪
```bash
# 使用 trace-cmd 实时跟踪
trace-cmd record -e sched_switch -e sched_wakeup ./bench/kernel/sha/sha.host
trace-cmd report > trace_report.txt
```

## 测试报告模板

### 测试报告结构
```markdown
# Yat-CAsched 调度算法测试报告

## 1. 测试概述
- 测试日期：
- 测试环境：
- 内核版本：
- 测试目标：

## 2. 测试环境
- 硬件配置：
- 软件环境：
- 调度算法配置：

## 3. 测试方法
- 基准测试选择：
- 测试参数：
- 性能指标：

## 4. 测试结果
### 4.1 执行性能
- 平均执行时间
- 时间稳定性
- 吞吐量对比

### 4.2 系统性能
- CPU 利用率
- 内存使用
- 上下文切换开销

### 4.3 调度性能
- 调度延迟
- 响应时间
- 抢占特性

## 5. 性能对比
- 与 CFS 对比
- 与 RT 调度对比
- 改进幅度分析

## 6. 结论与建议
- 性能评估
- 优化建议
- 下一步工作

## 7. 附录
- 详细测试数据
- 配置文件
- 问题记录
```

### 自动生成报告脚本
```bash
#!/bin/bash
# generate_report.sh - 自动生成测试报告

RESULT_DIR=$1
OUTPUT_FILE=${2:-"test_report_$(date +%Y%m%d).md"}

cat > "$OUTPUT_FILE" << EOF
# Yat-CAsched 调度算法测试报告

## 测试信息
- **测试日期**: $(date)
- **测试环境**: $(uname -a)
- **内核版本**: $(uname -r)
- **结果目录**: $RESULT_DIR

## 系统配置
\`\`\`
$(cat "$RESULT_DIR/system_info.txt" | head -20)
\`\`\`

## 测试结果汇总
$(python3 analyze_results.py "$RESULT_DIR" -o /tmp/analysis.txt && cat /tmp/analysis.txt)

## 性能图表
![执行时间对比](execution_times.png)
![稳定性指标](stability.png)

## 详细分析
<!-- 这里添加详细的分析内容 -->

EOF

echo "测试报告已生成: $OUTPUT_FILE"
```

---

*本测试指南提供了完整的 Yat-CAsched 调度算法测试流程，包括环境准备、测试执行、数据分析和结果报告。请根据实际情况调整测试参数和脚本。*
