#!/bin/bash

echo "=== 多核CPU启动问题诊断脚本 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo "尝试不同的QEMU配置以解决CPU启动问题..."

# 配置1: 最简单的多核配置
echo "======================================="
echo "配置1: 简化的4核配置"
echo "======================================="

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 rdinit=/init panic=1 initcall_blacklist=do_init_real_mode" \
    -no-reboot
