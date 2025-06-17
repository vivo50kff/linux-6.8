#!/bin/bash

echo "Starting QEMU with Yat_Casched cache-aware test..."

# 不需要切换目录，假设从linux-6.8目录执行此脚本

# 检查必要文件
if [ ! -f "arch/x86/boot/bzImage" ]; then
    echo "Error: bzImage not found! Please compile kernel first."
    exit 1
fi

if [ ! -f "initramfs_cache_test.cpio.gz" ]; then
    echo "Error: initramfs_cache_test.cpio.gz not found! Please create initramfs first."
    exit 1
fi

echo "Kernel: arch/x86/boot/bzImage"
echo "Initramfs: initramfs_cache_test.cpio.gz"
echo "Starting QEMU..."
echo ""

# 启动QEMU
qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -initrd initramfs_cache_test.cpio.gz \
    -m 2G \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 rdinit=/init loglevel=4" \
    -no-reboot

echo ""
echo "QEMU session ended."
