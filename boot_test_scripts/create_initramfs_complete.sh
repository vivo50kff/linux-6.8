#!/bin/bash

# 获取脚本所在目录，确保相对路径正确
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Yat_Casched 调度器测试环境创建脚本 ==="
echo "创建包含基础测试程序的 initramfs..."

# 创建临时目录
TEMP_DIR="/tmp/initramfs_basic"
rm -rf $TEMP_DIR
mkdir -p $TEMP_DIR

cd $TEMP_DIR

# 创建基本目录结构
mkdir -p bin sbin etc proc sys dev tmp usr/bin usr/sbin lib lib64

echo "正在配置基础环境..."

# 复制busybox（确保存在）
if [ -f "/bin/busybox" ]; then
    echo "✓ 复制 busybox 从 /bin/"
    cp /bin/busybox bin/
elif [ -f "/usr/bin/busybox" ]; then
    echo "✓ 复制 busybox 从 /usr/bin/"
    cp /usr/bin/busybox bin/
else
    echo "✗ 未找到 busybox，请先安装 busybox"
    exit 1
fi

chmod +x bin/busybox

# 编译并复制新的完整测试程序

echo "✓ 编译并复制新的完整测试程序 test_yat_casched_complete..."
if [ -f "$SCRIPT_DIR/test_yat_casched_complete.c" ]; then
    gcc -static "$SCRIPT_DIR/test_yat_casched_complete.c" -o bin/test_program
    if [ $? -eq 0 ]; then
        echo "  -> 编译成功"
        chmod +x bin/test_program
    else
        echo "  -> ✗ 编译失败！"
        exit 1
    fi
else
    echo "⚠ 测试程序源文件未找到: $SCRIPT_DIR/test_yat_casched_complete.c"
    exit 1
fi

# 编译并复制 yat_v2_2_test
echo "✓ 编译 yat_v2_2_test.c..."
if [ -f "$SCRIPT_DIR/yat_v2_2_test.c" ]; then
    gcc -static "$SCRIPT_DIR/yat_v2_2_test.c" -o bin/yat_v2_2_test
    if [ $? -eq 0 ]; then
        echo "  -> yat_v2_2_test 编译成功"
        chmod +x bin/yat_v2_2_test
    else
        echo "  -> ✗ yat_v2_2_test 编译失败！"
        exit 1
    fi
else
    echo "⚠ yat_v2_2_test.c 未找到: $SCRIPT_DIR/yat_v2_2_test.c"
    exit 1
fi

# 创建功能完整的 init 脚本
cat > init << 'EOF'
#!/bin/busybox sh

echo "=================================================================="
echo "   Yat_Casched v2.2 调度器测试环境"
echo "=================================================================="

# 设置PATH
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# 挂载基本文件系统
echo "正在初始化系统..."
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 挂载 debugfs 以支持调度器状态读取
/bin/busybox mount -t debugfs none /sys/kernel/debug

# 创建busybox符号链接
/bin/busybox --install -s /bin
/bin/busybox --install -s /sbin

echo "✓ 系统初始化完成"
echo ""

# 自动运行 v2.2 针对性测试
if [ -f "/bin/yat_v2_2_test" ]; then
    echo "=================================================================="
    echo "  自动运行 Yat_Casched v2.2 针对性测试..."
    echo "=================================================================="
    /bin/yat_v2_2_test
    echo "=================================================================="
    echo "  v2.2 测试完成。"
fi

# 保留 test_program 供手动测试
if [ -f "/bin/test_program" ]; then
    echo ""
    echo "可手动运行 /bin/test_program 进行基础功能测试。"
fi

echo ""
echo "测试已结束。可再次运行 /bin/yat_v2_2_test 或 /bin/test_program，或使用下面的 shell 进行调试。"
echo "调试命令: dmesg, ps -eo pid,class,comm"
echo ""

# 启动交互式shell
exec /bin/busybox sh
EOF

chmod +x init

find . -print0 | cpio --null -ov --format=newc | gzip -9 > initramfs_complete.cpio.gz


# === 新增：复制归档到脚本目录 ===
if [ $? -eq 0 ]; then
    echo "✓ 成功创建 initramfs_complete.cpio.gz"
    ls -lh initramfs_complete.cpio.gz
    cp initramfs_complete.cpio.gz "$SCRIPT_DIR/"
    echo "✓ 已复制到 $SCRIPT_DIR/initramfs_complete.cpio.gz"
else
    echo "✗ 创建 initramfs 失败！"
    exit 1
fi

# 清理临时目录
cd ..
rm -rf $TEMP_DIR

echo ""
echo "=================================================================="
echo "initramfs 创建完成！"
echo "使用 ./start_qemu_complete_test.sh 启动测试环境"
echo "=================================================================="
