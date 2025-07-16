#!/bin/bash

# 获取脚本所在目录，确保相对路径正确
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_IMAGE="$PROJECT_ROOT/vmlinux"
DISK_IMAGE="$PROJECT_ROOT/test_static.cpio.gz"

echo "=== Intel VT-x/EPT 优化的 Yat_Casched 内核启动脚本 ==="

# 检查必要文件
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 错误: 内核镜像未找到: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "✗ 错误: initramfs 未找到: $DISK_IMAGE"
    echo "正在创建静态 initramfs..."
    
    # 创建静态 initramfs
    cd "$PROJECT_ROOT"
    if [ ! -f "test_static.cpio.gz" ]; then
        echo "运行简单启动脚本来创建 initramfs..."
        "$PROJECT_ROOT/yat_test_scripts/start_qemu_simple.sh" --create-only 2>/dev/null || {
            echo "自动创建 initramfs，请稍候..."
            
            # 创建目录结构
            rm -rf test_static_initramfs
            mkdir -p test_static_initramfs/{bin,sbin,etc,proc,sys,dev,tmp}
            
            # 检查并安装静态 busybox
            if [ ! -f "/bin/busybox" ]; then
                echo "安装 busybox-static..."
                sudo apt update && sudo apt install -y busybox-static
            fi
            
            cp /bin/busybox test_static_initramfs/bin/
            chmod +x test_static_initramfs/bin/busybox
            
            # 创建 init 脚本
            cat > test_static_initramfs/init << 'INITEOF'
#!/bin/busybox sh
echo "=== Yat_Casched 测试环境启动成功 ==="
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys  
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox --install -s
echo "📋 进入交互式 shell，输入 'exit' 退出"
exec /bin/busybox sh
INITEOF
            chmod +x test_static_initramfs/init
            
            # 创建 initramfs
            cd test_static_initramfs
            find . | cpio -o -H newc | gzip > ../test_static.cpio.gz
            cd "$PROJECT_ROOT"
            echo "✅ initramfs 创建完成"
        }
    fi
    cd "$SCRIPT_DIR"
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
    qemu-system-x86_64 \
        -cpu qemu64 \
        -smp 2 \
        -m 1G \
        -machine pc,accel=tcg \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200" \
        -nographic \
        -no-reboot
    exit $?
else
    echo "✓ KVM可用，正常启动..."
    qemu-system-x86_64 \
        -enable-kvm \
        -cpu host \
        -machine pc,accel=kvm \
        -smp 2 \
        -m 1G \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$DISK_IMAGE" \
        -append "console=ttyS0,115200" \
        -nographic \
        -no-reboot
fi

echo ""
echo "注意事项:"
echo "1. 此脚本专为Intel VT-x/EPT优化"
echo "2. 如果启动失败，可能需要在BIOS中启用虚拟化"
echo "3. 监控接口: telnet 127.0.0.1 1234"
echo "4. 按 Ctrl+A, X 退出QEMU"
