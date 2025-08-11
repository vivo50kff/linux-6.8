#!/bin/bash
# filepath: /home/mrma91/Yat_CAsched/linux-6.8-main/yat_test/check_kernel_perf_config.sh

echo "=== 内核性能监控配置检查 ==="
echo "内核版本: $(uname -r)"
echo ""

# 1. 检查当前内核配置
KERNEL_CONFIG=""
if [ -f "/proc/config.gz" ]; then
    echo "✓ 找到 /proc/config.gz"
    KERNEL_CONFIG="zcat /proc/config.gz"
elif [ -f "/boot/config-$(uname -r)" ]; then
    echo "✓ 找到 /boot/config-$(uname -r)"
    KERNEL_CONFIG="cat /boot/config-$(uname -r)"
elif [ -f "/lib/modules/$(uname -r)/build/.config" ]; then
    echo "✓ 找到内核源码配置文件"
    KERNEL_CONFIG="cat /lib/modules/$(uname -r)/build/.config"
else
    echo "✗ 找不到内核配置文件"
    echo "请检查以下位置："
    echo "  - /proc/config.gz"
    echo "  - /boot/config-$(uname -r)"
    echo "  - /lib/modules/$(uname -r)/build/.config"
    exit 1
fi

echo "使用配置文件: $KERNEL_CONFIG"
echo ""

# 2. 检查必需的性能监控配置项
echo "=== 检查性能监控相关配置 ==="

# 定义需要检查的配置项
declare -A REQUIRED_CONFIGS=(
    ["CONFIG_PERF_EVENTS"]="性能事件支持 (必需)"
    ["CONFIG_HAVE_PERF_EVENTS"]="硬件性能事件支持"
    ["CONFIG_PERF_COUNTERS"]="性能计数器支持"
    ["CONFIG_HAVE_PERF_EVENTS_NMI"]="NMI性能事件支持"
    ["CONFIG_DEBUG_PERF_USE_VMALLOC"]="perf调试支持"
    ["CONFIG_HW_PERF_EVENTS"]="硬件性能事件"
    ["CONFIG_PERF_EVENT_IOC_SET_FILTER"]="perf事件过滤支持"
)

declare -A ARCH_CONFIGS=(
    ["CONFIG_X86_LOCAL_APIC"]="本地APIC支持 (x86)"
    ["CONFIG_X86_PERF_CTRS"]="x86性能计数器"
)

declare -A OPTIONAL_CONFIGS=(
    ["CONFIG_PROFILING"]="性能分析支持"
    ["CONFIG_OPROFILE"]="OProfile支持"
    ["CONFIG_KPROBES"]="内核探针支持"
    ["CONFIG_TRACING"]="跟踪支持"
    ["CONFIG_FTRACE"]="函数跟踪支持"
    ["CONFIG_DYNAMIC_FTRACE"]="动态函数跟踪"
)

check_config() {
    local config_name="$1"
    local description="$2"
    local status="未找到"
    local color="\033[0;31m" # 红色
    
    if $KERNEL_CONFIG | grep -q "^${config_name}=y"; then
        status="已启用"
        color="\033[0;32m" # 绿色
    elif $KERNEL_CONFIG | grep -q "^${config_name}=m"; then
        status="模块"
        color="\033[0;33m" # 黄色
    elif $KERNEL_CONFIG | grep -q "^# ${config_name} is not set"; then
        status="已禁用"
        color="\033[0;31m" # 红色
    fi
    
    echo -e "${color}${config_name}: ${status}${NC} - ${description}"
}

NC='\033[0m' # No Color

echo "必需配置项："
for config in "${!REQUIRED_CONFIGS[@]}"; do
    check_config "$config" "${REQUIRED_CONFIGS[$config]}"
done

echo ""
echo "架构相关配置项："
for config in "${!ARCH_CONFIGS[@]}"; do
    check_config "$config" "${ARCH_CONFIGS[$config]}"
done

echo ""
echo "可选配置项："
for config in "${!OPTIONAL_CONFIGS[@]}"; do
    check_config "$config" "${OPTIONAL_CONFIGS[$config]}"
done

echo ""

# 3. 检查运行时设置
echo "=== 检查运行时设置 ==="

if [ -f "/proc/sys/kernel/perf_event_paranoid" ]; then
    paranoid_level=$(cat /proc/sys/kernel/perf_event_paranoid)
    echo "perf_event_paranoid 级别: $paranoid_level"
    case $paranoid_level in
        -1)
            echo "  含义: 允许所有用户访问所有性能事件"
            ;;
        0)
            echo "  含义: 允许普通用户访问用户空间性能事件"
            ;;
        1)
            echo "  含义: 允许普通用户访问部分性能事件"
            ;;
        2|*)
            echo "  含义: 只允许root用户访问性能事件"
            echo "  建议: 降低此级别以允许普通用户使用perf"
            ;;
    esac
else
    echo "✗ 找不到 /proc/sys/kernel/perf_event_paranoid"
fi

if [ -f "/proc/sys/kernel/kptr_restrict" ]; then
    kptr_level=$(cat /proc/sys/kernel/kptr_restrict)
    echo "kptr_restrict 级别: $kptr_level"
    if [ "$kptr_level" -gt 0 ]; then
        echo "  注意: 内核指针访问受限，可能影响某些perf功能"
    fi
fi

echo ""

# 4. 测试性能事件
echo "=== 测试性能事件可用性 ==="

# 查找perf工具
PERF_CMD=""
if [ -f "/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf" ]; then
    PERF_CMD="/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf"
    echo "使用编译的perf工具: $PERF_CMD"
elif command -v perf >/dev/null 2>&1; then
    PERF_CMD="perf"
    echo "使用系统perf工具: $PERF_CMD"
else
    echo "✗ 找不到perf工具"
fi

if [ -n "$PERF_CMD" ]; then
    echo ""
    echo "测试基础软件事件："
    if $PERF_CMD stat -e task-clock,context-switches,page-faults -- echo "test" >/dev/null 2>&1; then
        echo "✓ 软件事件工作正常"
    else
        echo "✗ 软件事件测试失败"
    fi
    
    echo ""
    echo "测试硬件事件："
    if $PERF_CMD stat -e cycles,instructions -- echo "test" >/dev/null 2>&1; then
        echo "✓ 硬件事件工作正常"
    else
        echo "✗ 硬件事件测试失败"
    fi
    
    echo ""
    echo "测试缓存事件："
    if $PERF_CMD stat -e cache-references,cache-misses -- echo "test" >/dev/null 2>&1; then
        echo "✓ 缓存事件工作正常"
    else
        echo "✗ 缓存事件测试失败"
    fi
fi

echo ""

# 5. 生成建议
echo "=== 配置建议 ==="

echo "如果性能事件不工作，请检查以下配置："
echo ""
echo "1. 重新配置内核时确保启用这些选项："
echo "   CONFIG_PERF_EVENTS=y"
echo "   CONFIG_HAVE_PERF_EVENTS=y" 
echo "   CONFIG_HW_PERF_EVENTS=y"
echo "   CONFIG_X86_LOCAL_APIC=y  # x86架构"
echo ""

echo "2. 运行时调整参数："
echo "   echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid"
echo "   echo 0 | sudo tee /proc/sys/kernel/kptr_restrict"
echo ""

echo "3. 重新编译内核:"
echo "   cd /home/mrma91/Yat_CAsched/linux-6.8-main"
echo "   make menuconfig  # 启用上述配置项"
echo "   make -j\$(nproc)"
echo "   sudo make modules_install install"
echo ""