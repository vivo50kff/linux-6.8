#!/bin/bash

echo "=== 快速测试脚本 - 用于课程演示 ==="
echo "此脚本适用于课堂演示或快速验证"
echo ""

# 检查环境
if [ ! -f "arch/x86/boot/bzImage" ]; then
    echo "✗ 内核未编译，请先运行: make -j\$(nproc)"
    exit 1
fi

# 快速编译关键测试程序
echo "编译关键测试程序..."
gcc -static -o verify_real_scheduling verify_real_scheduling.c
if [ $? -ne 0 ]; then
    echo "✗ 编译失败"
    exit 1
fi

# 创建最小化 initramfs
echo "创建最小化测试环境..."
TEMP_DIR="/tmp/initramfs_demo"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

cd $TEMP_DIR
mkdir -p bin sbin proc sys dev

# 复制必要组件
cp /bin/busybox bin/busybox
cp verify_real_scheduling bin/
chmod +x bin/*

# 创建简单 init
cat > init << 'EOF'
#!/bin/busybox sh
echo "=== Yat_Casched 调度器演示 ==="
export PATH=/bin:/sbin
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox --install -s /bin
echo "系统: $(uname -r), CPU: $(nproc)核"
echo "运行调度器验证..."
/bin/verify_real_scheduling
echo "验证完成! 输入命令或按Ctrl+A,X退出"
exec /bin/busybox sh
EOF
chmod +x init

# 打包
find . | cpio -o -H newc | gzip > initramfs_demo.cpio.gz
cd ..
rm -rf $TEMP_DIR

echo "✓ 演示环境准备完成"
echo ""
echo "启动演示:"

# 直接启动
qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -initrd boot_test_scripts/initramfs_demo.cpio.gz \
    -m 512 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 loglevel=3 rdinit=/init" \
    -no-reboot \
    -enable-kvm 2>/dev/null || \
qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -initrd boot_test_scripts/initramfs_demo.cpio.gz \
    -m 512 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 loglevel=3 rdinit=/init" \
    -no-reboot
