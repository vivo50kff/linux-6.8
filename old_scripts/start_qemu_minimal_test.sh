#!/bin/bash

echo "Starting QEMU with minimal cache test initramfs..."

# 检查必要文件
if [ ! -f "arch/x86/boot/bzImage" ]; then
    echo "Error: bzImage not found. Please build the kernel first."
    exit 1
fi

if [ ! -f "initramfs_minimal.cpio.gz" ]; then
    echo "Error: initramfs_minimal.cpio.gz not found. Please run create_minimal_cache_test.sh first."
    exit 1
fi

echo "Starting QEMU with the following configuration:"
echo "- Kernel: arch/x86/boot/bzImage"
echo "- Initramfs: initramfs_minimal.cpio.gz"
echo "- CPUs: 4"
echo "- Memory: 512MB"
echo "- No KVM (for compatibility)"
echo ""
echo "Press Ctrl+A then X to exit QEMU"
echo ""

# 启动QEMU
qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -initrd initramfs_minimal.cpio.gz \
    -m 512 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 earlyprintk=serial,ttyS0,115200 loglevel=7 rdinit=/init" \
    -no-reboot
