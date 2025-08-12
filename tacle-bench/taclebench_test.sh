#!/bin/bash

# TACLeBench 统一测试脚本
# 支持快速检查和完整测试模式

set -e

# 脚本参数
SCRIPT_MODE="${1:-}"  # 如果没有参数，将提示用户选择
TACLEBENCH_DIR="/home/yat/Desktop/linux-6.8/tacle-bench"
RESULTS_DIR="$TACLEBENCH_DIR/test_results"
TEST_DATE=$(date +%Y%m%d_%H%M%S)

# 交互式选择模式
select_mode() {
    echo "================================================"
    echo "        TACLeBench 测试脚本"
    echo "================================================"
    echo "🧪 请选择测试模式："
    echo ""
    echo "1) quick  - 快速检查模式 (推荐新手)"
    echo "           验证环境，测试4个关键程序，用时<30秒"
    echo ""
    echo "2) full   - 完整测试模式"
    echo "           测试kernel和sequential类程序(约50+个)，用时2-5分钟"
    echo ""
    echo "3) help   - 显示帮助信息"
    echo ""
    echo "4) exit   - 退出脚本"
    echo ""
    echo "💡 说明: 当前测试的是系统默认调度器(CFS)，可作为YAT_CASCHED对比基准"
    echo ""
    
    while true; do
        read -p "🚀 请输入选择 [1-4] 或 [quick/full/help/exit]: " choice
        
        case "$choice" in
            1|"quick")
                SCRIPT_MODE="quick"
                break
                ;;
            2|"full")
                SCRIPT_MODE="full"
                echo ""
                echo "⚠️  完整测试将运行kernel和sequential类程序，约需2-5分钟。"
                echo "💡 注意: parallel类程序因编译复杂已跳过，专注测试可用程序。"
                read -p "确认继续? [y/N]: " confirm
                if [[ "$confirm" =~ ^[Yy]$ ]]; then
                    break
                else
                    echo "❌ 取消完整测试"
                    continue
                fi
                ;;
            3|"help")
                SCRIPT_MODE="help"
                break
                ;;
            4|"exit")
                echo "👋 再见！"
                exit 0
                ;;
            "")
                echo "❌ 请输入有效选择"
                ;;
            *)
                echo "❌ 无效选择: $choice"
                echo "请输入 1-4 或 quick/full/help/exit"
                ;;
        esac
    done
    
    echo ""
}
# 显示帮助信息
show_help() {
    echo "================================================"
    echo "        TACLeBench 测试脚本 - 帮助信息"
    echo "================================================"
    cat << EOF

📖 脚本功能:
   TACLeBench 是一个实时系统基准测试套件，用于评估调度算法性能。
   本脚本支持快速检查和完整测试两种模式。

🚀 使用方法:
   $0                    # 交互式选择模式 (推荐)
   $0 quick              # 直接运行快速检查
   $0 full               # 直接运行完整测试
   $0 help               # 显示此帮助信息

📋 测试模式详解:

   🔹 quick (快速模式):
      - 验证编译环境和基本功能
      - 测试4个关键程序: 矩阵运算、SHA加密、FFT变换、快速排序
      - 显示基本性能数据
      - 用时: < 30秒
      - 适合: 环境验证和快速性能预览

   🔹 full (完整模式):
      - 编译kernel和sequential类程序
      - 测试约50个可用程序 (跳过复杂的parallel类)
      - 生成详细的性能报告
      - 保存结果到 test_results/ 目录
      - 用时: 2-5分钟
      - 适合: 完整的性能基准测试

📊 当前调度器:
   系统默认调度器 (通常是CFS)
   
🎯 用途:
   - 为YAT_CASCHED调度器建立对比基准
   - 评估系统性能表现  
   - 验证TACLeBench测试环境
   - 专注于kernel和sequential类基准测试

📝 说明:
   parallel类程序因编译复杂度高已跳过，专注测试稳定可用的程序。

📁 结果文件:
   test_results/cfs_baseline_YYYYMMDD_HHMMSS.log

⚙️  系统要求:
   - GCC 编译器
   - Make 工具
   - BC 计算器 (用于精确计时)
   - 至少4GB内存

🔗 更多信息:
   TACLeBench项目: https://www.tacle.eu/

EOF
}

# 检查工具函数
check_environment() {
    echo "🔧 环境检查..."
    
    # 检查目录
    if [ ! -d "$TACLEBENCH_DIR" ]; then
        echo "❌ TACLeBench目录不存在: $TACLEBENCH_DIR"
        exit 1
    fi
    
    cd "$TACLEBENCH_DIR"
    
    # 检查makefile
    if [ ! -f "makefile" ]; then
        echo "❌ makefile不存在"
        exit 1
    fi
    
    # 检查基本工具
    for tool in gcc make time; do
        if ! command -v "$tool" &> /dev/null; then
            echo "❌ 缺少工具: $tool"
            exit 1
        fi
    done
    
    echo "✅ 环境检查通过"
}

# 编译程序函数
compile_programs() {
    echo "🔨 编译测试程序..."
    echo "💡 跳过parallel类程序(编译复杂，需要特定环境)"
    
    echo "编译 kernel 类程序..."
    make kernel-host >/dev/null 2>&1 || echo "⚠️ 部分kernel程序编译失败"
    
    if [ "$SCRIPT_MODE" = "full" ]; then
        echo "编译 sequential 类程序..."
        make sequential-host >/dev/null 2>&1 || echo "⚠️ 部分sequential程序编译失败"
        
        echo "编译 app 类程序..."
        make app-host >/dev/null 2>&1 || echo "⚠️ 部分app程序编译失败"
    fi
    
    echo "✅ 编译完成"
}

# 快速测试关键程序
quick_test() {
    echo "⚡ 快速测试模式"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # 测试几个关键程序
    local test_programs=(
        "bench/kernel/matrix1/matrix1.host:矩阵运算"
        "bench/kernel/sha/sha.host:SHA加密"
        "bench/kernel/fft/fft.host:FFT变换"
        "bench/kernel/quicksort/quicksort.host:快速排序"
    )
    
    local success_count=0
    local total_time=0
    
    for prog_info in "${test_programs[@]}"; do
        IFS=':' read -r prog_path prog_name <<< "$prog_info"
        
        if [ -f "$prog_path" ]; then
            echo -n "🧪 测试 $prog_name ... "
            
            # 运行3次取平均值
            local times=()
            for i in {1..3}; do
                start_time=$(date +%s.%N)
                if timeout 10s "$prog_path" >/dev/null 2>&1; then
                    end_time=$(date +%s.%N)
                    duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "0")
                    times+=("$duration")
                else
                    echo "失败 ❌"
                    continue 2
                fi
            done
            
            # 计算平均时间
            local sum=0
            for time in "${times[@]}"; do
                sum=$(echo "$sum + $time" | bc -l 2>/dev/null || echo "$sum")
            done
            local avg=$(echo "scale=6; $sum / 3" | bc -l 2>/dev/null || echo "0")
            
            printf "%.3fs ✅\n" "$avg"
            success_count=$((success_count + 1))
            total_time=$(echo "$total_time + $avg" | bc -l 2>/dev/null || echo "$total_time")
        else
            echo "🧪 测试 $prog_name ... 未编译 ⚠️"
        fi
    done
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "📊 快速测试结果:"
    echo "  ✅ 成功测试: $success_count/4 个程序"
    printf "  ⏱️  总执行时间: %.3fs\n" "$total_time"
    echo "  🖥️  当前调度器: 系统默认 (CFS)"
    echo "  📅 测试时间: $(date)"
}

# 完整测试所有程序
full_test() {
    echo "🚀 完整测试模式"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # 创建结果目录
    mkdir -p "$RESULTS_DIR"
    local result_file="$RESULTS_DIR/cfs_baseline_$TEST_DATE.log"
    
    echo "📊 结果将保存到: $result_file"
    
    # 查找所有可用程序
    local all_programs=$(find bench -name "*.host" -type f 2>/dev/null | sort)
    
    if [ -z "$all_programs" ]; then
        echo "❌ 没有找到可用的测试程序"
        exit 1
    fi
    
    local total_programs=$(echo "$all_programs" | wc -l)
    echo "🔍 找到 $total_programs 个测试程序"
    
    # 初始化结果文件
    cat > "$result_file" << EOF
TACLeBench CFS基准测试报告
==========================
测试时间: $(date)
内核版本: $(uname -r)
调度器: 系统默认 (CFS)
测试机器: $(hostname)
CPU信息: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
CPU核心数: $(nproc)
总程序数: $total_programs

程序列表:
EOF
    
    echo "$all_programs" | sed 's/^/  /' >> "$result_file"
    echo "" >> "$result_file"
    
    # 测试统计
    local success_count=0
    local fail_count=0
    local total_time=0
    local program_count=0
    
    # 逐个测试程序
    for prog in $all_programs; do
        program_count=$((program_count + 1))
        local name=$(basename "$prog" .host)
        local category=$(echo "$prog" | cut -d'/' -f2)
        
        echo -n "[$program_count/$total_programs] 测试 $name ($category) ... "
        
        # 运行5次取最佳时间
        local best_time=999999
        local run_success=false
        
        for i in {1..5}; do
            start_time=$(date +%s.%N)
            if timeout 15s "$prog" >/dev/null 2>&1; then
                end_time=$(date +%s.%N)
                duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "999999")
                if (( $(echo "$duration < $best_time" | bc -l 2>/dev/null || echo 0) )); then
                    best_time=$duration
                    run_success=true
                fi
            fi
        done
        
        if [ "$run_success" = true ]; then
            printf "%.6fs ✅\n" "$best_time"
            success_count=$((success_count + 1))
            total_time=$(echo "$total_time + $best_time" | bc -l 2>/dev/null || echo "$total_time")
            
            # 记录到文件
            {
                echo "[$program_count] $name ($category)"
                printf "  执行时间: %.6f 秒\n" "$best_time"
                echo ""
            } >> "$result_file"
        else
            echo "失败 ❌"
            fail_count=$((fail_count + 1))
            echo "[$program_count] $name ($category) - 测试失败" >> "$result_file"
            echo "" >> "$result_file"
        fi
    done
    
    # 生成总结
    local avg_time=$(echo "scale=6; $total_time / $success_count" | bc -l 2>/dev/null || echo "0")
    
    {
        echo "=== 测试总结 ==="
        echo "测试完成时间: $(date)"
        echo "成功测试: $success_count 个"
        echo "失败测试: $fail_count 个"
        printf "总执行时间: %.3f 秒\n" "$total_time"
        printf "平均执行时间: %.6f 秒\n" "$avg_time"
        echo "成功率: $(echo "scale=1; $success_count * 100 / $total_programs" | bc -l)%"
    } >> "$result_file"
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "🎉 完整测试完成！"
    echo "📊 测试总结:"
    echo "  ✅ 成功: $success_count/$total_programs 个程序"
    echo "  ❌ 失败: $fail_count 个程序"
    printf "  ⏱️  总时间: %.3fs\n" "$total_time"
    printf "  📈 平均时间: %.6fs\n" "$avg_time"
    echo "  📁 详细结果: $result_file"
}

# 显示状态信息
show_status() {
    echo "================================================"
    echo "        开始执行 TACLeBench 测试"
    echo "================================================"
    echo "📅 测试时间: $(date)"
    echo "🖥️  当前内核: $(uname -r)"
    echo "⚙️  测试模式: $SCRIPT_MODE"
    echo "📁 工作目录: $TACLEBENCH_DIR"
    echo "🔧 当前调度器: 系统默认 (CFS)"
    echo ""
}

# 主程序
main() {
    # 如果没有提供参数，使用交互式选择
    if [ -z "$SCRIPT_MODE" ]; then
        select_mode
    fi
    
    case "$SCRIPT_MODE" in
        "help"|"-h"|"--help")
            show_help
            exit 0
            ;;
        "quick")
            show_status
            check_environment
            compile_programs
            echo ""
            quick_test
            echo ""
            echo "💡 提示: 运行 '$0 full' 进行完整测试"
            ;;
        "full")
            show_status
            check_environment
            compile_programs
            echo ""
            full_test
            ;;
        *)
            echo "❌ 未知模式: $SCRIPT_MODE"
            echo ""
            echo "🔧 可用模式:"
            echo "  quick  - 快速检查模式"
            echo "  full   - 完整测试模式"
            echo "  help   - 显示帮助信息"
            echo ""
            echo "💡 运行 '$0' 进入交互式选择模式"
            exit 1
            ;;
    esac
}

# 运行主程序
main
