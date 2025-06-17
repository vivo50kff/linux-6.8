#!/bin/bash

# 简单的 QEMU 启动脚本，直接前台运行

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs_fixed.cpio.gz"

echo "=== 简单 QEMU 启动脚本 ==="
echo ""

# 检查必要文件
if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件 $KERNEL 不存在"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误：initramfs 文件 $INITRAMFS 不存在"
    exit 1
fi

echo "✓ 内核文件: $KERNEL ($(du -h $KERNEL | cut -f1))"
echo "✓ initramfs: $INITRAMFS ($(du -h $INITRAMFS | cut -f1))"
echo ""

echo "启动 QEMU（前台运行）..."
echo "提示：使用 Ctrl+A 然后 X 退出 QEMU"
echo ""

# 启动 QEMU
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 512M \
    -smp 2 \
    -nographic \
    -append "console=ttyS0 loglevel=7" \
    -no-reboot

echo ""
echo "QEMU 已退出"
