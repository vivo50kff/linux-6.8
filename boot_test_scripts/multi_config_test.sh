#!/bin/bash

echo "=== 多内核版本SMP启动对比测试 ==="
echo "测试目标：确定是否是内核6.8特有的实模式初始化问题"
echo ""

# 测试不同的QEMU配置参数
KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo "当前内核信息："
file "$KERNEL_IMAGE" | head -1
echo ""

# 函数：测试特定配置
test_config() {
    local config_name="$1"
    local machine="$2"
    local cpu="$3"
    local memory="$4"
    local extra_args="$5"
    
    echo "========================================================"
    echo "测试配置: $config_name"
    echo "机器类型: $machine, CPU: $cpu, 内存: $memory"
    echo "额外参数: $extra_args"
    echo "========================================================"
    
    timeout 20 qemu-system-x86_64 \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$INITRAMFS" \
        -m "$memory" \
        -smp 4 \
        -cpu "$cpu" \
        -machine "$machine" \
        -nographic \
        -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 $extra_args" \
        -no-reboot 2>&1 | grep -E "(Linux version|smp.*CPU|CPU.*failed|CPU.*alive|Brought up.*CPU|init_real_mode|Oops|panic)" | head -15
    
    echo "配置测试完成"
    echo ""
}

echo "开始多配置对比测试..."
echo ""

# 测试1: 基本配置（已知会崩溃）
test_config "基本pc机器" "pc" "qemu64" "1G" ""

# 测试2: 老式i440FX + 486 CPU
test_config "i440FX+486" "pc,accel=tcg" "486" "1G" ""

# 测试3: Q35芯片组
test_config "Q35芯片组" "q35,accel=tcg" "qemu64" "1G" ""

# 测试4: 不同CPU模型
test_config "Pentium CPU" "pc,accel=tcg" "pentium" "1G" ""

# 测试5: 增加内存
test_config "更多内存" "pc,accel=tcg" "qemu64" "2G" ""

# 测试6: 禁用ACPI
test_config "禁用ACPI" "pc,accel=tcg" "qemu64" "1G" "acpi=off"

# 测试7: 启用更多调试
test_config "更多调试信息" "pc,accel=tcg" "qemu64" "1G" "debug ignore_loglevel"

# 测试8: 尝试旧式启动
test_config "旧式启动" "pc,accel=tcg" "qemu64" "1G" "nokaslr noapic"

echo "========================================================"
echo "对比测试完成"
echo "如果某个配置能成功启动多核，我们就知道问题所在了！"
echo "========================================================"
