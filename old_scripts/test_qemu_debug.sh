#!/bin/bash

# 将 QEMU 输出保存到文件进行调试

echo "=== QEMU 输出调试测试 ==="

# 测试1：将所有输出保存到文件
echo "测试1：保存输出到文件..."
timeout 10s qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -nographic \
    -accel tcg \
    -m 512 \
    -append "console=ttyS0,115200 panic=1 loglevel=8" \
    -serial file:qemu_output.log \
    -monitor none \
    > qemu_stdout.log 2> qemu_stderr.log

echo "检查输出文件："
echo "=== qemu_stdout.log ==="
if [ -f qemu_stdout.log ]; then
    cat qemu_stdout.log
else
    echo "文件不存在"
fi

echo ""
echo "=== qemu_stderr.log ==="
if [ -f qemu_stderr.log ]; then
    cat qemu_stderr.log
else
    echo "文件不存在"
fi

echo ""
echo "=== qemu_output.log (serial output) ==="
if [ -f qemu_output.log ]; then
    cat qemu_output.log
else
    echo "文件不存在"
fi

echo ""
echo "测试完成"
