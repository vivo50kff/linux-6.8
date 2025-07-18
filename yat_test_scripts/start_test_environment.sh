#!/bin/bash

echo "=== Yat_Casched 测试环境启动脚本 ==="
echo ""

cd /home/yatsched/linux-6.8

# 检查内核文件
if [ ! -f "vmlinux" ]; then
    echo "❌ 错误: 找不到内核文件 vmlinux"
    echo "请先编译内核：make -j$(nproc)"
    exit 1
fi

# 检查测试程序版本的 initramfs
if [ ! -f "test_with_programs.cpio.gz" ]; then
    echo "🔧 创建包含测试程序的 initramfs..."
    cd yat_test_scripts
    ./create_test_initramfs.sh
    cd ..
fi

echo "🚀 启动包含测试程序的 Yat_Casched 测试环境"
echo "   - 内核: vmlinux"
echo "   - 初始化文件系统: test_with_programs.cpio.gz (包含测试程序)"
echo "   - 内存: 1024MB"
echo ""
echo "💡 提示："
echo "   - 启动后运行: test_yat_core_functions"
echo "   - 然后运行: test_yat_hash_operations"
echo "   - 退出: 在虚拟机中输入 'exit'，然后按 Ctrl+A 再按 X"
echo ""

# 启动QEMU
echo "SeaBIOS (version 1.16.3-debian-1.16.3-2)" > /dev/null 2>&1

qemu-system-x86_64 -nographic -m 1024 -kernel vmlinux -initrd test_with_programs.cpio.gz -append "console=ttyS0,115200 noapic nolapic acpi=off" -accel tcg
