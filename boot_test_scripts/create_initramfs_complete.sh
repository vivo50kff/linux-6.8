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

# 复制基础测试程序
for prog in test_yat_casched_complete test_cache_aware_fixed verify_real_scheduling; do
    if [ -f "$SCRIPT_DIR/$prog" ]; then
        echo "✓ 复制 $prog"
        cp "$SCRIPT_DIR/$prog" bin/
        chmod +x bin/$prog
    else
        echo "⚠ 测试程序未找到: $SCRIPT_DIR/$prog"
    fi
    done

# 创建功能完整的 init 脚本
cat > init << 'EOF'
#!/bin/busybox sh

echo "=================================================================="
echo "   Yat_Casched 缓存感知调度器测试环境"
echo "   Linux Kernel Scheduler Implementation Test Environment"
echo "=================================================================="

# 设置PATH
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# 挂载基本文件系统
echo "正在初始化系统..."
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 创建busybox符号链接
/bin/busybox --install -s /bin
/bin/busybox --install -s /sbin

echo "✓ 系统初始化完成"
echo ""

# 显示系统信息
echo "系统信息:"
echo "  内核版本: $(uname -r)"
echo "  CPU核心数: $(nproc)"
echo "  当前时间: $(date)"
echo "  运行在CPU: $(cat /proc/cpuinfo | grep processor | wc -l) 核心"
echo ""

# 显示可用测试程序
echo "可用的测试程序:"
echo "=================================================================="
if [ -f "/bin/test_yat_casched_complete" ]; then
    echo "  1. test_yat_casched_complete  - 完整功能测试套件"
fi
if [ -f "/bin/test_cache_aware_fixed" ]; then
    echo "  2. test_cache_aware_fixed     - 缓存感知专项测试"
fi
if [ -f "/bin/verify_real_scheduling" ]; then
    echo "  3. verify_real_scheduling     - 调度器真实性验证"
fi
if [ -f "/bin/performance_test" ]; then
    echo "  4. performance_test           - 性能基准测试"
fi
echo "  5. cat /proc/cpuinfo          - 查看CPU信息"
echo "  6. cat /proc/sched_debug      - 查看调度器调试信息"
echo "  7. ps -eo pid,class,comm     - 查看进程调度类"
echo "=================================================================="
echo ""

# 自动运行基础验证
if [ -f "/bin/verify_real_scheduling" ]; then
    echo "自动运行调度器真实性验证..."
    echo "=================================================================="
    /bin/verify_real_scheduling
    echo "=================================================================="
    echo ""
fi

# 显示交互提示
echo "交互式测试环境已就绪！"
echo ""
echo "快速测试命令:"
echo "  test_yat_casched_complete  # 运行完整测试"
echo "  test_cache_aware_fixed     # 运行缓存感知测试"
echo "  verify_real_scheduling     # 重新验证调度器"
echo "  performance_test           # 运行性能基准测试"
echo ""
echo "调试命令:"
echo "  ps aux                     # 查看所有进程"
echo "  top                        # 查看实时进程信息"
echo "  cat /proc/loadavg          # 查看系统负载"
echo ""
echo "输入 'exit' 关闭系统"
echo ""

# 启动交互式shell
exec /bin/busybox sh
EOF

chmod +x init

echo "正在创建 initramfs 归档..."
find . -print0 | cpio --null -ov --format=newc | gzip -9 > initramfs_complete.cpio.gz

if [ $? -eq 0 ]; then
    echo "✓ 成功创建 initramfs_complete.cpio.gz"
    ls -lh initramfs_complete.cpio.gz
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
