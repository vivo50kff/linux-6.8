#!/bin/bash

# 简化的 initramfs，直接运行测试程序

echo "=== 简化 Yat_Casched 测试环境 ==="

# 挂载基本文件系统
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo "环境初始化完成！"

# 直接运行测试程序
echo "运行 Yat_Casched 测试程序..."
/bin/test_yat_casched

echo "测试完成！"

# 保持系统运行
echo "进入简单 shell..."
exec /bin/busybox sh
