#!/bin/bash

# QEMU 启动脚本 - 不使用 KVM（适用于虚拟机环境）
# 专门用于测试 Yat_Casched 调度器

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs.cpio.gz"

echo "=== Yat_Casched 调度器 QEMU 测试启动脚本（无KVM）==="
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

echo "启动参数（无KVM）："
echo "  - 内存: 1GB"
echo "  - CPU: 2 核心"
echo "  - 加速: TCG（软件模拟）"
echo "  - 控制台: 串口"
echo ""

echo "=== 启动说明 ==="
echo "1. 不使用 KVM 加速（适用于虚拟机环境）"
echo "2. 如果看到内核启动信息，说明正常"
echo "3. 等待看到 shell 提示符"
echo "4. 退出 QEMU: 按 Ctrl+A 然后按 X"
echo ""

read -p "按 Enter 键启动 QEMU，或 Ctrl+C 取消..."

echo "启动中（无KVM）..."
echo ""

# 启动 QEMU - 使用 TCG 加速（不使用 KVM）
# VSCode 环境适配版本
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 1024 \
    -smp 2 \
    -nographic \
    -accel tcg \
    -cpu qemu64 \
    -append "console=ttyS0,115200 init=/init loglevel=7 earlyprintk=serial" \
    -no-reboot \
    -serial mon:stdio

echo ""
echo "QEMU 已退出"
