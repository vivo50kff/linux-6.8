#!/bin/bash

# 获取脚本所在目录，确保相对路径正确
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_IMAGE="$PROJECT_ROOT/arch/x86/boot/bzImage"
DISK_IMAGE="$SCRIPT_DIR/initramfs_complete.cpio.gz"

echo "=== Yat_Casched v2.2 针对性测试启动脚本 ==="
echo "专门测试Benefit机制和Impact机制"

# 检查必要文件
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    echo "请先编译内核: make -j$(nproc)"
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "✗ 错误: initramfs 未找到: $DISK_IMAGE"
    echo "请先运行: $SCRIPT_DIR/create_initramfs_complete.sh"
    exit 1
fi

echo "✓ 检查通过，准备启动v2.2测试环境..."
echo "  内核: $KERNEL_IMAGE"
echo "  文件系统: $DISK_IMAGE"
echo "  测试程序: yat_v2_2_test (包含Benefit/Impact验证)"
echo ""

# 检查KVM可用性并选择最佳启动方案
if [ -c /dev/kvm ] && groups | grep -q kvm; then
    echo "✓ 使用KVM加速启动 (推荐)"
    echo "配置: 8核心, 4GB内存, Intel VT-x优化"
    echo "=================================="
    
    qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp 8 \
        -m 4G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=8 isolcpus=1-7" \
        -nographic \
        -no-reboot
        
elif [ -c /dev/kvm ]; then
    echo "⚠ KVM可用但需要sudo权限"
    echo "配置: 8核心, 4GB内存"
    echo "=================================="
    
    sudo qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp 8 \
        -m 4G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=8" \
        -nographic \
        -no-reboot
        
else
    echo "⚠ KVM不可用，使用TCG模拟 (较慢)"
    echo "配置: 4核心, 2GB内存"
    echo "=================================="
    
    qemu-system-x86_64 \
        -cpu qemu64 \
        -smp 4 \
        -m 2G \
        -machine pc,accel=tcg \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=4" \
        -nographic \
        -no-reboot
fi

echo ""
echo "=================================================================="
echo "v2.2测试环境退出。查看测试结果:"
echo "1. Benefit机制: 观察任务是否倾向于返回之前运行的CPU"
echo "2. Impact机制: 观察多任务冲突时的智能分配"
echo "3. 任务池管理: 观察优先级排序和历史表更新"
echo "=================================================================="
