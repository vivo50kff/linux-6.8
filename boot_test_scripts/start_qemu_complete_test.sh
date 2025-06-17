#!/bin/bash

echo "=== Yat_Casched 完整测试环境启动脚本 ==="

# 检查必要文件
KERNEL_IMAGE="arch/x86/boot/bzImage"  # 使用刚编译的内核
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    echo "请先编译内核: make -j\$(nproc)"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "✗ 错误: initramfs 未找到: $INITRAMFS"
    echo "请先运行: ./boot_test_scripts/create_initramfs_complete.sh"
    exit 1
fi

echo "✓ 检查通过，准备启动测试环境"
echo ""
echo "启动配置:"
echo "  内核镜像: $KERNEL_IMAGE"
echo "  文件系统: $INITRAMFS"
echo "  CPU核心: 4"
echo "  内存: 1GB"
echo "  架构: x86_64"
echo ""
echo "测试功能:"
echo "  ✓ 调度器真实性验证"
echo "  ✓ 缓存感知性能测试"
echo "  ✓ 多进程多线程测试"
echo "  ✓ CPU亲和性验证"
echo ""
echo "控制命令:"
echo "  退出QEMU: Ctrl+A 然后按 X"
echo "  强制退出: Ctrl+C"
echo ""

# 启动QEMU
echo "正在启动 QEMU 虚拟机..."
echo "=================================================================="

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -m 2G \
    -smp 4 \
    -cpu Nehalem \
    -machine pc,accel=tcg \
    -nographic \
    -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 nr_cpus=4 initcall_blacklist=do_init_real_mode" \
    -no-reboot

echo ""
echo "=================================================================="
echo "QEMU 已退出。测试会话结束。"
echo "=================================================================="
