#!/bin/bash

echo "Creating verification initramfs..."

TEMP_DIR="/tmp/initramfs_verify"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

cd $TEMP_DIR

# 创建基本目录结构
mkdir -p bin sbin etc proc sys dev tmp

# 复制busybox
cp /bin/busybox bin/busybox
chmod +x bin/busybox

# 复制验证程序
cp verify_real_scheduling bin/
chmod +x bin/verify_real_scheduling

# 创建init脚本
cat > init << 'EOF'
#!/bin/busybox sh

echo "=== 内核调度器真实性验证系统 ==="

export PATH=/bin:/sbin

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 创建busybox符号链接
/bin/busybox --install -s /bin
/bin/busybox --install -s /sbin

echo "系统初始化完成!"
echo ""

# 显示系统信息
echo "系统信息:"
echo "内核版本: $(uname -r)"
echo "CPU核心数: $(nproc)"
echo "当前时间: $(date)"
echo ""

# 运行验证程序
echo "开始验证 Yat_Casched 调度器的真实性..."
echo ""
/bin/verify_real_scheduling

echo ""
echo "验证完成! 现在你可以:"
echo "1. 再次运行验证: verify_real_scheduling"
echo "2. 查看进程: ps"
echo "3. 查看CPU信息: cat /proc/cpuinfo"
echo "4. 查看调度器信息: cat /proc/sched_debug"
echo ""

exec /bin/busybox sh
EOF

chmod +x init

echo "创建initramfs归档..."
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs_verify.cpio.gz

if [ $? -eq 0 ]; then
    echo "✓ 成功创建 initramfs_verify.cpio.gz!"
    ls -lh /home/lwd/桌面/linux-6.8/initramfs_verify.cpio.gz
else
    echo "✗ 创建initramfs失败!"
    exit 1
fi

cd ..
rm -rf $TEMP_DIR

echo "完成!"
