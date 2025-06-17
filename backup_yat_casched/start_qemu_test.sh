#!/bin/bash

# QEMU 启动脚本 - 测试 Yat_Casched 调度器
# 使用方法: ./start_qemu_test.sh

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs.cpio.gz"

echo "=== Yat_Casched 调度器 QEMU 测试启动脚本 ==="
echo ""

# 检查必要文件
if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件 $KERNEL 不存在"
    echo "请先编译内核: make -j\$(nproc)"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误：initramfs 文件 $INITRAMFS 不存在"
    echo "请先运行: ./create_simple_initramfs.sh"
    exit 1
fi

echo "✓ 内核文件: $KERNEL ($(du -h $KERNEL | cut -f1))"
echo "✓ initramfs: $INITRAMFS ($(du -h $INITRAMFS | cut -f1))"
echo ""

# 检查 QEMU 是否安装
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "错误：qemu-system-x86_64 未安装"
    echo "请安装: sudo apt install qemu-system-x86-64"
    exit 1
fi

echo "✓ QEMU 已安装: $(qemu-system-x86_64 --version | head -1)"
echo ""

echo "启动参数："
echo "  - 内存: 2GB"
echo "  - CPU: 4 核心"
echo "  - 控制台: 串口 (nographic)"
echo "  - 内核参数: console=ttyS0 init=/init"
echo ""

echo "=== 启动说明 ==="
echo "1. 系统启动后会自动运行初始化脚本"
echo "2. 进入 shell 后，可以运行以下命令测试调度器："
echo "   - test_yat_casched                 # 运行调度器测试"
echo "   - cat /proc/sched_debug           # 查看调度调试信息"
echo "   - ps -eo pid,class,comm           # 查看进程调度类"
echo "   - grep -i yat /proc/sched_debug   # 查找 yat_casched 相关信息"
echo "3. 退出 QEMU: 按 Ctrl+A 然后按 X"
echo ""

read -p "按 Enter 键启动 QEMU，或 Ctrl+C 取消..."

echo "启动中..."
echo ""

# 启动 QEMU
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 2048 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 init=/init loglevel=7" \
    -no-reboot \
    -enable-kvm 2>/dev/null || \
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 2048 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 init=/init loglevel=7" \
    -no-reboot

echo ""
echo "QEMU 已退出"
