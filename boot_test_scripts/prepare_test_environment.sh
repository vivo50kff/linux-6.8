#!/bin/bash

echo "=== 自动化编译和测试脚本 ==="
echo "此脚本将自动编译所有测试程序并创建测试环境"
echo ""

# 移动到项目根目录
# 不需要切换目录，假设从linux-6.8目录执行此脚本

# 编译所有测试程序
echo "1. 编译测试程序..."
echo "=================================================================="

PROGRAMS=(
    "test_yat_casched_complete.c:test_yat_casched_complete"
    "test_cache_aware_fixed.c:test_cache_aware_fixed"
    "verify_real_scheduling.c:verify_real_scheduling"
)

for prog in "${PROGRAMS[@]}"; do
    src="${prog%:*}"
    target="${prog#*:}"
    
    if [ -f "$src" ]; then
        echo "编译 $src -> $target"
        gcc -static -pthread -o "$target" "$src"
        if [ $? -eq 0 ]; then
            echo "✓ $target 编译成功"
        else
            echo "✗ $target 编译失败"
            exit 1
        fi
    else
        echo "⚠ 源文件未找到: $src"
    fi
done

echo ""
echo "2. 创建测试环境..."
echo "=================================================================="

# 创建 initramfs
if [ -f "boot_test_scripts/create_initramfs_complete.sh" ]; then
    chmod +x boot_test_scripts/create_initramfs_complete.sh
    ./boot_test_scripts/create_initramfs_complete.sh
else
    echo "✗ initramfs 创建脚本未找到"
    exit 1
fi

echo ""
echo "3. 验证环境..."
echo "=================================================================="

# 检查必要文件
FILES_TO_CHECK=(
    "arch/x86/boot/bzImage"
    "boot_test_scripts/initramfs_complete.cpio.gz"
    "boot_test_scripts/start_qemu_complete_test.sh"
)

ALL_GOOD=true
for file in "${FILES_TO_CHECK[@]}"; do
    if [ -f "$file" ]; then
        echo "✓ $file 存在"
    else
        echo "✗ $file 缺失"
        ALL_GOOD=false
    fi
done

if [ "$ALL_GOOD" = true ]; then
    echo ""
    echo "=================================================================="
    echo "🎉 所有准备工作完成！"
    echo "=================================================================="
    echo ""
    echo "现在可以启动测试:"
    echo "  ./boot_test_scripts/start_qemu_complete_test.sh"
    echo ""
    echo "或者手动启动:"
    echo "  cd boot_test_scripts"
    echo "  ./start_qemu_complete_test.sh"
    echo ""
    echo "测试程序说明:"
    echo "  • verify_real_scheduling     - 验证调度器真实工作"
    echo "  • test_yat_casched_complete  - 完整功能测试"
    echo "  • test_cache_aware_fixed     - 缓存感知专项测试"
    echo ""
else
    echo ""
    echo "✗ 环境准备失败，请检查上述缺失文件"
    exit 1
fi
