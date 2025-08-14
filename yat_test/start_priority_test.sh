#!/bin/bash
# YAT_CASCHED 优先级调度测试启动脚本

set -e

echo "=== YAT_CASCHED 优先级调度测试启动 ==="

# 配置路径
WORK_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL="$WORK_DIR/../vmlinux"
INITRAMFS="$WORK_DIR/priority_test.cpio.gz"
LOG_FILE="$WORK_DIR/priority_test.log"

# 检查文件
if [ ! -f "$KERNEL" ]; then
    echo "错误: 内核文件不存在: $KERNEL"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误: 测试镜像不存在，请运行: ./build_priority_test.sh"
    exit 1
fi

echo "内核: $KERNEL"
echo "测试镜像: $INITRAMFS"
echo "日志: $LOG_FILE"

# 简化的QEMU配置
QEMU_ARGS=(
    -nographic
    -m 1024
    -smp 4
    -kernel $KERNEL
    -initrd $INITRAMFS
    -append "console=ttyS0,115200 noapic"   
    -accel tcg
)

echo ""
echo "测试说明:"
echo "1. 系统将创建5个关键进程(高优先级)和10个普通进程(低优先级)"
echo "2. 观察执行顺序是否体现优先级差异"
echo "3. 关键进程应该先于普通进程执行"
echo ""

echo "启动测试 (按 Ctrl+A 然后 X 退出)..."

# 启动QEMU
qemu-system-x86_64 "${QEMU_ARGS[@]}" 2>&1 | tee "$LOG_FILE"

echo ""
echo "测试完成！"
echo "分析执行顺序: grep -E '(关键进程|普通进程).*执行' $LOG_FILE"
# echo "查看完整日志: cat $LOG_FILE"