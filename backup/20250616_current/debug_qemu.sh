#!/bin/bash

# QEMU 调试启动脚本 - 查看详细的启动过程和错误信息

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs.cpio.gz"

echo "=== QEMU 调试模式启动 ==="
echo ""

# 检查文件
if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件不存在"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误：initramfs 文件不存在"
    exit 1
fi

echo "✓ 内核: $(ls -lh $KERNEL)"
echo "✓ initramfs: $(ls -lh $INITRAMFS)"
echo ""

echo "启动参数说明："
echo "  -d int,cpu_reset    : 显示中断和CPU重置信息"
echo "  -monitor stdio      : 在控制台显示 QEMU 监视器"
echo "  -serial file:qemu.log : 将串口输出保存到文件"
echo "  -kernel ... -append : 内核启动参数"
echo ""

echo "如果启动卡住，您可以："
echo "1. 在 QEMU 监视器中输入 'info registers' 查看CPU状态"
echo "2. 输入 'info cpus' 查看CPU信息"
echo "3. 输入 'quit' 退出 QEMU"
echo "4. 按 Ctrl+C 强制退出"
echo ""

read -p "按 Enter 开始调试启动..."

echo "启动中，请等待..."

# 启动 QEMU 带调试信息
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 1024 \
    -smp 2 \
    -machine pc \
    -cpu qemu64 \
    -nographic \
    -monitor stdio \
    -serial file:qemu.log \
    -d int,cpu_reset \
    -append "console=ttyS0,115200 init=/init debug earlyprintk=serial,ttyS0,115200 loglevel=8" \
    -no-reboot \
    -no-shutdown

echo ""
echo "QEMU 已退出"
echo ""
echo "查看日志文件："
if [ -f "qemu.log" ]; then
    echo "=== QEMU 串口日志 (最后20行) ==="
    tail -20 qemu.log
else
    echo "没有生成日志文件"
fi
