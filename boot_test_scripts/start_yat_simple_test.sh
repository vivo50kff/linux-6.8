#!/bin/bash

# YAT_CASCHED QEMU启动脚本
# 启动带有YAT调度器的Linux内核进行测试

set -e

echo "=== YAT_CASCHED QEMU测试启动 ==="

# 配置路径
WORK_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_DIR="$WORK_DIR/boot_test_scripts"
KERNEL="$WORK_DIR/vmlinux"
INITRAMFS="$SCRIPT_DIR/yat_simple_test.cpio.gz"
LOG_FILE="$SCRIPT_DIR/yat_simple_qemu.log"

# 检查文件存在性
if [ ! -f "$KERNEL" ]; then
    echo "错误: 内核文件不存在: $KERNEL"
    echo "请先编译内核"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误: initramfs文件不存在: $INITRAMFS"
    echo "请先运行: ./build_yat_simple_test.sh"
    exit 1
fi

echo "内核文件: $KERNEL"
echo "Initramfs: $INITRAMFS" 
echo "日志文件: $LOG_FILE"

# QEMU启动参数
QEMU_ARGS=(
    -nographic 
    -m 1024 
    -smp 4   
    -kernel $KERNEL   
    -initrd $INITRAMFS   
    -append "console=ttyS0,115200"   
    -accel tcg
)

echo ""
echo "启动参数:"
echo "内存: 512MB"
echo "CPU核心: 4"
echo "控制台: ttyS0"
echo ""

echo "启动QEMU (按 Ctrl+A 然后 X 退出)..."
echo "日志将保存到: $LOG_FILE"
echo ""

# 启动QEMU并记录日志
qemu-system-x86_64 "${QEMU_ARGS[@]}" 2>&1 | tee "$LOG_FILE"

echo ""
echo "QEMU已退出"
echo "查看完整日志: cat $LOG_FILE"
