#!/bin/bash

# 创建完善的 initramfs 用于 Yat_Casched 用户态测试

INITRAMFS_DIR="initramfs_fixed"
INITRAMFS_FILE="initramfs_fixed.cpio.gz"

echo "=== 创建完善的 Yat_Casched 测试 initramfs ==="

# 清理并创建目录结构
rm -rf $INITRAMFS_DIR
mkdir -p $INITRAMFS_DIR/{bin,sbin,etc,proc,sys,dev,lib,lib64,tmp,var,home,root}

echo "1. 创建目录结构..."

# 复制 busybox
if command -v busybox >/dev/null 2>&1; then
    cp $(which busybox) $INITRAMFS_DIR/bin/
    echo "   - 复制 busybox"
else
    echo "   - 错误：busybox 不可用"
    exit 1
fi

# 编译并复制完整测试程序
echo "   - 编译 test_yat_casched_complete..."
if [ -f "test_yat_casched_complete.c" ]; then
    gcc -static -o test_yat_casched_complete test_yat_casched_complete.c -lpthread
    if [ $? -eq 0 ]; then
        cp test_yat_casched_complete $INITRAMFS_DIR/bin/
        chmod +x $INITRAMFS_DIR/bin/test_yat_casched_complete
        echo "   - 复制 test_yat_casched_complete (完整版)"
    else
        echo "   - 错误：编译 test_yat_casched_complete 失败"
        exit 1
    fi
else
    echo "   - 错误：test_yat_casched_complete.c 不存在"
    exit 1
fi

# 同时保留简单版本作为备用
if [ -f "test_yat_casched.c" ]; then
    gcc -static -o test_yat_casched test_yat_casched.c
    if [ $? -eq 0 ]; then
        cp test_yat_casched $INITRAMFS_DIR/bin/
        chmod +x $INITRAMFS_DIR/bin/test_yat_casched
        echo "   - 复制 test_yat_casched (简单版)"
    fi
fi

echo "2. 复制必要的库文件..."

# 复制动态链接器
if [ -f /lib64/ld-linux-x86-64.so.2 ]; then
    cp /lib64/ld-linux-x86-64.so.2 $INITRAMFS_DIR/lib64/
fi

# 复制基本库
for lib in libc.so.6 libpthread.so.0 libm.so.6 libdl.so.2; do
    if [ -f /lib/x86_64-linux-gnu/$lib ]; then
        cp /lib/x86_64-linux-gnu/$lib $INITRAMFS_DIR/lib/
    fi
done

echo "3. 创建配置文件..."

# 创建基本的配置文件
cat > $INITRAMFS_DIR/etc/passwd << 'EOF'
root:x:0:0:root:/root:/bin/sh
EOF

cat > $INITRAMFS_DIR/etc/group << 'EOF'
root:x:0:
EOF

echo "4. 创建 init 脚本..."

# 创建修复的 init 脚本
cat > $INITRAMFS_DIR/init << 'EOF'
#!/bin/busybox sh

echo "=== Yat_Casched 测试环境启动 ==="
echo "Linux 内核启动完成，正在初始化测试环境..."

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 设置环境变量
export PATH=/bin:/sbin
export HOME=/root
cd /root

echo "环境初始化完成！"
echo ""

# 创建 busybox 符号链接（避免循环）
echo "设置基本命令链接..."
for cmd in sh ls cat echo ps mount umount grep; do
    if [ ! -e /bin/$cmd ]; then
        /bin/busybox ln -sf /bin/busybox /bin/$cmd
    fi
done

echo ""
echo "=== Yat_Casched 调度器测试 ==="
echo "系统信息："
echo "  内核版本: $(cat /proc/version | cut -d' ' -f1-3)"
echo "  CPU 数量: $(cat /proc/cpuinfo | grep processor | wc -l)"
echo ""

# 显示调度器信息
echo "=== 调度器信息 ==="
if [ -f /proc/sched_debug ]; then
    echo "检查 Yat_Casched 调度器..."
    if grep -q "yat_casched" /proc/sched_debug 2>/dev/null; then
        echo "✓ 发现 Yat_Casched 调度器！"
        echo "Yat_Casched 相关信息："
        grep -i yat /proc/sched_debug | head -5
    else
        echo "! 在 /proc/sched_debug 中未找到 yat_casched"
        echo "显示前几行调度器信息："
        head -20 /proc/sched_debug
    fi
else
    echo "! /proc/sched_debug 不存在"
fi

echo ""
echo "=== 运行用户态测试程序 ==="

# 运行完整测试程序
if [ -x /bin/test_yat_casched_complete ]; then
    echo "开始运行完整测试程序 test_yat_casched_complete..."
    echo "-----------------------------------"
    /bin/test_yat_casched_complete
    echo "-----------------------------------"
    echo "完整测试程序运行完成！"
    echo ""
fi

# 运行简单测试程序作为备用
if [ -x /bin/test_yat_casched ]; then
    echo "开始运行简单测试程序 test_yat_casched..."
    echo "-----------------------------------"
    /bin/test_yat_casched
    echo "-----------------------------------"
    echo "简单测试程序运行完成！"
else
    echo "! test_yat_casched 不存在或不可执行"
fi

echo ""
echo "=== 其他测试命令 ==="
echo "您可以运行以下命令进行更多测试："
echo "  /bin/test_yat_casched_complete           # 运行完整测试程序"
echo "  /bin/test_yat_casched                    # 运行简单测试程序"
echo "  cat /proc/sched_debug | grep -i sched    # 查看调度器信息"
echo "  ps -eo pid,class,comm                    # 查看进程调度类"
echo "  cat /proc/schedstat                      # 查看调度统计"
echo ""

# 启动交互式 shell
echo "启动交互式 shell..."
echo "提示：输入 'exit' 退出"
exec /bin/busybox sh
EOF

chmod +x $INITRAMFS_DIR/init

echo "5. 创建设备节点..."
# 注意：在普通用户权限下可能失败，但不影响基本功能
mknod $INITRAMFS_DIR/dev/console c 5 1 2>/dev/null || echo "   - 跳过设备节点创建（需要root权限）"
mknod $INITRAMFS_DIR/dev/null c 1 3 2>/dev/null
mknod $INITRAMFS_DIR/dev/zero c 1 5 2>/dev/null

echo "6. 打包 initramfs..."
cd $INITRAMFS_DIR
find . | cpio -o -H newc | gzip > ../$INITRAMFS_FILE
cd ..

echo ""
echo "=== 修复版 initramfs 创建完成 ==="
echo "文件位置: $(pwd)/$INITRAMFS_FILE"
echo "大小: $(du -h $INITRAMFS_FILE | cut -f1)"
echo ""
