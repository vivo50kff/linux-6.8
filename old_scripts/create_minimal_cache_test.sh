#!/bin/bash

echo "Creating minimal initramfs for cache test..."

# 创建临时目录
TEMP_DIR="/tmp/initramfs_minimal"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

cd $TEMP_DIR

# 创建基本目录结构
mkdir -p bin sbin etc proc sys dev tmp

# 复制busybox - 使用静态链接版本
if [ -f "/bin/busybox" ]; then
    cp /bin/busybox bin/busybox
    echo "✓ Found busybox in /bin"
elif [ -f "/usr/bin/busybox" ]; then
    cp /usr/bin/busybox bin/busybox
    echo "✓ Found busybox in /usr/bin"
else
    echo "✗ busybox not found!"
    # 尝试安装busybox
    sudo apt-get update && sudo apt-get install -y busybox-static
    if [ -f "/bin/busybox" ]; then
        cp /bin/busybox bin/busybox
        echo "✓ Installed and copied busybox"
    else
        echo "✗ Failed to install busybox"
        exit 1
    fi
fi

# 确保busybox可执行
chmod +x bin/busybox

# 复制测试程序
cp /home/lwd/桌面/linux-6.8/test_cache_aware_fixed bin/
chmod +x bin/test_cache_aware_fixed
echo "✓ Copied cache test program"

# 创建非常简单的init脚本
cat > init << 'EOF'
#!/bin/busybox sh

echo "=== Minimal Init Starting ==="

# 设置PATH
export PATH=/bin:/sbin

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 创建busybox符号链接
/bin/busybox --install -s /bin
/bin/busybox --install -s /sbin

echo "=== System Initialized ==="
echo ""

# 显示CPU信息
echo "CPU cores available:"
nproc
echo ""

# 运行缓存感知测试
echo "=== Running Cache-Aware Scheduler Test ==="
/bin/test_cache_aware_fixed

echo ""
echo "=== Test Completed ==="
echo ""
echo "Dropping to shell..."
echo "Commands: test_cache_aware_fixed, ps, top, cat /proc/cpuinfo"
echo ""

# 启动shell
exec /bin/busybox sh
EOF

# 设置init脚本可执行
chmod +x init

echo "Creating initramfs archive..."
find . -print0 | cpio --null -ov --format=newc | gzip -9 > /home/lwd/桌面/linux-6.8/initramfs_minimal.cpio.gz

if [ $? -eq 0 ]; then
    echo "✓ Created initramfs_minimal.cpio.gz successfully!"
    ls -lh /home/lwd/桌面/linux-6.8/initramfs_minimal.cpio.gz
else
    echo "✗ Failed to create initramfs!"
    exit 1
fi

# 验证initramfs内容
echo ""
echo "Verifying initramfs contents..."
cd /tmp
mkdir -p verify_initramfs
cd verify_initramfs
zcat /home/lwd/桌面/linux-6.8/initramfs_minimal.cpio.gz | cpio -idv | head -20

echo ""
echo "Checking init script:"
ls -la init
echo ""
echo "Checking test program:"
ls -la bin/test_cache_aware_fixed

# 清理
cd ..
rm -rf $TEMP_DIR /tmp/verify_initramfs

echo "Done!"
