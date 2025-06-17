#!/bin/bash

echo "=== 直接对比测试：实模式初始化对多核启动的影响 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo ""
echo "测试1: 不绕过实模式初始化"
echo "========================================================"
echo "预期: 可能看到多个CPU启动，然后遇到 init_real_mode 崩溃"
echo "按 Enter 开始，或 Ctrl+C 取消"
read

timeout 30 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot

echo ""
echo "========================================================"
echo "测试2: 绕过实模式初始化"
echo "========================================================"
echo "预期: 稳定启动但可能只有1个CPU"
echo "按 Enter 开始，或 Ctrl+C 取消"
read

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 initcall_blacklist=do_init_real_mode" \
    -no-reboot

echo ""
echo "对比测试完成"
