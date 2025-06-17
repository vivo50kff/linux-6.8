#!/bin/bash

echo "=== 强制显示输出的内核对比测试 ==="

KERNEL_IMAGE="arch/x86/boot/bzImage"
INITRAMFS="boot_test_scripts/initramfs_complete.cpio.gz"
SYSTEM_KERNEL="/boot/vmlinuz-6.11.0-25-generic"

if [ ! -f "$KERNEL_IMAGE" ] || [ ! -f "$INITRAMFS" ]; then
    echo "缺少必要文件"
    exit 1
fi

# 函数：运行并捕获输出
run_qemu_test() {
    local title="$1"
    local kernel="$2"
    local extra_args="$3"
    
    echo ""
    echo "========================================================"
    echo "$title"
    echo "使用内核: $kernel"
    echo "========================================================"
    
    # 使用临时文件捕获输出
    local logfile="/tmp/qemu_test_$$.log"
    
    timeout 20 qemu-system-x86_64 \
        -kernel "$kernel" \
        -initrd "$INITRAMFS" \
        -m 1G \
        -smp 4 \
        -cpu qemu64 \
        -machine pc,accel=tcg \
        -nographic \
        -append "console=ttyS0 loglevel=7 rdinit=/init panic=1 maxcpus=4 $extra_args" \
        -no-reboot > "$logfile" 2>&1
    
    # 显示关键输出
    echo "启动结果："
    if [ -s "$logfile" ]; then
        echo "--- Linux版本 ---"
        grep "Linux version" "$logfile" || echo "未找到Linux版本信息"
        
        echo "--- SMP相关 ---"
        grep -E "(smp.*CPU|Allowing.*CPU)" "$logfile" || echo "未找到SMP信息"
        
        echo "--- CPU启动状态 ---"
        grep -E "(CPU.*failed|CPU.*alive|Brought up.*CPU)" "$logfile" || echo "未找到CPU启动信息"
        
        echo "--- 错误信息 ---"
        grep -E "(init_real_mode|Oops|panic|BUG)" "$logfile" || echo "未找到错误信息"
        
        echo "--- 最后几行 ---"
        tail -5 "$logfile"
    else
        echo "❌ QEMU没有任何输出！可能启动失败"
    fi
    
    rm -f "$logfile"
    echo "测试完成"
}

# 测试1: 我们的6.8内核
run_qemu_test "测试1: 我们编译的6.8内核" "$KERNEL_IMAGE" ""

# 测试2: 系统6.11内核
if [ -f "$SYSTEM_KERNEL" ]; then
    run_qemu_test "测试2: 系统6.11内核" "$SYSTEM_KERNEL" ""
else
    echo "系统内核不存在: $SYSTEM_KERNEL"
fi

# 测试3: 6.8绕过实模式
run_qemu_test "测试3: 6.8内核绕过实模式" "$KERNEL_IMAGE" "initcall_blacklist=do_init_real_mode"

echo ""
echo "========================================================"
echo "总结：对比不同内核的SMP启动能力"
echo "========================================================"
