#!/bin/bash

echo "=== Intel VT-x/EPT 优化的 Yat_Casched 内核启动脚本 ==="

# 检查必要文件
KERNEL_IMAGE="/home/lwd/桌面/linux-6.8/arch/x86/boot/bzImage"
INITRAMFS="/home/lwd/桌面/linux-6.8/boot_test_scripts/initramfs_complete.cpio.gz"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "✗ 错误: initramfs 未找到: $INITRAMFS"
    echo "请先运行: ./boot_test_scripts/create_initramfs_complete.sh"
    exit 1
fi

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

echo "✓ 检查通过，准备启动..."
echo "  内核: $KERNEL_IMAGE"
echo "  文件系统: $INITRAMFS"
echo "  CPU: Intel VT-x/EPT"
echo ""

# Intel VT-x/EPT 优化配置
echo "启动配置: Intel VT-x/EPT 多核测试"
echo "=================================="

qemu-system-x86_64 \
    -enable-kvm \
    -cpu host,+vmx,+ept \
    -machine pc,accel=kvm \
    -smp cores=4,threads=1,sockets=1 \
    -m 2G \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7 maxcpus=4 nr_cpus=4" \
    -nographic \
    -no-reboot \
    -serial stdio \
    -monitor telnet:127.0.0.1:1234,server,nowait \
    -netdev user,id=net0 \
    -device e1000,netdev=net0

echo ""
echo "注意事项:"
echo "1. 此脚本专为Intel VT-x/EPT优化"
echo "2. 如果启动失败，可能需要在BIOS中启用虚拟化"
echo "3. 监控接口: telnet 127.0.0.1 1234"
echo "4. 按 Ctrl+A, X 退出QEMU"
