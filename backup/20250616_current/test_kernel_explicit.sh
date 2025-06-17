#!/bin/bash

# 明确指定内核启动的测试

echo "=== 明确指定内核启动测试 ==="

# 清理之前的日志文件
rm -f qemu_*.log

echo "测试：明确指定从内核启动..."
timeout 15s qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -accel tcg \
    -m 512 \
    -smp 1 \
    -nographic \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 loglevel=8 debug initcall_debug" \
    -serial file:kernel_output.log \
    -no-reboot \
    -no-shutdown \
    > qemu_stdout2.log 2> qemu_stderr2.log

echo ""
echo "检查输出："
echo "=== QEMU stderr ==="
[ -f qemu_stderr2.log ] && cat qemu_stderr2.log

echo ""
echo "=== 内核输出 ==="
[ -f kernel_output.log ] && cat kernel_output.log | head -20

echo ""
echo "=== QEMU stdout ==="
[ -f qemu_stdout2.log ] && cat qemu_stdout2.log
