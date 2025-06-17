#!/bin/bash

# 创建简单的 initramfs 用于测试 Yat_Casched 调度器
# 这个脚本会创建一个最小化的 initramfs，包含基本的测试环境

INITRAMFS_DIR="initramfs"
INITRAMFS_FILE="initramfs.cpio.gz"

echo "=== 创建简单的 initramfs 用于 Yat_Casched 测试 ==="

# 清理并创建目录结构
rm -rf $INITRAMFS_DIR
mkdir -p $INITRAMFS_DIR/{bin,sbin,etc,proc,sys,dev,lib,lib64,tmp,var,home,root}

echo "1. 创建目录结构..."

# 复制基本的可执行文件
echo "2. 复制基本系统工具..."

# 检查并复制 busybox（如果可用）
if command -v busybox >/dev/null 2>&1; then
    cp $(which busybox) $INITRAMFS_DIR/bin/
    echo "   - 复制 busybox"
else
    echo "   - busybox 不可用，尝试复制其他基本工具..."
    
    # 复制基本的 shell 和工具
    for cmd in bash sh ls cat echo mkdir rm cp mv touch chmod chown ps kill grep; do
        if command -v $cmd >/dev/null 2>&1; then
            cp $(which $cmd) $INITRAMFS_DIR/bin/ 2>/dev/null && echo "     - 复制 $cmd"
        fi
    done
fi

# 复制我们的测试程序
echo "3. 复制 Yat_Casched 测试程序..."
if [ -f "test_yat_casched" ]; then
    cp test_yat_casched $INITRAMFS_DIR/bin/
    chmod +x $INITRAMFS_DIR/bin/test_yat_casched
    echo "   - 复制 test_yat_casched"
else
    echo "   - 警告：test_yat_casched 不存在，请先编译"
fi

# 复制必要的库文件
echo "4. 复制库文件..."

# 创建一个函数来复制库依赖
copy_libs() {
    local binary=$1
    if [ -f "$binary" ]; then
        ldd "$binary" 2>/dev/null | grep -o '/[^ ]*' | while read lib; do
            if [ -f "$lib" ]; then
                libdir=$(dirname "$lib")
                mkdir -p "$INITRAMFS_DIR$libdir"
                cp "$lib" "$INITRAMFS_DIR$lib" 2>/dev/null
            fi
        done
    fi
}

# 复制关键可执行文件的库依赖
for binary in $INITRAMFS_DIR/bin/*; do
    if [ -x "$binary" ]; then
        copy_libs "$binary"
    fi
done

# 复制动态链接器
if [ -f /lib64/ld-linux-x86-64.so.2 ]; then
    cp /lib64/ld-linux-x86-64.so.2 $INITRAMFS_DIR/lib64/
fi
if [ -f /lib/ld-linux.so.2 ]; then
    cp /lib/ld-linux.so.2 $INITRAMFS_DIR/lib/
fi

# 创建基本的配置文件
echo "5. 创建配置文件..."

cat > $INITRAMFS_DIR/etc/passwd << 'EOF'
root:x:0:0:root:/root:/bin/bash
EOF

cat > $INITRAMFS_DIR/etc/group << 'EOF'
root:x:0:
EOF

# 创建启动脚本
cat > $INITRAMFS_DIR/init << 'EOF'
#!/bin/busybox sh

echo "=== Yat_Casched 测试环境启动 ==="
echo "Linux 内核启动完成，正在初始化测试环境..."

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 设置基本环境
export PATH=/bin:/sbin
export HOME=/root
cd /root

echo "环境初始化完成！"
echo ""
echo "=== Yat_Casched 调度器测试 ==="
echo "您现在可以运行以下命令来测试调度器："
echo "  test_yat_casched     - 运行调度器测试程序"
echo "  cat /proc/sched_debug - 查看调度器调试信息"
echo "  ps -eo pid,class,comm - 查看进程调度类"
echo ""

# 设置 busybox 符号链接
echo "设置 busybox 符号链接..."
for cmd in $(/bin/busybox --list); do
    /bin/busybox ln -sf /bin/busybox /bin/$cmd 2>/dev/null
done

echo "启动交互式 shell..."
echo "提示：输入 'test_yat_casched' 来测试新的调度器"

# 启动 shell
exec /bin/busybox sh
EOF

chmod +x $INITRAMFS_DIR/init

# 如果 busybox 不可用，创建一个简单的 init
if ! command -v busybox >/dev/null 2>&1; then
    cat > $INITRAMFS_DIR/simple_init << 'EOF'
#!/bin/bash
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
export PATH=/bin:/sbin
cd /root
echo "=== 简化的 Yat_Casched 测试环境 ==="
echo "运行测试: /bin/test_yat_casched"
exec /bin/bash || exec /bin/sh
EOF
    chmod +x $INITRAMFS_DIR/simple_init
fi

# 创建设备节点
echo "6. 创建基本设备节点..."
mknod $INITRAMFS_DIR/dev/console c 5 1
mknod $INITRAMFS_DIR/dev/null c 1 3
mknod $INITRAMFS_DIR/dev/zero c 1 5

# 打包 initramfs
echo "7. 打包 initramfs..."
cd $INITRAMFS_DIR
find . | cpio -o -H newc | gzip > ../$INITRAMFS_FILE
cd ..

echo ""
echo "=== initramfs 创建完成 ==="
echo "文件位置: $(pwd)/$INITRAMFS_FILE"
echo "大小: $(du -h $INITRAMFS_FILE | cut -f1)"
echo ""
echo "现在可以使用以下命令启动 QEMU 测试："
echo ""
echo "qemu-system-x86_64 \\"
echo "    -kernel arch/x86/boot/bzImage \\"
echo "    -initrd $INITRAMFS_FILE \\"
echo "    -m 2048 \\"
echo "    -smp 4 \\"
echo "    -nographic \\"
echo "    -append \"console=ttyS0 init=/init\""
echo ""
