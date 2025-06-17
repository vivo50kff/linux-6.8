#!/bin/bash

# 最简化的内核测试 - 只启动内核看看基本功能

echo "=== 最简化内核测试 ==="
echo "这只是测试内核是否能正常启动"
echo ""

qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -m 512 \
    -nographic \
    -append "console=ttyS0 init=/bin/sh" \
    -no-reboot
