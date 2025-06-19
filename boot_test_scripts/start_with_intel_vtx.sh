#!/bin/bash

# 获取脚本所在目录，确保相对路径正确
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_IMAGE="$PROJECT_ROOT/arch/x86/boot/bzImage"
DISK_IMAGE="$SCRIPT_DIR/initramfs_complete.cpio.gz"

echo "=== Intel VT-x/EPT 优化的 Yat_Casched 内核启动脚本 ==="

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
# 检查KVM是否可用
if [ ! -c /dev/kvm ]; then
    echo "✗ 警告: /dev/kvm 不存在，KVM可能未启用"
    echo "请检查BIOS中是否启用了Intel VT-x"
fi

# 检查当前用户是否有kvm权限
if ! groups | grep -q kvm; then
    echo "✗ 警告: 当前用户不在kvm组中"
    echo "建议运行: sudo usermod -a -G kvm $USER"
    echo "然后重新登录"
fi

# Intel VT-x/EPT 优化配置
echo "启动配置: Intel VT-x/EPT 多核测试"
echo "=================================="

if [ ! -c /dev/kvm ]; then
    echo "✓ KVM不可用，尝试使用TCG加速..."
    sudo qemu-system-x86_64 \
        -cpu qemu64 \
        -smp 4 \
        -m 2G \
        -machine pc,accel=tcg \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 initcall_blacklist=do_init_real_mode" \
        -nographic \
        -no-reboot
    exit $?
else
    echo "✓ KVM可用，正常启动..."
    qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp 8 \
        -m 4G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7" \
        -nographic \
        -no-reboot
fi

echo ""
echo "注意事项:"
echo "1. 此脚本专为Intel VT-x/EPT优化"
echo "2. 如果启动失败，可能需要在BIOS中启用虚拟化"
echo "3. 监控接口: telnet 127.0.0.1 1234"
echo "4. 按 Ctrl+A, X 退出QEMU"
