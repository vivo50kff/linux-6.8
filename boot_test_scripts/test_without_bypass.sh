#!/bin/bash

echo "=== 测试不绕过实模式的版本 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

echo "这次不绕过 init_real_mode，看看是否能真正解决多核问题"
echo "如果崩溃，说明需要修复实模式代码"
echo "如果成功，说明多核启动需要实模式初始化"
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot

echo "测试完成"
