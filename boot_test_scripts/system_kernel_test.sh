#!/bin/bash

echo "=== 系统内核对比测试 ==="

# 检查系统中的可用内核
echo "检查系统中可用的内核镜像："
ls -la /boot/vmlinuz-* 2>/dev/null || echo "未找到系统内核镜像"
echo ""

# 检查当前运行的内核版本
echo "当前运行的内核版本："
uname -r
echo ""

# 如果存在系统内核，测试它
SYSTEM_KERNEL=$(ls /boot/vmlinuz-* 2>/dev/null | head -1)
INITRD="boot_test_scripts/initramfs_complete.cpio.gz"

if [ -n "$SYSTEM_KERNEL" ] && [ -f "$INITRD" ]; then
    echo "测试系统内核的SMP启动能力..."
    echo "使用内核: $SYSTEM_KERNEL"
    echo "========================================================"
    
    timeout 30 qemu-system-x86_64 \
        -kernel "$SYSTEM_KERNEL" \
        -initrd "$INITRD" \
        -m 2G \
        -smp 4 \
        -cpu qemu64 \
        -machine pc,accel=tcg \
        -nographic \
        -append "console=ttyS0 rdinit=/init panic=1 maxcpus=4" \
        -no-reboot 2>&1 | grep -E "(Linux version|smp.*CPU|CPU.*failed|CPU.*alive|Brought up.*CPU|init_real_mode|Oops)" | head -10
        
    echo "系统内核测试完成"
else
    echo "未找到可用的系统内核进行对比测试"
fi

echo ""
echo "建议："
echo "1. 如果系统内核能正常启动多核，说明问题在我们编译的6.8内核"
echo "2. 如果系统内核也有问题，说明是QEMU/虚拟化环境问题"
echo "3. 可以考虑下载并测试其他版本的内核源码"
