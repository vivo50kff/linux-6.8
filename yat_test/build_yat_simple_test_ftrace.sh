#!/bin/bash
# 修复版本：避免ftrace递归和RCU问题

set -e

echo "=== 修复版TACLEBENCH kernel任务集ftrace测试环境构建 ==="

# 1. 路径定义
YAT_TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_SRC_DIR="$YAT_TEST_DIR/../tacle-bench/bench/kernel"
INITRAMFS_DIR="$YAT_TEST_DIR/yat_simple_test_ftrace_env/initramfs"
BIN_DIR="$INITRAMFS_DIR/bin"
TASKS=(binarysearch bitcount bitonic bsort complex_updates cosf countnegative cubic deg2rad fac fft filterbank)

# 2. 清理旧环境
rm -rf "$YAT_TEST_DIR/yat_simple_test_ftrace_env"

# 3. 创建目录结构
mkdir -p "$BIN_DIR" "$INITRAMFS_DIR"/{sbin,etc,proc,sys,dev,tmp,var,lib,usr/bin,usr/sbin}

# 4. 编译12个kernel任务
for idx in "${!TASKS[@]}"; do
    task="${TASKS[$idx]}"
    SRC_DIR="$KERNEL_SRC_DIR/$task"
    if ls "$SRC_DIR"/*.c 1> /dev/null 2>&1; then
        gcc -static -O2 -o "$BIN_DIR/$task" "$SRC_DIR"/*.c
        chmod +x "$BIN_DIR/$task"
        echo "  已编译: $task"
    else
        echo "  未找到: $SRC_DIR/*.c"
    fi
done

# 5. 编译主控测试程序
gcc -static -O2 -o "$BIN_DIR/tacle_kernel_test" "$YAT_TEST_DIR/tacle_kernel_test.c"
chmod +x "$BIN_DIR/tacle_kernel_test"

# 6. 生成修复版init脚本
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc none /proc 2>/dev/null || true
mount -t sysfs none /sys 2>/dev/null || true

# 挂载debugfs
mount -t debugfs none /sys/kernel/debug

echo "=== 配置ftrace追踪（修复版）==="

# 首先清理所有追踪设置
echo 0 > /sys/kernel/debug/tracing/tracing_on
echo nop > /sys/kernel/debug/tracing/current_tracer
echo > /sys/kernel/debug/tracing/set_ftrace_filter
echo > /sys/kernel/debug/tracing/set_ftrace_notrace

# 设置不追踪的函数（避免递归和RCU问题）
echo '*idle*' > /sys/kernel/debug/tracing/set_ftrace_notrace
echo '*rcu*' >> /sys/kernel/debug/tracing/set_ftrace_notrace
echo '*ftrace*' >> /sys/kernel/debug/tracing/set_ftrace_notrace
echo '*trace*' >> /sys/kernel/debug/tracing/set_ftrace_notrace

# 设置追踪缓冲区大小
echo 4096 > /sys/kernel/debug/tracing/buffer_size_kb

# 使用function追踪器而非function_graph（减少递归风险）
echo function > /sys/kernel/debug/tracing/current_tracer

# 只追踪我们的YAT调度器函数
echo 'enqueue_task_yat_casched' > /sys/kernel/debug/tracing/set_ftrace_filter
echo 'dequeue_task_yat_casched' >> /sys/kernel/debug/tracing/set_ftrace_filter
echo 'pick_next_task_yat_casched' >> /sys/kernel/debug/tracing/set_ftrace_filter
echo 'select_task_rq_yat_casched' >> /sys/kernel/debug/tracing/set_ftrace_filter

# 启用调度事件追踪（更安全的方式）
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable

echo "ftrace配置完成，开始测试..."

# 开始追踪
echo 1 > /sys/kernel/debug/tracing/tracing_on

echo "=== 启动TACLEBENCH kernel任务集测试 ==="
/bin/tacle_kernel_test

# 停止追踪
echo 0 > /sys/kernel/debug/tracing/tracing_on

echo "=== 测试完成，ftrace结果："
echo "查看追踪结果: cat /sys/kernel/debug/tracing/trace"
echo "查看追踪统计: cat /sys/kernel/debug/tracing/trace_stat/function*"

exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

# 7. 添加busybox
if command -v busybox >/dev/null 2>&1; then
    cp $(which busybox) "$BIN_DIR/"
    cd "$BIN_DIR"
    for cmd in sh ls cat echo mount umount ps kill halt reboot head tail grep; do
        ln -sf busybox $cmd
    done
fi

# 8. 生成cpio镜像
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip | sudo tee "$YAT_TEST_DIR/yat_simple_test_ftrace.cpio.gz" > /dev/null

echo "=== 修复版构建完成 ==="
echo "测试镜像: $YAT_TEST_DIR/yat_simple_test_ftrace.cpio.gz"
echo ""
echo "主要修复："
echo "1. 使用function追踪器替代function_graph（避免递归）"
echo "2. 排除idle、rcu、ftrace相关函数（避免RCU问题）"
echo "3. 只追踪YAT调度器核心函数"
echo "4. 添加调度事件追踪作为补充"