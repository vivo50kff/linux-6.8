#!/bin/bash

echo "=== Yat_Casched 调度器改动验证快速指南 ==="
echo ""

echo "🎯 目标：验证哈希表、历史表等核心改动是否正确实现"
echo ""

echo "📋 测试步骤："
echo ""

echo "1️⃣ 启动测试环境"
echo "cd /home/yatsched/linux-6.8/yat_test_scripts"
echo "qemu-system-x86_64 -nographic -m 1024 -kernel ../vmlinux -initrd test_with_programs.cpio.gz -append \"console=ttyS0,115200\" -accel tcg"
echo ""

echo "2️⃣ 验证调度器初始化（必做）"
echo "在虚拟机终端中运行："
echo "dmesg | grep \"init yat_casched\""
echo "# 应该看到："
echo "# ======init yat_casched rq======"
echo "# ======init yat_casched rq (Decision Final Version)======"
echo "# yat_casched: Global structures initialized"
echo ""

echo "3️⃣ 测试核心功能（必做）"
echo "test_yat_core_functions"
echo "# 验证："
echo "# - 调度器正确加载"
echo "# - 进程创建和调度正常"
echo "# - 系统稳定运行"
echo ""

echo "4️⃣ 测试哈希表和历史表（必做）"
echo "test_yat_hash_operations"
echo "# 验证："
echo "# - CREATE: 进程创建触发哈希表插入"
echo "# - READ: 进程查询触发哈希表查找"
echo "# - UPDATE: 优先级修改触发哈希表更新"
echo "# - DELETE: 进程终止触发哈希表删除"
echo "# - 历史表正确记录短期进程"
echo ""

echo "5️⃣ 额外性能测试（可选）"
echo "test_yat_casched_complete    # 完整调度器测试"
echo "test_cache_aware_fixed       # 缓存感知功能测试"
echo ""

echo "✅ 成功标准："
echo "- 无 kernel panic 或系统崩溃"
echo "- 调度器初始化日志正常"
echo "- 所有测试程序正常完成"
echo "- 进程创建/销毁无异常"
echo "- 系统负载和内存使用正常"
echo ""

echo "❌ 失败排查："
echo "- 查看错误日志: dmesg | tail -20"
echo "- 检查内存使用: free"
echo "- 监控系统状态: ps aux"
echo ""

echo "🚪 退出方式："
echo "在虚拟机中输入 'exit'，然后按 Ctrl+A 再按 X"
