#!/bin/bash

# VSCode 环境专用的 QEMU 启动脚本
# 解决串口冲突问题

KERNEL="arch/x86/boot/bzImage"
INITRAMFS="initramfs_fixed.cpio.gz"

echo "=== VSCode 环境 Yat_Casched 测试脚本 ==="
echo ""

# 检查必要文件
if [ ! -f "$KERNEL" ]; then
    echo "错误：内核文件 $KERNEL 不存在"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误：initramfs 文件 $INITRAMFS 不存在"
    exit 1
fi

echo "✓ 内核文件: $KERNEL ($(du -h $KERNEL | cut -f1))"
echo "✓ initramfs: $INITRAMFS ($(du -h $INITRAMFS | cut -f1))"
echo ""

echo "=== VSCode 环境配置 ==="
echo "- 输出将保存到日志文件"
echo "- 使用文件监控方式查看启动过程"
echo "- 适配 VSCode 终端环境"
echo ""

# 清理旧日志
rm -f vscode_qemu.log qemu_monitor.log

echo "启动 QEMU..."
echo "输出将保存到 vscode_qemu.log"
echo "按 Ctrl+C 停止监控"
echo ""

# 在后台启动 QEMU，输出到文件
qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 1024 \
    -smp 2 \
    -nographic \
    -accel tcg \
    -cpu qemu64 \
    -append "console=ttyS0,115200 init=/init loglevel=7 earlyprintk=serial" \
    -no-reboot \
    -serial file:vscode_qemu.log \
    -monitor telnet:localhost:4445,server,nowait \
    > qemu_stdout.log 2> qemu_stderr.log &

QEMU_PID=$!
echo "QEMU 已启动，PID: $QEMU_PID"
echo ""

# 实时监控日志文件
echo "=== 实时监控启动日志 ==="
echo "（按 Ctrl+C 停止监控，QEMU 将继续运行）"
echo ""

# 等待日志文件创建
sleep 2

if [ -f vscode_qemu.log ]; then
    # 实时显示日志
    tail -f vscode_qemu.log &
    TAIL_PID=$!
    
    # 等待用户中断
    trap "kill $TAIL_PID 2>/dev/null; echo ''; echo '停止监控'; exit 0" INT
    
    # 等待 QEMU 进程结束或用户中断
    wait $QEMU_PID 2>/dev/null
    
    # 清理
    kill $TAIL_PID 2>/dev/null
else
    echo "等待日志文件创建..."
    sleep 5
    if [ -f vscode_qemu.log ]; then
        echo "=== 启动日志内容 ==="
        cat vscode_qemu.log
    else
        echo "日志文件未创建，检查错误："
        [ -f qemu_stderr.log ] && cat qemu_stderr.log
    fi
fi

echo ""
echo "=== 后续操作 ==="
echo "1. 查看完整日志: cat vscode_qemu.log"
echo "2. 连接 QEMU 监控器: telnet localhost 4445"
echo "3. 停止 QEMU: kill $QEMU_PID"
echo "4. 检查错误日志: cat qemu_stderr.log"
