#!/bin/bash

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_IMAGE="$PROJECT_ROOT/arch/x86/boot/bzImage"
INITRAMFS_IMAGE="$PROJECT_ROOT/boot_test_scripts/initramfs_complete.cpio.gz"

echo "=== Yat_Casched 调度器一键测试环境启动脚本 ==="

# 检查内核镜像和initramfs
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 未找到内核镜像: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$INITRAMFS_IMAGE" ]; then
    echo "✗ 错误: 未找到initramfs: $INITRAMFS_IMAGE"
    echo "请先运行: boot_test_scripts/create_initramfs_complete.sh"
    exit 1
fi

echo "✓ 检查通过，准备启动 QEMU 测试环境..."
echo "  内核: $KERNEL_IMAGE"
echo "  文件系统: $INITRAMFS_IMAGE"
echo ""

# 检查 QEMU 是否安装
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "✗ 错误: 未找到 qemu-system-x86_64 命令"
    echo "请先安装 QEMU：sudo apt install qemu-system-x86"
    exit 1
fi

# 检查 KVM 是否可用
if [ ! -c /dev/kvm ]; then
    echo "✗ 警告: /dev/kvm 不存在，KVM加速不可用，将使用纯软件模拟 (TCG)。"
    echo "如需加速，请在BIOS中启用虚拟化，并安装KVM模块。"
    QEMU_ACCEL="tcg"
    QEMU_KVM_OPTS=""
    QEMU_CPU="qemu64"
else
    QEMU_ACCEL="kvm"
    QEMU_KVM_OPTS="-enable-kvm"
    QEMU_CPU="host"
fi

# 启动 QEMU
qemu-system-x86_64 \
    $QEMU_KVM_OPTS \
    -cpu $QEMU_CPU \
    -machine pc,accel=$QEMU_ACCEL \
    -smp 4 \
    -m 2G \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS_IMAGE" \
    -append "console=ttyS0,115200 noapic acpi=off rdinit=/init panic=1 loglevel=7 maxcpus=4" \
    -nographic \
    -no-reboot

echo ""
echo "QEMU 测试环境已关闭。"
