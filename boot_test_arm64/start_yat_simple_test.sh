#!/bin/bash

# YAT_CASCHED ARM64 内核测试启动脚本
# 使用QEMU模拟ARM64环境运行YAT调度器测试

set -e

# 配置参数
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$(dirname "$SCRIPT_DIR")"
KERNEL_IMAGE="$WORK_DIR/arch/arm64/boot/Image"
INITRAMFS="$SCRIPT_DIR/yat_simple_test.cpio.gz"

echo "=== YAT_CASCHED ARM64 内核测试环境 ==="
echo "脚本目录: $SCRIPT_DIR"
echo "内核目录: $WORK_DIR"

# 检查文件是否存在
echo "检查必要文件..."
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "错误: ARM64内核镜像不存在: $KERNEL_IMAGE"
    echo "请先编译ARM64内核: make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "警告: 测试镜像不存在: $INITRAMFS"
    echo "正在构建测试环境..."
    "$SCRIPT_DIR/build_yat_simple_test.sh"
    if [ ! -f "$INITRAMFS" ]; then
        echo "错误: 构建测试镜像失败"
        exit 1
    fi
fi

echo "文件检查完成:"
echo "  内核镜像: $KERNEL_IMAGE ($(du -h "$KERNEL_IMAGE" | cut -f1))"
echo "  测试镜像: $INITRAMFS ($(du -h "$INITRAMFS" | cut -f1))"

# 检查QEMU是否安装
if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
    echo "错误: 未找到 qemu-system-aarch64"
    echo "请安装: sudo apt-get install qemu-system-arm"
    exit 1
fi

echo "QEMU版本: $(qemu-system-aarch64 --version | head -1)"

# QEMU参数配置
QEMU_PARAMS=(
    "-M" "virt"                    # ARM64虚拟机类型
    "-cpu" "cortex-a57"            # Raspberry Pi 4兼容的CPU
    "-smp" "4"                     # 4核CPU，匹配树莓派4
    "-m" "2048M"                   # 2GB内存
    "-kernel" "$KERNEL_IMAGE"      # ARM64内核镜像
    "-initrd" "$INITRAMFS"         # 初始化文件系统
    "-append" "console=ttyAMA0 root=/dev/ram0 rdinit=/init loglevel=7 debug"
    "-serial" "stdio"              # 串口输出到终端
    "-display" "none"              # 无图形界面
    "-no-reboot"                   # 退出时不重启
)

echo ""
echo "启动参数:"
for param in "${QEMU_PARAMS[@]}"; do
    echo "  $param"
done

echo ""
echo "=== 启动ARM64内核测试 ==="
echo "注意: 使用 Ctrl+A, X 退出QEMU"
echo "      或在QEMU控制台中输入 'halt'"
echo ""

# 启动QEMU
exec qemu-system-aarch64 "${QEMU_PARAMS[@]}"
