# TACLeBench 新手入门指导

## 📖 目录

- [什么是 TACLeBench](#什么是-taclebench)
- [基础概念解释](#基础概念解释)
- [环境准备](#环境准备)
- [检查可用的测试程序](#检查可用的测试程序)
- [第一次运行测试](#第一次运行测试)
- [复杂项目说明](#复杂项目说明)
- [理解测试结果](#理解测试结果)
- [常用测试指令详解](#常用测试指令详解)
- [常见问题解答](#常见问题解答)

## 🎯 什么是 TACLeBench

### 简单理解

TACLeBench 是一个**基准测试程序集合**，就像是一套"标准考试题"，用来测试你的调度算法性能如何。

### 为什么需要它？

- **标准化测试**：就像高考有统一试卷，TACLeBench 提供统一的测试程序
- **性能对比**：可以比较不同调度算法的优劣
- **实时系统评估**：专门用于测试实时系统的时间性能

### 包含什么？

```
TACLeBench 包含 4 类测试程序：
├── sequential/   # 顺序执行的程序（如图像处理、路径计算）
├── kernel/      # 系统核心功能（如加密、矩阵运算）
├── app/         # 完整应用程序（如汽车控制系统）
└── parallel/    # 并行处理程序（如无人机控制）
```

## 🧠 基础概念解释

### 调度算法是什么？

**简单比喻**：想象你是一个工厂经理，有很多工人（CPU核心）和很多任务。调度算法就是决定"哪个工人什么时候做哪个任务"的规则。

### 为什么要测试调度算法？

- **效率**：哪种安排方式让工作完成得更快？
- **公平性**：是否每个任务都能及时完成？
- **可预测性**：能否准确估算完成时间？

### 关键术语解释

- **WCET**：最坏情况执行时间（Worst-Case Execution Time）
  - **简单理解**：程序在最不利情况下需要的最长时间
- **响应时间**：从任务开始到完成的总时间
- **吞吐量**：单位时间内完成的任务数量
- **上下文切换**：CPU 从执行一个任务切换到另一个任务

## 🛠️ 环境准备

### 第一步：检查系统环境

```bash
# 查看当前系统信息
uname -a
# 解释：显示操作系统名称、版本、架构等信息

# 查看 CPU 信息
cat /proc/cpuinfo | grep "model name" | head -1
# 解释：显示 CPU 型号，影响程序执行速度

# 查看内存信息
free -h
# 解释：显示内存使用情况，确保有足够内存运行测试
```

### 第二步：安装必要工具

```bash
# 更新软件包列表
sudo apt update
# 解释：获取最新的软件包信息

# 安装编译工具
sudo apt install -y gcc make
# 解释：
# - gcc: C语言编译器，将源代码转换为可执行程序
# - make: 自动化编译工具，按照 Makefile 规则编译

# 安装性能监控工具
sudo apt install -y linux-tools-common linux-tools-generic
# 解释：安装 perf 等性能分析工具
```

### 第三步：验证安装

```bash
# 检查编译器版本
gcc --version
# 解释：确认 gcc 安装成功

# 检查 make 工具
make --version
# 解释：确认 make 工具可用

# 检查性能工具
perf --version
# 解释：确认性能监控工具可用
```

## 🔍 检查可用的测试程序

### ⚠️ 重要：了解项目的复杂性

TACLeBench 包含两类项目：

1. **简单项目**：单个可执行文件，容易编译和运行
2. **复杂项目**：多架构、多组件项目，需要特殊处理

#### 查看简单的可编译项目

```bash
# 查看推荐的简单项目
make list-simple
```

典型输出：

```
=== 推荐的简单基准测试 ===

sequential 类别:
  ✓ susan (4 个C文件)
  ✓ dijkstra (2 个C文件)
  ✓ adpcm (6 个C文件)

kernel 类别:
  ✓ sha (3 个C文件)
  ✓ matrix1 (1 个C文件)

app 类别:
  ✓ powerwindow (15 个C文件)
```

#### 查看所有项目（包括复杂项目）

```bash
# 查看所有项目
make list-benchmarks
```

### 编译简单项目

#### 只编译简单项目（推荐新手）

```bash
# 编译 sequential 类别（通常最简单）
make sequential-host

# 编译 kernel 类别
make kernel-host

# 编译 app 类别
make app-host
```

## 🚀 第一次运行测试

### 基于简单项目的测试

根据 `make list-simple` 的结果，选择简单的项目开始：

#### 情况1：如果有 Matrix 程序（最简单）

```bash
# 检查是否编译成功
ls bench/kernel/matrix1/matrix1.host

# 运行 Matrix 测试
./bench/kernel/matrix1/matrix1.host
```

#### 情况2：如果有 SHA 程序

```bash
# 检查编译结果
ls bench/kernel/sha/sha.host

# 运行 SHA 测试
./bench/kernel/sha/sha.host
```

#### 情况3：如果有 Dijkstra 程序

```bash
# 检查编译结果
ls bench/sequential/dijkstra/dijkstra.host

# 运行 Dijkstra 测试
./bench/sequential/dijkstra/dijkstra.host
```

### 避免的项目（新手）

以下项目对新手来说过于复杂，建议先掌握简单项目：

- **DEBIE**：太空碎片检测系统，多架构项目
- **PapaBench**：无人机控制系统，需要特殊编译环境
- **Rosace**：多线程飞行控制系统

## 🔧 复杂项目说明

### 为什么某些项目编译失败？

你看到的编译失败是正常的，原因如下：

#### 1. DEBIE 项目

```
编译 bench/parallel/DEBIE/code/arch/arm7 (主机版本)
警告: arm7 编译失败
```

**原因**：

- DEBIE 是多架构项目，`arch/arm7` 是 ARM7 架构特定代码
- 不能直接用 GCC 编译 ARM7 代码
- 需要使用项目自带的编译系统

#### 2. PapaBench 项目

```
编译 bench/parallel/PapaBench/sw/airborne/autopilot (主机版本)
警告: autopilot 编译失败
```

**原因**：

- PapaBench 是无人机控制系统
- `sw/airborne/autopilot` 是子组件，不是独立程序
- 需要特殊的 AVR 编译器

#### 3. Rosace 项目

```
编译 bench/parallel/rosace/thread1 (主机版本)
警告: thread1 编译失败
```

**原因**：

- Rosace 是多线程项目
- `thread1` 是线程组件，不是完整程序
- 需要链接所有线程组件

### 如何处理复杂项目

#### 查看复杂项目编译指南

```bash
# 获取复杂项目的编译帮助
make help-complex
```

#### DEBIE 项目编译

```bash
# 进入 DEBIE 目录
cd bench/parallel/DEBIE/code

# 查看编译说明
cat README.txt

# 查看可用的目标架构
ls arch/

# 编译 x86 版本（如果支持）
cd arch/x86
make  # 如果有 Makefile
```

#### PapaBench 项目编译

```bash
# 进入 PapaBench 目录
cd bench/parallel/PapaBench

# 查看配置文件
ls conf/

# 查看编译说明
cat README  # 如果存在
```

### 为新手的建议

#### 优先级排序

1. **第一优先级**：kernel 和 sequential 目录下的简单项目
2. **第二优先级**：app 目录下的项目
3. **第三优先级**：parallel 目录下的简单项目
4. **最后考虑**：DEBIE、PapaBench 等复杂项目

#### 学习路径

```bash
# 第1步：掌握简单项目
make list-simple
make kernel-host
./bench/kernel/*/*.host

# 第2步：学习性能测量
time ./bench/kernel/sha/sha.host
perf stat ./bench/kernel/sha/sha.host

# 第3步：批量测试
./smart_benchmark.sh

# 第4步：尝试复杂项目
make help-complex
```

## 📊 理解测试结果

### 为什么有些程序没有输出？

这是 TACLeBench 程序的正常现象！许多基准测试程序设计为：
- **专注于计算性能**，而不是用户交互
- **内部验证结果**，通过返回码表示成功/失败
- **避免 I/O 开销**，确保测试的是纯计算性能

### 如何验证程序是否正常运行？

#### 方法1：检查返回码
```bash
# 运行程序并检查返回码
./bench/kernel/sha/sha.host
echo "程序返回码: $?"
# 返回码 0 = 成功，非 0 = 失败
```

#### 方法2：测量执行时间
```bash
# 使用 time 命令
time ./bench/kernel/sha/sha.host

# 典型输出：
# real    0m0.045s  <- 实际运行时间
# user    0m0.044s  <- CPU 用户态时间  
# sys     0m0.001s  <- CPU 内核态时间
```

#### 方法3：使用我们的测试工具
```bash
# 测试所有可用程序
make test-available

# 详细分析特定程序
make analyze-program PROG=bench/kernel/sha/sha.host
```

### 针对不同程序的结果解释

#### 1. SHA 程序（无输出型）
```bash
./bench/kernel/sha/sha.host
# 无输出 = 正常！程序在内部计算 SHA 哈希值
```

**验证方法**：
```bash
# 检查执行时间
time ./bench/kernel/sha/sha.host

# 使用详细分析
make analyze-program PROG=bench/kernel/sha/sha.host
```

#### 2. Matrix 程序（无输出型）
```bash
./bench/kernel/matrix1/matrix1.host
# 无输出 = 正常！程序在内部进行矩阵运算
```

#### 3. Susan 程序（需要参数）
```bash
# 错误的运行方式（会失败）
./bench/sequential/susan/susan.host

# 正确的运行方式（需要输入文件）
./bench/sequential/susan/susan.host input.pgm output.pgm -s
```

#### 4. 查看程序源码了解行为
```bash
# 查看 SHA 程序做什么
grep -n "main\|printf\|return" bench/kernel/sha/sha.c

# 查看 Matrix 程序做什么  
grep -n "main\|printf\|return" bench/kernel/matrix1/matrix1.c
```

## 🧪 简单测试实例（更新版）

### 实例1：验证无输出程序的性能

#### 创建验证脚本
```bash
# 创建验证脚本：verify_silent_programs.sh
cat > verify_silent_programs.sh << 'EOF'
#!/bin/bash

echo "=== 验证静默运行的基准测试程序 ==="
echo "测试时间: $(date)"
echo

# 查找所有可用的测试程序
AVAILABLE_TESTS=$(find bench -name "*.host" -type f 2>/dev/null)

if [ -z "$AVAILABLE_TESTS" ]; then
    echo "❌ 没有找到编译成功的程序"
    echo "请先运行: make sequential-host kernel-host"
    exit 1
fi

echo "找到 $(echo "$AVAILABLE_TESTS" | wc -l) 个可用测试程序"
echo

for test_prog in $AVAILABLE_TESTS; do
    name=$(basename "$test_prog" .host)
    category=$(echo "$test_prog" | cut -d'/' -f2)
    
    echo "--- 验证 $name ($category) ---"
    
    # 运行 3 次测量稳定性
    times=()
    all_success=true
    
    for i in {1..3}; do
        echo -n "  运行 $i/3: "
        
        start_time=$(date +%s.%N)
        timeout 15s "$test_prog" >/dev/null 2>&1
        exit_code=$?
        end_time=$(date +%s.%N)
        
        if [ $exit_code -eq 0 ]; then
            duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "0")
            times+=("$duration")
            printf "%.6fs ✓\n" "$duration"
        elif [ $exit_code -eq 124 ]; then
            echo "超时 ⏰"
            all_success=false
        else
            echo "失败 (退出码: $exit_code) ❌"
            all_success=false
        fi
    done
    
    if [ "$all_success" = true ] && [ ${#times[@]} -eq 3 ]; then
        # 计算平均时间和稳定性
        total=0
        for time in "${times[@]}"; do
            total=$(echo "$total + $time" | bc -l 2>/dev/null || echo "$total")
        done
        avg=$(echo "scale=6; $total / 3" | bc -l 2>/dev/null || echo "0")
        
        # 计算标准差（简化版）
        variance=0
        for time in "${times[@]}"; do
            diff=$(echo "$time - $avg" | bc -l 2>/dev/null || echo "0")
            sq_diff=$(echo "$diff * $diff" | bc -l 2>/dev/null || echo "0")
            variance=$(echo "$variance + $sq_diff" | bc -l 2>/dev/null || echo "$variance")
        done
        variance=$(echo "scale=6; $variance / 3" | bc -l 2>/dev/null || echo "0")
        
        echo "  ✓ 程序验证通过"
        printf "  📊 平均时间: %.6fs\n" "$avg"
        printf "  📈 时间稳定性: %.6f (越小越稳定)\n" "$variance"
        
        # 性能等级判断
        if (( $(echo "$avg < 0.01" | bc -l 2>/dev/null || echo 0) )); then
            echo "  🚀 性能等级: 极快 (< 0.01s)"
        elif (( $(echo "$avg < 0.1" | bc -l 2>/dev/null || echo 0) )); then
            echo "  ⚡ 性能等级: 快速 (< 0.1s)"
        elif (( $(echo "$avg < 1.0" | bc -l 2>/dev/null || echo 0) )); then
            echo "  🐢 性能等级: 中等 (< 1.0s)"
        else
            echo "  🐌 性能等级: 较慢 (> 1.0s)"
        fi
    else
        echo "  ❌ 程序验证失败"
    fi
    echo
done

echo "=== 验证完成 ==="
EOF

chmod +x verify_silent_programs.sh
```

#### 运行验证脚本
```bash
# 确保安装了 bc 计算器
sudo apt install -y bc

# 运行验证
./verify_silent_programs.sh
```

### 实例2：使用 perf 进行深度性能分析

#### 对静默程序进行详细分析
```bash
# 分析 SHA 程序的详细性能
echo "=== SHA 程序性能分析 ==="

# 基本性能统计
perf stat ./bench/kernel/sha/sha.host 2>&1 | head -15

echo ""
echo "=== Matrix 程序性能分析 ==="

# 矩阵程序性能统计  
perf stat ./bench/kernel/matrix1/matrix1.host 2>&1 | head -15
```

#### 创建性能比较脚本
```bash
# 创建性能比较脚本：compare_performance.sh
cat > compare_performance.sh << 'EOF'
#!/bin/bash

echo "=== TACLeBench 程序性能对比 ==="
echo "测试时间: $(date)"
echo

# 查找所有可用程序
PROGRAMS=$(find bench -name "*.host" -type f | head -10)

if [ -z "$PROGRAMS" ]; then
    echo "❌ 没有找到可用程序"
    exit 1
fi

echo "对比以下程序的性能:"
echo "$PROGRAMS" | sed 's/^/  - /'
echo

# 性能数据收集
declare -A performance_data

for prog in $PROGRAMS; do
    name=$(basename "$prog" .host)
    echo -n "测试 $name ... "
    
    # 运行 5 次取最佳时间
    best_time=999999
    for i in {1..5}; do
        start_time=$(date +%s.%N)
        timeout 10s "$prog" >/dev/null 2>&1
        exit_code=$?
        end_time=$(date +%s.%N)
        
        if [ $exit_code -eq 0 ]; then
            duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "999999")
            if (( $(echo "$duration < $best_time" | bc -l 2>/dev/null || echo 0) )); then
                best_time=$duration
            fi
        fi
    done
    
    if [ "$best_time" != "999999" ]; then
        performance_data["$name"]=$best_time
        printf "%.6fs ✓\n" "$best_time"
    else
        echo "失败 ❌"
    fi
done

echo ""
echo "=== 性能排名 (按执行时间排序) ==="

# 排序并显示结果
for name in $(for key in "${!performance_data[@]}"; do
    echo "${performance_data[$key]} $key"
done | sort -n | cut -d' ' -f2); do
    time=${performance_data[$name]}
    printf "🏆 %-15s: %.6fs\n" "$name" "$time"
done

echo ""
echo "=== 分析结论 ==="
fastest=$(for key in "${!performance_data[@]}"; do
    echo "${performance_data[$key]} $key"
done | sort -n | head -1 | cut -d' ' -f2)

slowest=$(for key in "${!performance_data[@]}"; do
    echo "${performance_data[$key]} $key"
done | sort -n | tail -1 | cut -d' ' -f2)

echo "🚀 最快程序: $fastest (${performance_data[$fastest]}s)"
echo "🐌 最慢程序: $slowest (${performance_data[$slowest]}s)"

if [ ${#performance_data[@]} -gt 1 ]; then
    ratio=$(echo "scale=2; ${performance_data[$slowest]} / ${performance_data[$fastest]}" | bc -l 2>/dev/null || echo "未知")
    echo "📊 性能差异: ${ratio}x"
fi
EOF

chmod +x compare_performance.sh
./compare_performance.sh
```

## ❓ 常见问题解答（更新版）

### Q1: 程序运行了但没有任何输出，这正常吗？

**答**：完全正常！许多 TACLeBench 程序是这样设计的：

```bash
# 这些都是正常的（无输出）
./bench/kernel/sha/sha.host          # SHA 哈希计算
./bench/kernel/matrix1/matrix1.host  # 矩阵运算
./bench/sequential/prime/prime.host  # 素数计算

# 验证程序是否正常工作
echo $?  # 检查返回码，0 = 成功
```

### Q2: 如何知道无输出程序在做什么？

**检查方法**：
```bash
# 1. 查看源代码
head -20 bench/kernel/sha/sha.c

# 2. 使用我们的分析工具
make analyze-program PROG=bench/kernel/sha/sha.host

# 3. 使用系统监控
top -p $(pgrep -f sha.host) &
./bench/kernel/sha/sha.host
```

### Q3: 如何确认程序性能是否正常？

**基准参考**：
```bash
# 典型的执行时间范围：
# - SHA: 0.001-0.1 秒
# - Matrix: 0.01-0.5 秒  
# - Prime: 0.1-2.0 秒

# 使用我们的验证脚本
./verify_silent_programs.sh
```

### Q4: 程序执行时间每次都不一样？

**原因和解决方案**：
```bash
# 原因：系统负载、缓存状态等影响
# 解决方案：多次运行取平均值

# 设置 CPU 性能模式（减少变化）
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# 使用我们的稳定性测试
./verify_silent_programs.sh
```

## 📝 更新的快速开始检查清单

- [ ] 理解项目复杂性（简单 vs 复杂项目）
- [ ] 查看推荐项目 (`make list-simple`)
- [ ] 编译简单项目 (`make sequential-host kernel-host`)
- [ ] 检查编译结果 (`find bench -name "*.host"`)
- [ ] 运行第一个成功的测试
- [ ] 使用智能测试脚本 (`./test_available_only.sh`)
- [ ] 理解为什么某些项目会失败
- [ ] 专注于简单项目的性能分析

**重要提醒**：不要被编译失败吓到！重点关注编译成功的简单项目。

---

*更新说明：这个版本明确区分了简单和复杂项目，帮助新手避免不必要的困惑，专注于可用的基准测试。*
