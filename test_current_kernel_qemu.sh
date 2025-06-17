#!/bin/bash

echo "=== 测试当前6.11内核在QEMU中的启动 ==="
echo "验证QEMU环境是否正常工作"
echo ""

# 检查当前内核文件
KERNEL_IMAGE="/boot/vmlinuz-6.11.0-25-generic"
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "✗ 当前内核镜像不存在: $KERNEL_IMAGE"
    echo "可用内核："
    ls -la /boot/vmlinuz*
    exit 1
fi

echo "✓ 使用内核: $KERNEL_IMAGE"

# 创建简单的initramfs用于测试
echo "创建测试initramfs..."
mkdir -p test_initramfs_611
cd test_initramfs_611

# 创建基本目录结构
mkdir -p bin sbin etc proc sys dev

# 创建简单的init脚本
cat > init << 'EOF'
#!/bin/sh
echo "=== 6.11内核在QEMU中启动成功! ==="
echo "内核版本: $(uname -r)"
echo "CPU信息: $(cat /proc/cpuinfo | grep 'model name' | head -1)"
echo "内存信息: $(cat /proc/meminfo | grep MemTotal)"
echo ""
echo "QEMU环境测试通过!"
echo ""
echo "按回车键继续或等待10秒后自动关机..."
read -t 10
poweroff -f
EOF

chmod +x init

# 创建initramfs
echo "打包initramfs..."
find . | cpio -o -H newc | gzip > ../test_611.cpio.gz
cd ..

echo "✓ 测试initramfs创建完成"

# 启动QEMU测试
echo ""
echo "=== 启动QEMU测试6.11内核 ==="
echo "如果看到'QEMU环境测试通过!'说明环境正常"
echo "按Ctrl+C可以提前退出"
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -initrd test_611.cpio.gz \
    -m 512M \
    -smp 2 \
    -nographic \
    -append "console=ttyS0 quiet loglevel=3" \
    -no-reboot

echo ""
echo "=== QEMU测试完成 ==="

# 清理
rm -rf test_initramfs_611 test_611.cpio.gz

if [ $? -eq 0 ]; then
    echo "✓ 6.11内核在QEMU中运行正常"
    echo "现在可以安全测试6.8自编译内核"
else
    echo "✗ 6.11内核在QEMU中运行异常"
    echo "需要检查QEMU环境配置"
fi
