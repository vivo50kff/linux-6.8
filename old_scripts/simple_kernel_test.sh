#!/bin/bash

# 最简单的内核启动测试 - 不使用 initramfs

KERNEL="arch/x86/boot/bzImage"

echo "=== 最简单的内核启动测试 ==="
echo "这个测试不使用 initramfs，只测试内核是否能启动"
echo ""

if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件不存在"
    exit 1
fi

echo "✓ 内核: $(file $KERNEL)"
echo ""

echo "启动参数："
echo "  - 只启动内核，不加载任何文件系统"
echo "  - 使用最基本的启动参数"
echo "  - 如果成功，会看到内核panic（这是正常的，因为没有init进程）"
echo ""

read -p "按 Enter 开始测试..."

echo "启动中..."
echo "预期：内核启动日志，最后出现 kernel panic（正常现象）"
echo "如果卡住没有输出，说明内核有问题"
echo ""

# 最简单的启动测试
timeout 30s qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -m 512 \
    -nographic \
    -append "console=ttyS0 panic=1" \
    -no-reboot \
    || echo "启动超时或出错"

echo ""
echo "测试完成"
