#!/bin/bash

echo "=== 测试不绕过实模式的版本 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"  
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

echo "不使用 initcall_blacklist，看是否还会崩溃..."
echo "如果崩溃，说明实模式问题没有真正解决"
echo "如果不崩溃，说明我们的修改确实修复了问题"
echo ""

timeout 30 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot

echo ""
echo "测试完成"
