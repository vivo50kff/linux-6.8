#!/bin/bash

echo "=== 单核模式测试 Yat_Casched 调度器 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo "使用单核模式避免CPU启动等待..."
echo "这样可以快速测试调度器功能"
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 1 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 initcall_blacklist=do_init_real_mode" \
    -no-reboot

echo "单核测试完成"
