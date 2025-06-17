#!/bin/bash

echo "=== 简化版多核测试脚本 ==="

# 检查必要文件
KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "✗ 错误: initramfs 未找到: $INITRAMFS"
    exit 1
fi

echo "尝试方案1: KVM + host CPU"
echo "======================================="

timeout 10 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu host \
    -machine q35,accel=kvm \
    -enable-kvm \
    -nographic \
    -append "console=ttyS0 rdinit=/init panic=1" \
    -no-reboot 2>/dev/null

if [ $? -ne 0 ]; then
    echo ""
    echo "KVM失败，尝试方案2: TCG + Nehalem CPU"
    echo "======================================="
    
    qemu-system-x86_64 \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$INITRAMFS" \
        -m 1G \
        -smp 4 \
        -cpu Nehalem \
        -machine pc,accel=tcg \
        -nographic \
        -append "console=ttyS0 rdinit=/init panic=1 maxcpus=4 nr_cpus=4" \
        -no-reboot
fi
