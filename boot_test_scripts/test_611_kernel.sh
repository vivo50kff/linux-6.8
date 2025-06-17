#!/bin/bash

echo "=== 测试6.11内核在QEMU中的兼容性 ==="
echo ""

# 检查6.11内核是否存在
if [ ! -f "/boot/vmlinuz-6.11.0-25-generic" ]; then
    echo "✗ 6.11内核不存在"
    exit 1
fi

# 快速编译关键测试程序
echo "编译关键测试程序..."
cd ..
gcc -static -o verify_real_scheduling verify_real_scheduling.c
if [ $? -ne 0 ]; then
    echo "✗ 编译失败"
    exit 1
fi

# 创建最小化 initramfs
echo "创建最小化测试环境..."
TEMP_DIR="/tmp/initramfs_611_test"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

cd $TEMP_DIR
mkdir -p bin sbin proc sys dev

# 复制必要组件
cp /bin/busybox bin/busybox
cp /home/lwd/桌面/linux-6.8/verify_real_scheduling bin/
chmod +x bin/*

# 创建简单 init
cat > init << 'EOF'
#!/bin/busybox sh
echo "=== 6.11内核QEMU兼容性测试 ==="
export PATH=/bin:/sbin
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox --install -s /bin
echo "系统: $(uname -r), CPU: $(nproc)核"
echo "6.11内核在QEMU中启动成功!"
echo "输入命令或按Ctrl+A,X退出"
exec /bin/busybox sh
EOF
chmod +x init

# 打包
find . | cpio -o -H newc | gzip > /home/lwd/桌面/linux-6.8/boot_test_scripts/initramfs_611_test.cpio.gz
cd /home/lwd/桌面/linux-6.8
rm -rf $TEMP_DIR

echo "✓ 6.11测试环境准备完成"
echo ""
echo "启动6.11内核测试:"

# 直接启动6.11内核
timeout 60 qemu-system-x86_64 \
    -kernel /boot/vmlinuz-6.11.0-25-generic \
    -initrd boot_test_scripts/initramfs_611_test.cpio.gz \
    -m 512 \
    -smp 2 \
    -nographic \
    -append "console=ttyS0 loglevel=3 rdinit=/init" \
    -no-reboot
