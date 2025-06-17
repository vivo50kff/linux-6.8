#!/bin/bash

echo "=== Yat_Casched 调度器完整测试方案 ==="
echo "编译时间: $(date)"
echo ""

# 检查内核文件
KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

echo "1. 检查编译结果"
echo "========================================================"
if [ -f "$KERNEL_IMAGE" ]; then
    echo "✓ 内核镜像存在: $KERNEL_IMAGE"
    echo "  文件大小: $(du -h $KERNEL_IMAGE | cut -f1)"
    echo "  文件信息: $(file $KERNEL_IMAGE)"
else
    echo "✗ 内核镜像不存在: $KERNEL_IMAGE"
    exit 1
fi

if [ -f "$INITRAMFS" ]; then
    echo "✓ initramfs存在: $INITRAMFS"
else
    echo "✗ initramfs不存在，需要创建"
    echo "  运行: ./boot_test_scripts/create_initramfs_complete.sh"
    exit 1
fi

echo ""
echo "2. 启动测试环境"
echo "========================================================"
echo "使用绕过实模式的安全启动参数..."
echo "预期结果: 系统稳定启动，但可能只有单核"
echo ""

# 使用我们知道有效的参数
qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 initcall_blacklist=do_init_real_mode" \
    -no-reboot

echo ""
echo "测试完成！"
