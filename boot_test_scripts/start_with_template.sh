#!/bin/bash

echo "=== 基于工作模板的 Yat_Casched 内核启动脚本 ==="

# 检查必要文件
KERNEL_IMAGE="/home/lwd/桌面/linux-6.8/arch/x86/boot/bzImage"
DISK_IMAGE="/home/lwd/桌面/linux-6.8/boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "✗ 错误: initramfs 未找到: $DISK_IMAGE"
    echo "请先运行: ./boot_test_scripts/create_initramfs_complete.sh"
    exit 1
fi

echo "✓ 检查通过，准备启动..."
echo "  内核: $KERNEL_IMAGE"
echo "  文件系统: $DISK_IMAGE"
echo ""

# 方案1: 尝试使用KVM加速（更接近真实硬件）
echo "尝试方案1: KVM加速启动"
echo "=================================="

timeout 10s qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -m 2G \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$DISK_IMAGE" \
    -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7" \
    -nographic \
    -no-reboot 2>/dev/null

if [ $? -ne 0 ]; then
    echo ""
    echo "KVM不可用，尝试方案2: TCG模拟"
    echo "=================================="
    
    qemu-system-x86_64 \
        -cpu qemu64 \
        -smp 4 \
        -m 2G \
        -machine pc,accel=tcg \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 initcall_blacklist=do_init_real_mode" \
        -nographic \
        -no-reboot
fi

echo ""
echo "启动完成"
