#!/bin/bash

echo "=== 详细 QEMU 调试启动 ==="
echo "测试1: 最简单的内核启动 (无 initramfs)"
echo "按 Ctrl+C 可以中断"
echo ""

# 使用非常详细的调试参数
qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -m 512 \
    -smp 1 \
    -machine q35 \
    -cpu qemu64 \
    -nographic \
    -monitor stdio \
    -serial mon:stdio \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 loglevel=8 debug ignore_loglevel panic=10 rdinit=/bin/sh" \
    -no-reboot \
    -no-shutdown
