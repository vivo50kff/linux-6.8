#!/bin/bash
# filepath: /home/mrma91/Yat_CAsched/linux-6.8-main/yat_test/diagnose_perf.sh

echo "=== Perf性能监控诊断脚本 ==="
echo "诊断时间: $(date)"
echo ""

# 1. 检查基本环境
echo "1. 系统环境检查:"
echo "   操作系统: $(uname -a)"
echo "   虚拟化: $(systemd-detect-virt 2>/dev/null || echo '未知')"
echo "   CPU型号: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
echo "   CPU核心数: $(nproc)"
echo ""

# 2. 检查权限
echo "2. 权限检查:"
echo "   当前用户: $(whoami)"
echo "   perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"
echo "   是否为root: $([[ $EUID -eq 0 ]] && echo '是' || echo '否')"
echo ""

# 3. 检查perf工具
echo "3. Perf工具检查:"
if [ -f "/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf" ]; then
    PERF_CMD="/home/mrma91/Yat_CAsched/linux-6.8-main/tools/perf/perf"
    echo "   使用自编译perf: $PERF_CMD"
    echo "   版本: $($PERF_CMD --version 2>/dev/null || echo '无法获取版本')"
else
    PERF_CMD="perf"
    echo "   使用系统perf: $(which perf || echo '未找到')"
    echo "   版本: $(perf --version 2>/dev/null || echo '无法获取版本')"
fi
echo ""

# 4. 检查测试程序
echo "4. 测试程序检查:"
PROGRAM="./tacle_kernel_test_local"
if [ -f "$PROGRAM" ]; then
    echo "   程序存在: 是"
    echo "   文件大小: $(stat -c%s "$PROGRAM") 字节"
    echo "   是否可执行: $([[ -x "$PROGRAM" ]] && echo '是' || echo '否')"
    
    # 测试程序能否正常运行
    echo "   测试运行:"
    if timeout 5s "$PROGRAM" >/dev/null 2>&1; then
        echo "     程序运行正常"
    else
        echo "     程序运行异常或超时"
    fi
else
    echo "   程序不存在!"
    echo "   请运行: gcc -o tacle_kernel_test_local tacle_kernel_test_local.c"
    exit 1
fi
echo ""

# 5. 基本perf功能测试
echo "5. Perf基本功能测试:"
echo "   测试简单命令 (ls):"
if $PERF_CMD stat -e cycles -- ls >/tmp/perf_test.out 2>&1; then
    cycles=$(grep cycles /tmp/perf_test.out | awk '{print $1}' | tr -d ',')
    if [ -n "$cycles" ] && [ "$cycles" -gt 0 ]; then
        echo "     ✓ 成功获取到周期数: $cycles"
    else
        echo "     ✗ 周期数为0或无效"
        cat /tmp/perf_test.out
    fi
else
    echo "     ✗ perf stat 失败"
    cat /tmp/perf_test.out
fi
echo ""

# 6. 测试程序的perf监控
echo "6. 测试程序的Perf监控:"
echo "   监控测试程序:"
if $PERF_CMD stat -e cycles,instructions "$PROGRAM" >/tmp/perf_program_test.out 2>&1; then
    cycles=$(grep cycles /tmp/perf_program_test.out | awk '{print $1}' | tr -d ',')
    instructions=$(grep instructions /tmp/perf_program_test.out | awk '{print $1}' | tr -d ',')
    
    echo "     原始输出:"
    cat /tmp/perf_program_test.out
    echo ""
    
    if [ -n "$cycles" ] && [ "$cycles" -gt 0 ]; then
        echo "     ✓ 成功获取周期数: $cycles"
    else
        echo "     ✗ 周期数异常: '$cycles'"
    fi
    
    if [ -n "$instructions" ] && [ "$instructions" -gt 0 ]; then
        echo "     ✓ 成功获取指令数: $instructions"
    else
        echo "     ✗ 指令数异常: '$instructions'"
    fi
else
    echo "     ✗ 监控测试程序失败"
    cat /tmp/perf_program_test.out
fi
echo ""

# 7. 检查可用事件
echo "7. 可用事件检查:"
echo "   基本事件支持:"
for event in cycles instructions cache-references cache-misses; do
    if $PERF_CMD list | grep -q "^  $event "; then
        echo "     ✓ $event: 支持"
    else
        echo "     ✗ $event: 不支持"
    fi
done
echo ""

# 8. VMware特定检查
echo "8. VMware环境检查:"
if systemd-detect-virt | grep -q vmware; then
    echo "   运行在VMware环境中"
    echo "   检查虚拟化CPU特性:"
    if grep -q "hypervisor" /proc/cpuinfo; then
        echo "     ✓ 检测到hypervisor标志"
    else
        echo "     ✗ 未检测到hypervisor标志"
    fi
    
    echo "   VMware工具状态:"
    if command -v vmware-toolbox-cmd >/dev/null; then
        echo "     VMware工具: 已安装"
    else
        echo "     VMware工具: 未安装或不可用"
    fi
else
    echo "   不在VMware环境中"
fi
echo ""

# 9. 内核配置检查
echo "9. 内核配置检查:"
if [ -f "/boot/config-$(uname -r)" ]; then
    echo "   内核配置文件: 存在"
    
    # 检查关键的perf相关配置
    config_file="/boot/config-$(uname -r)"
    for config in CONFIG_PERF_EVENTS CONFIG_HW_PERF_EVENTS CONFIG_X86_PMU CONFIG_PERF_EVENTS_AMD_POWER; do
        if grep -q "^${config}=y" "$config_file"; then
            echo "     ✓ $config: 启用"
        elif grep -q "^${config}=m" "$config_file"; then
            echo "     ◐ $config: 模块"
        else
            echo "     ✗ $config: 禁用或不存在"
        fi
    done
else
    echo "   内核配置文件: 不存在"
fi
echo ""

echo "=== 诊断完成 ==="
echo ""

# 10. 给出建议
echo "=== 问题诊断建议 ==="
if [ -f "/tmp/perf_program_test.out" ]; then
    if grep -q "not supported" /tmp/perf_program_test.out; then
        echo "❌ 问题: perf事件不被支持"
        echo "   原因: VMware虚拟环境限制"
        echo "   解决方案: 1) 在VMware中启用性能计数器虚拟化"
        echo "            2) 或在物理机上测试"
    elif grep -q "Access.*limited" /tmp/perf_program_test.out; then
        echo "❌ 问题: 权限限制"
        echo "   解决方案: sudo sysctl kernel.perf_event_paranoid=-1"
    elif ! grep -E "[0-9,]+" /tmp/perf_program_test.out >/dev/null; then
        echo "❌ 问题: 程序可能没有实际执行工作"
        echo "   原因: 测试程序可能执行时间太短或没有足够的计算"
        echo "   解决方案: 检查程序是否正常运行并消耗CPU"
    else
        echo "✓ 基本功能正常，但可能是数据提取问题"
        echo "   解决方案: 检查脚本中的数据解析逻辑"
    fi
else
    echo "❌ 问题: perf完全无法运行"
    echo "   需要进一步调查perf工具本身的问题"
fi