#!/bin/bash

# Yat_Casched 快速测试脚本
# 使用静态 busybox 启动 QEMU 测试环境

cd /home/yatsched/linux-6.8

echo "=== Yat_Casched 快速测试启动 ==="
echo "时间: $(date)"

# 检查内核文件
if [ ! -f "vmlinux" ]; then
    echo "❌ 错误: 找不到内核文件 vmlinux"
    echo "请先编译内核：make -j$(nproc)"
    exit 1
fi

# 使用现有的静态 initramfs 或创建新的
if [ ! -f "test_static.cpio.gz" ]; then
    echo "🔧 创建静态 initramfs..."
    
    # 创建目录结构
    rm -rf test_static_initramfs
    mkdir -p test_static_initramfs/{bin,sbin,etc,proc,sys,dev,tmp,usr/bin}
    
    # 复制静态 busybox
    if [ ! -f "/bin/busybox" ]; then
        echo "安装 busybox-static..."
        sudo apt update && sudo apt install -y busybox-static
    fi
    
    cp /bin/busybox test_static_initramfs/bin/
    chmod +x test_static_initramfs/bin/busybox
    
    # 创建 init 脚本
    cat > test_static_initramfs/init << 'EOF'
#!/bin/busybox sh

echo "=== Yat_Casched 测试环境启动成功 ==="
echo "内核版本: $(busybox uname -r)"
echo "当前时间: $(busybox date)"

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys  
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 安装 busybox 命令
/bin/busybox --install -s

echo ""
echo "🎯 Yat_Casched 调度器信息:"
echo "- 内核版本: $(uname -r)"
echo "- 调度器类型: Yat_Casched (Cache-Aware Scheduler)"

echo ""
echo "📋 可用命令："
echo "- ps        : 查看进程"
echo "- top       : 系统监控"
echo "- uptime    : 系统运行时间"
echo "- free      : 内存使用情况"
echo "- cat /proc/version : 详细内核信息"
echo "- exit      : 退出"

echo ""
echo "=== 进入交互式 shell ==="
exec /bin/busybox sh
EOF
    
    chmod +x test_static_initramfs/init
    
    # 创建 initramfs
    cd test_static_initramfs
    find . | cpio -o -H newc | gzip > ../test_static.cpio.gz
    cd ..
    
    echo "✅ initramfs 创建完成"
fi

echo ""
echo "🚀 启动 QEMU 虚拟机..."
echo "   - 内核: vmlinux"
echo "   - 内存: 1024MB"
echo "   - 加速: TCG"
echo "   - 串口: ttyS0"
echo ""
echo "💡 提示: 要退出 QEMU，请在虚拟机中输入 'exit'，然后按 Ctrl+A 再按 X"
echo ""

# 启动 QEMU
qemu-system-x86_64 \
    -nographic \
    -m 1024 \
    -kernel vmlinux \
    -initrd test_static.cpio.gz \
    -append "console=ttyS0,115200 quiet" \
    -accel tcg
