#!/bin/bash

echo "=== QEMU 详细调试启动 ==="
echo "尝试多种启动方式来诊断问题..."

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs.cpio.gz"

echo ""
echo "1. 测试基本内核启动（无 initramfs）..."
echo "   如果这里卡住，说明内核本身有问题"
echo "   按 Ctrl+C 中断测试"
echo ""

timeout 8s qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -m 512 \
    -nographic \
    -serial mon:stdio \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 debug loglevel=8 nokaslr" \
    -no-reboot || echo "超时或失败"

echo ""
echo "2. 如果上面有输出但卡住，说明内核启动了但需要 init..."
echo "   现在测试带 initramfs 的启动"
echo ""

timeout 10s qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 512 \
    -nographic \
    -serial mon:stdio \
    -append "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 debug loglevel=8 init=/init" \
    -no-reboot || echo "超时或失败"

echo ""
echo "3. 如果还是没反应，尝试最简单的启动..."
echo ""

timeout 5s qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -nographic \
    -append "console=ttyS0" \
    -m 256 || echo "最简单启动也失败"

echo ""
echo "=== 诊断完成 ==="
echo "如果所有测试都没有任何输出，可能的原因："
echo "1. 内核文件损坏"
echo "2. QEMU 配置问题"
echo "3. 内核配置缺少必要的驱动"
echo "4. 虚拟机平台问题"
