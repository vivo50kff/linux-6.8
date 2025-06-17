#!/bin/bash

echo "=== 最小化多核配置测试 ==="

# 检查必要文件
KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

echo "启动最小化配置..."

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4,cores=4,threads=1,sockets=1 \
    -cpu qemu64 \
    -machine isapc \
    -nographic \
    -append "console=ttyS0 rdinit=/init loglevel=3 acpi=off noapic nolapic" \
    -no-reboot
