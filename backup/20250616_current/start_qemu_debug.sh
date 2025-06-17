#!/bin/bash

# 简化的 QEMU 测试启动脚本，带调试信息

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs.cpio.gz"

echo "=== Yat_Casched 调度器 QEMU 调试启动 ==="

# 检查文件
if [ ! -f "$KERNEL" ] || [ ! -f "$INITRAMFS" ]; then
    echo "错误：缺少必要文件"
    exit 1
fi

echo "启动 QEMU..."
echo "如果看到内核启动信息后卡住，请等待几秒钟"
echo "退出方式: Ctrl+A, 然后按 X"
echo ""

# 使用更简单的参数启动
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 1024 \
    -smp 2 \
    -nographic \
    -append "console=ttyS0,115200 init=/init loglevel=7 debug" \
    -no-reboot \
    -serial stdio
