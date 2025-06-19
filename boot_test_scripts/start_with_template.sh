#!/bin/bash

# 获取脚本所在目录，确保相对路径正确
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_IMAGE="$PROJECT_ROOT/arch/x86/boot/bzImage"
DISK_IMAGE="$SCRIPT_DIR/initramfs_complete.cpio.gz"

echo "=== 基于工作模板的 Yat_Casched 内核启动脚本 ==="

# 检查必要文件

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "✗ 错误: initramfs 未找到: $DISK_IMAGE"
    echo "请先运行: $SCRIPT_DIR/create_initramfs_complete.sh"
    exit 1
fi

echo "✓ 检查通过，准备启动..."
echo "  内核: $KERNEL_IMAGE"
echo "  文件系统: $DISK_IMAGE"
echo ""

# 方案1: 尝试使用KVM加速（Intel VT-x/EPT优化）
echo "尝试方案1: KVM加速启动 (Intel VT-x/EPT)"
echo "=================================="

# 检查KVM可用性
if [ ! -c /dev/kvm ]; then
    echo "✗ /dev/kvm 不存在，跳过KVM"
elif ! groups | grep -q kvm && [ "$EUID" -ne 0 ]; then
    echo "⚠ 用户不在kvm组，尝试sudo运行..."
    sudo qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp cores=4,threads=1,sockets=1 \
        -m 2G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=4" \
        -nographic \
        -no-reboot
    exit $?
else
    echo "✓ KVM可用，正常启动..."
    timeout 30s qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp cores=4,threads=1,sockets=1 \
        -m 2G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=4" \
        -nographic \
        -no-reboot
fi

if [ $? -ne 0 ]; then
    echo ""
    echo "KVM不可用，尝试方案2: TCG模拟"
    echo "=================================="
    
    qemu-system-x86_64 \
        -cpu qemu64 \
        -smp 4 \
        -m 2G \
        -machine pc,accel=tcg \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 initcall_blacklist=do_init_real_mode" \
        -nographic \
        -no-reboot
fi

echo ""
echo "启动完成"
