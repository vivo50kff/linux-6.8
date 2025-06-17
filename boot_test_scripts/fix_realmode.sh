#!/bin/bash

echo "=== 尝试修复 init_real_mode 页面错误的多种方案 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo ""
echo "方案1: 使用更多内存 + 不同CPU模型"
echo "=================================================="
echo "按Enter继续测试..."
read

timeout 25 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 4G \
    -smp 4 \
    -cpu core2duo \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot

echo ""
echo "方案2: 使用Q35芯片组 + 更简单CPU"
echo "=================================================="
echo "按Enter继续测试..."
read

timeout 25 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu qemu64 \
    -machine q35 \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 mem=2048M" \
    -no-reboot

echo ""
echo "方案3: 修改内存布局参数"
echo "=================================================="
echo "按Enter继续测试..."
read

timeout 25 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 memmap=128K#96K" \
    -no-reboot

echo ""
echo "所有方案测试完成"
