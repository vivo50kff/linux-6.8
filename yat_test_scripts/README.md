# Yat_Casched 调度器测试环境教程

## 🚀 快速开始

### ⭐ 推荐：直接启动测试环境（包含所有测试程序）
```bash
cd /home/yatsched/linux-6.8/yat_test_scripts
./start_test_environment.sh
```

### 手动启动方式
```bash
# 确保测试程序 initramfs 存在
./create_test_initramfs.sh

# 手动启动（推荐使用上面的自动脚本）
qemu-system-x86_64 -nographic -m 1024 -kernel ../vmlinux -initrd ../test_with_programs.cpio.gz -append "console=ttyS0,115200" -accel tcg
```

### 基础环境（仅 shell，无测试程序）
```bash
./start_qemu_simple.sh    # 仅基础环境，适合手动测试
```

## 🧪 核心功能测试

### 必做测试（验证改动）

**1. 核心功能测试**
```bash
test_yat_core_functions
```
- 验证调度器初始化
- 测试进程调度基本功能
- 检查缓存感知调度

**2. 哈希表/历史表测试**
```bash
test_yat_hash_operations  
```
- 测试哈希表 CRUD 操作
- 验证历史表记录功能
- 性能压力测试

**3. 专门历史表测试**
```bash
test_yat_history_table
```
- 专门测试历史表增删改查
- 验证历史记录插入、查询、更新、删除
- 批量操作和性能测试

### 完整测试套件

**3. 性能测试**
```bash
test_yat_casched_complete   # 完整调度器测试
test_cache_aware_fixed      # 缓存感知测试
verify_real_scheduling      # 实时调度验证
```

## 📋 测试验证要点

### ✅ 成功标准
- 调度器初始化无错误：`dmesg | grep yat_casched`
- 哈希表操作正常：进程创建/销毁无异常
- 历史表记录工作：短期进程正确记录
- 系统稳定运行：无 kernel panic
- 缓存感知生效：内存访问优化

### ⚠️ 关键检查点
```bash
# 查看调度器启动日志
dmesg | grep "init yat_casched rq"

# 检查数据结构初始化
dmesg | grep "Global structures initialized"

# 监控系统状态
ps aux
top
free
```

## �️ 环境准备

### 依赖安装
```bash
sudo apt update
sudo apt install busybox-static gcc cpio gzip qemu-system-x86
```

### 编译测试程序
```bash
cd /home/yatsched/linux-6.8
gcc -O2 -static -o boot_test_scripts/test_yat_core_functions boot_test_scripts/test_yat_core_functions.c
gcc -O2 -static -o boot_test_scripts/test_yat_hash_operations boot_test_scripts/test_yat_hash_operations.c
```

## 🎯 使用方式

### 基础测试（终端环境）
进入虚拟机后运行基本命令：
```bash
cat /proc/version    # 查看内核版本
ps aux              # 查看进程
dmesg | grep yat    # 查看调度器日志
```

### 专业测试（完整程序）
运行专门的测试程序验证改动：
```bash
test_yat_core_functions     # 🔥 核心功能
test_yat_hash_operations    # 🔥 数据结构
```

## ⚠️ 重要提示

- **退出**: `exit` → `Ctrl+A` → `X`
- **核心日志**: `dmesg | grep "init yat_casched rq"`
- **必测项目**: `test_yat_core_functions` + `test_yat_hash_operations`

## � 故障排查

| 问题 | 解决方案 |
|------|---------|
| busybox 找不到 | `sudo apt install busybox-static` |
| KVM 不可用 | 自动降级 TCG，正常使用 |
| 权限错误 | `chmod +x *.sh` |
| 编译失败 | 检查 gcc 和源码文件 |
