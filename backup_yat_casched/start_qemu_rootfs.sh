#!/bin/bash

# 使用系统现有 initramfs 的启动脚本
# 这个方案使用主机系统的文件系统，更接近真实环境

KERNEL="arch/x86/boot/bzImage"

echo "=== 方案2：使用主机文件系统测试 Yat_Casched ==="
echo ""

# 检查内核文件
if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件 $KERNEL 不存在"
    exit 1
fi

echo "✓ 内核文件: $KERNEL ($(du -h $KERNEL | cut -f1))"
echo ""

# 创建一个简单的根文件系统镜像
ROOT_IMG="rootfs.img"
if [ ! -f "$ROOT_IMG" ]; then
    echo "创建根文件系统镜像..."
    dd if=/dev/zero of="$ROOT_IMG" bs=1M count=512 2>/dev/null
    mkfs.ext4 -F "$ROOT_IMG" >/dev/null 2>&1
    
    # 挂载并复制必要文件
    mkdir -p mnt
    sudo mount -o loop "$ROOT_IMG" mnt
    
    # 创建基本目录结构
    sudo mkdir -p mnt/{bin,sbin,lib,lib64,usr,etc,proc,sys,dev,tmp,root,home}
    
    # 复制 busybox 和测试程序
    sudo cp /usr/bin/busybox mnt/bin/
    sudo cp test_yat_casched mnt/bin/ 2>/dev/null || echo "警告：test_yat_casched 不存在"
    
    # 复制基本库文件
    sudo cp -P /lib/x86_64-linux-gnu/libc.so.6 mnt/lib/ 2>/dev/null
    sudo cp -P /lib64/ld-linux-x86-64.so.2 mnt/lib64/ 2>/dev/null
    sudo cp -P /lib/x86_64-linux-gnu/libpthread.so.0 mnt/lib/ 2>/dev/null
    
    # 创建 init 脚本
    sudo tee mnt/init > /dev/null << 'EOF'
#!/bin/busybox sh

echo "=== Yat_Casched 测试环境 ==="
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 设置 busybox 符号链接
for cmd in $(/bin/busybox --list); do
    /bin/busybox ln -sf /bin/busybox /bin/$cmd
done

export PATH=/bin:/sbin:/usr/bin
cd /root

echo "环境就绪！测试命令："
echo "  test_yat_casched"
echo "  cat /proc/sched_debug | grep -i yat"

exec /bin/busybox sh
EOF
    sudo chmod +x mnt/init
    
    sudo umount mnt
    rmdir mnt
    echo "✓ 根文件系统创建完成: $ROOT_IMG"
fi

echo ""
echo "启动 QEMU (使用根文件系统镜像)..."
echo "退出: Ctrl+A, X"
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -hda "$ROOT_IMG" \
    -m 2048 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 root=/dev/sda init=/init" \
    -no-reboot
