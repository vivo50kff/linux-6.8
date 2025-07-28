#!/bin/bash

# CFS 简化测试环境构建脚本
# 创建最小化的测试环境

set -e

echo "=== CFS 简化测试环境构建 ==="

# 工作目录
WORK_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_DIR="$WORK_DIR/boot_test_scripts"
TEST_DIR="$WORK_DIR/CFS_test_env/CFS_test_env"
INITRAMFS_DIR="$TEST_DIR/initramfs"

echo "清理旧的测试环境..."
rm -rf "$TEST_DIR"

echo "创建测试目录结构..."
mkdir -p "$INITRAMFS_DIR"/{bin,sbin,etc,proc,sys,dev,tmp,var,lib,usr/bin,usr/sbin}

echo "复制基本的系统文件..."
# 创建基本的设备文件
cd "$INITRAMFS_DIR"
mkdir -p dev

# 创建基本的文件系统结构
mkdir -p {proc,sys,tmp,var,run}
chmod 1777 tmp

echo "编译测试程序..."
cd "$SCRIPT_DIR"
gcc -static -o CFS_test CFS_test.c
if [ $? -eq 0 ]; then
    echo "测试程序编译成功"
    cp CFS_test "$INITRAMFS_DIR/bin/"
    chmod +x "$INITRAMFS_DIR/bin/CFS_test"
else
    echo "测试程序编译失败"
    exit 1
fi

echo "创建init脚本..."
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh

echo "=== CFS 简化测试环境启动 ==="

# 挂载基本文件系统
mount -t proc none /proc 2>/dev/null || true
mount -t sysfs none /sys 2>/dev/null || true
mkdir -p /sys/kernel/debug
mount -t debugfs none /sys/kernel/debug 2>/dev/null || true

echo "系统信息:"
echo "内核版本: $(uname -r)"
if [ -f /proc/cpuinfo ]; then
    echo "CPU信息: $(cat /proc/cpuinfo | grep 'model name' | head -1 | cut -d':' -f2)"
    echo "CPU核心数: $(nproc 2>/dev/null || echo '1')"
else
    echo "CPU信息: 不可用"
    echo "CPU核心数: 1"
fi

echo ""
echo "开始CFS调度器测试..."
echo "----------------------------------------"

# --- 新增代码：在测试前开启 ftrace ---
echo "开启 sched_switch 事件跟踪..."
# 清空旧的跟踪记录
echo > /sys/kernel/debug/tracing/trace
# 启用 sched_switch 事件
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
# --- 新增代码结束 ---

# 运行测试程序
/bin/CFS_test

# --- 新增代码：在测试后关闭 ftrace ---
echo "关闭 sched_switch 事件跟踪..."
# 关闭 sched_switch 事件
echo 0 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
# --- 新增代码结束 ---

echo "----------------------------------------"
echo "测试完成"

echo ""
echo "debugfs已挂载到 /sys/kernel/debug"
echo "您现在可以使用ftrace进行内核跟踪"
echo "例如: cd /sys/kernel/debug/tracing && echo sched_switch > current_tracer"
echo "输入 'exit' 或按 Ctrl+C 结束"

# 启动简单的shell
exec /bin/sh
EOF

chmod +x "$INITRAMFS_DIR/init"

echo "添加基本的shell命令..."
# 如果系统有busybox，使用它；否则复制基本的系统命令
if command -v busybox >/dev/null 2>&1; then
    echo "使用busybox..."
    cp $(which busybox) "$INITRAMFS_DIR/bin/"
    cd "$INITRAMFS_DIR/bin"
    for cmd in sh ls cat echo mount umount ps kill halt reboot; do
        ln -sf busybox $cmd
    done
else
    echo "复制系统命令..."
    # 复制必要的系统命令
    for cmd in /bin/sh /bin/ls /bin/cat /bin/echo /bin/ps /sbin/halt; do
        if [ -f "$cmd" ]; then
            cp "$cmd" "$INITRAMFS_DIR$cmd" 2>/dev/null || true
        fi
    done
fi

echo "创建cpio镜像..."
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip > "$SCRIPT_DIR/CFS_test.cpio.gz"

echo ""
echo "=== 构建完成 ==="
echo "测试镜像: $SCRIPT_DIR/CFS_test.cpio.gz"
echo "大小: $(du -h "$SCRIPT_DIR/CFS_test.cpio.gz" | cut -f1)"
echo ""
echo "可以使用以下命令启动测试:"
echo "cd $SCRIPT_DIR && ./start_CFS_test.sh"