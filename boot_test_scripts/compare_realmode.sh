#!/bin/bash

echo "=== 对比测试：实模式初始化对多核启动的影响 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo ""
echo "测试1: 不绕过实模式初始化（观察CPU启动和可能的崩溃）"
echo "========================================================"
echo "关注关键词: smp, CPU, failed, alive, Brought up"
echo ""

timeout 25 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot > /tmp/test1.log 2>&1

echo "测试1完成，关键信息："
grep -E "(smp.*CPU|CPU.*failed|CPU.*alive|Brought up.*CPU|smpboot.*CPU)" /tmp/test1.log | head -10

echo ""
echo "========================================================"
echo "测试2: 绕过实模式初始化（观察CPU启动情况）"
echo "========================================================"
echo "关注关键词: smp, CPU, failed, alive, Brought up"
echo ""

timeout 25 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 initcall_blacklist=do_init_real_mode" \
    -no-reboot > /tmp/test2.log 2>&1

echo "测试2完成，关键信息："
grep -E "(smp.*CPU|CPU.*failed|CPU.*alive|Brought up.*CPU|smpboot.*CPU)" /tmp/test2.log | head -10

echo ""
echo "========================================================"
echo "对比结果分析："
echo "========================================================"
echo "测试1的CPU启动情况："
grep -E "(Brought up.*CPU)" /tmp/test1.log || echo "未找到CPU启动成功信息"

echo ""
echo "测试2的CPU启动情况："
grep -E "(Brought up.*CPU)" /tmp/test2.log || echo "未找到CPU启动成功信息"

echo ""
echo "完整日志已保存到 /tmp/test1.log 和 /tmp/test2.log"
