#!/bin/bash

echo "Creating initramfs with cache-aware test..."

# 创建临时目录
TEMP_DIR="/tmp/initramfs_cache_test"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

# 创建基本目录结构
cd $TEMP_DIR
mkdir -p bin sbin etc proc sys dev tmp usr/bin usr/sbin lib lib64

# 复制busybox（如果存在）
if [ -f "/bin/busybox" ]; then
    cp /bin/busybox bin/
elif [ -f "/usr/bin/busybox" ]; then
    cp /usr/bin/busybox bin/
else
    echo "Warning: busybox not found, creating minimal environment"
fi

# 复制基本的系统文件
if [ -d "/lib" ]; then
    cp -r /lib/* lib/ 2>/dev/null || true
fi
if [ -d "/lib64" ]; then
    cp -r /lib64/* lib64/ 2>/dev/null || true
fi

# 复制测试程序
if [ -f "test_cache_aware_fixed" ]; then
    cp test_cache_aware_fixed bin/
    chmod +x bin/test_cache_aware_fixed
    echo "✓ Copied cache-aware test program"
else
    echo "✗ Cache-aware test program not found!"
    exit 1
fi

# 也复制完整测试程序作为备用
if [ -f "test_yat_casched_complete" ]; then
    cp test_yat_casched_complete bin/
    chmod +x bin/test_yat_casched_complete
    echo "✓ Copied complete test program"
fi

# 创建init脚本
cat > init << 'EOF'
#!/bin/sh

echo "Initializing system..."
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# 挂载基本文件系统
/bin/mount -t proc proc /proc
/bin/mount -t sysfs sysfs /sys
/bin/mount -t devtmpfs devtmpfs /dev

echo "System initialized successfully!"
echo ""
echo "Available test programs:"
echo "1. test_cache_aware_fixed - Cache-aware scheduler test"
echo "2. test_yat_casched_complete - Complete scheduler test"
echo ""

# 检查是否有busybox
if [ -f "/bin/busybox" ]; then
    # 创建busybox符号链接
    /bin/busybox --install -s /bin
    /bin/busybox --install -s /sbin
    echo "Busybox installed successfully!"
fi

echo "============================================================"
echo "Starting Yat_Casched Cache-Aware Test"
echo "============================================================"

# 显示CPU信息
if [ -f "/proc/cpuinfo" ]; then
    echo "CPU Information:"
    grep "processor\|model name" /proc/cpuinfo | head -8
    echo ""
fi

# 显示调度器信息
if [ -f "/proc/sched_debug" ]; then
    echo "Scheduler debug info available at /proc/sched_debug"
fi

# 运行缓存感知测试
echo "Running cache-aware scheduler test..."
echo ""
/bin/test_cache_aware_fixed

echo ""
echo "============================================================"
echo "Cache-aware test completed!"
echo "============================================================"
echo ""

# 提供交互式shell
echo "Dropping to shell for manual testing..."
echo "Available commands:"
echo "  test_cache_aware_fixed    - Run cache-aware test again"
echo "  test_yat_casched_complete - Run complete test suite"
echo "  cat /proc/cpuinfo         - Show CPU information"
echo "  cat /proc/sched_debug     - Show scheduler debug info"
echo "  ps                        - Show processes"
echo ""

# 启动shell
exec /bin/sh
EOF

chmod +x init

echo "Creating initramfs archive..."
find . | cpio -o -H newc | gzip > ../initramfs_cache_test.cpio.gz

if [ $? -eq 0 ]; then
    echo "✓ Created initramfs_cache_test.cpio.gz successfully!"
    ls -lh ../initramfs_cache_test.cpio.gz
else
    echo "✗ Failed to create initramfs!"
    exit 1
fi

# 清理临时目录
cd ..
rm -rf $TEMP_DIR

echo "Done!"
