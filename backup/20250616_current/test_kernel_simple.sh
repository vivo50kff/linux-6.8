#!/bin/bash

# 简单的内核启动测试 - 不使用 KVM
echo "=== 简单内核启动测试（无KVM）==="

# 测试1：最基本的内核启动
echo "测试1：基本内核启动（10秒）"
timeout 10s qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -nographic \
    -accel tcg \
    -m 512 \
    -append "console=ttyS0,115200 panic=1" \
    -serial stdio || echo "测试1结束"

echo ""
echo "================================================"
echo ""

# 测试2：使用 initramfs
echo "测试2：使用 initramfs 启动（15秒）"
if [ -f "initramfs.cpio.gz" ]; then
    timeout 15s qemu-system-x86_64 \
        -kernel arch/x86/boot/bzImage \
        -initrd initramfs.cpio.gz \
        -nographic \
        -accel tcg \
        -m 512 \
        -append "console=ttyS0,115200 init=/init" \
        -serial stdio || echo "测试2结束"
else
    echo "initramfs.cpio.gz 不存在，跳过测试2"
fi

echo ""
echo "================================================"
echo ""

# 测试3：使用图形界面（如果可能）
echo "测试3：尝试图形界面启动（5秒）"
timeout 5s qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -accel tcg \
    -m 512 \
    -append "console=tty0" 2>/dev/null || echo "测试3结束"

echo ""
echo "所有测试完成"
