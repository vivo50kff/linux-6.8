#!/bin/bash

echo "=== 详细的内核启动诊断测试 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"
SYSTEM_KERNEL="/boot/vmlinuz-6.11.0-25-generic"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

echo "测试1: 我们编译的6.8内核（详细输出）"
echo "========================================================"
echo "预期看到: init_real_mode 崩溃"

timeout 15 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
    -no-reboot

echo ""
echo "========================================================"
echo "测试2: 系统6.11内核（详细输出）"
echo "========================================================"
echo "预期看到: 成功的多核启动？"

if [ -f "$SYSTEM_KERNEL" ]; then
    timeout 15 qemu-system-x86_64 \
        -kernel "$SYSTEM_KERNEL" \
        -initrd "$INITRAMFS" \
        -m 1G \
        -smp 4 \
        -cpu qemu64 \
        -machine pc,accel=tcg \
        -nographic \
        -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4" \
        -no-reboot
else
    echo "系统内核文件不存在: $SYSTEM_KERNEL"
fi

echo ""
echo "========================================================"
echo "测试3: 6.8内核 + 绕过实模式（参考对比）"
echo "========================================================"
echo "预期看到: 稳定启动但只有单核"

timeout 15 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 1G \
    -smp 4 \
    -cpu qemu64 \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 initcall_blacklist=do_init_real_mode" \
    -no-reboot

echo ""
echo "对比测试完成！"
