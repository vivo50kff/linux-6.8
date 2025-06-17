#!/bin/bash

echo "=== 简单的内核测试方案 ==="
echo ""
echo "由于 QEMU 在嵌套虚拟化环境中可能有问题，"
echo "我们提供一个替代测试方案："
echo ""

echo "方案1: 使用 chroot 测试调度器"
echo "-------------------------------"
echo "创建一个简单的 chroot 环境来测试我们的调度器"
echo ""

# 创建测试目录
mkdir -p test_chroot/{bin,lib,lib64,proc,sys,dev}

# 复制必要文件
cp test_yat_casched test_chroot/bin/ 2>/dev/null || echo "请先确保 test_yat_casched 已编译"
cp /lib/x86_64-linux-gnu/libc.so.6 test_chroot/lib/ 2>/dev/null
cp /lib64/ld-linux-x86-64.so.2 test_chroot/lib64/ 2>/dev/null
cp /lib/x86_64-linux-gnu/libpthread.so.0 test_chroot/lib/ 2>/dev/null
cp /usr/bin/busybox test_chroot/bin/ 2>/dev/null

echo "✓ 测试环境创建完成"
echo ""

echo "方案2: 直接安装新内核到系统"
echo "--------------------------------"
echo "如果您想要完整测试，可以："
echo "1. sudo make modules_install"
echo "2. sudo make install"
echo "3. 重启选择新内核"
echo ""

echo "方案3: 使用 Docker 测试"
echo "----------------------"
echo "创建一个 Docker 容器来测试："

cat > Dockerfile << 'EOF'
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential
COPY test_yat_casched /usr/bin/
COPY arch/x86/boot/bzImage /boot/
ENTRYPOINT ["/usr/bin/test_yat_casched"]
EOF

echo "✓ Dockerfile 已创建"
echo ""

echo "方案4: 检查当前内核是否已包含我们的调度器"
echo "--------------------------------------------"
echo "检查当前运行的内核版本："
uname -r

echo ""
echo "检查是否有我们的调度器符号："
if [ -f /proc/kallsyms ]; then
    grep yat_casched /proc/kallsyms || echo "当前内核中没有找到 yat_casched 符号"
else
    echo "/proc/kallsyms 不可用"
fi

echo ""
echo "直接在当前系统测试（预期会失败，因为是旧内核）："
echo "./test_yat_casched"

echo ""
echo "=== 建议的测试策略 ==="
echo "1. 如果这是生产环境，建议使用方案1（chroot）进行基本测试"
echo "2. 如果可以重启，建议使用方案2（安装新内核）"
echo "3. 如果有 Docker，建议使用方案3"
echo "4. QEMU 问题可能是由于虚拟机嵌套虚拟化限制导致"
