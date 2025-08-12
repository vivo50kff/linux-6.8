# Yat_Casched 调度器测试环境使用指南

## 📁 目录结构

```
boot_test_scripts/
├── build_CFS_test.sh                 # CFS 调度器测试构建脚本
├── build_yat_simple_test.sh          # Yat_Casched 简化测试构建脚本
├── start_CFS_test.sh                 # CFS 调度器测试启动脚本
├── start_yat_simple_test.sh          # Yat_Casched 简化测试启动脚本
├── start_with_arm64.sh               # ARM64 架构启动脚本
├── CFS_test.c                        # CFS 调度器测试程序源码
├── yat_simple_test.c                 # Yat_Casched 简化测试程序源码
├── yat_scheduler_test.c              # Yat_Casched 调度器测试程序源码
├── CFS_test                          # CFS 测试可执行文件
├── yat_simple_test                   # Yat_Casched 简化测试可执行文件
├── CFS_test.cpio.gz                  # CFS 测试 initramfs 文件系统
├── yat_simple_test.cpio.gz           # Yat_Casched 测试 initramfs 文件系统
├── initramfs_complete.cpio.gz        # 完整测试环境文件系统
├── 挂载trace指令                      # trace 挂载指令参考
├── README.md                         # 本使用指南
└── SIMPLE_TEST_README.md             # 简化测试说明文档
```

## 🚀 使用方法

### 方法一：CFS调度器测试
1. 构建CFS测试环境：
   ```bash
   ./build_CFS_test.sh
   ```

2. 启动CFS测试：
   ```bash
   ./start_CFS_test.sh
   ```

### 方法二：Yat_Casched简化测试
1. 构建Yat_Casched测试环境：
   ```bash
   ./build_yat_simple_test.sh
   ```

2. 启动Yat_Casched测试：
   ```bash
   ./start_yat_simple_test.sh
   ```

### 方法三：ARM64架构测试
1. 启动ARM64测试环境：
   ```bash
   ./start_with_arm64.sh
   ```

## 🧪 测试程序说明

### 测试程序简介

- **CFS_test.c**：CFS调度器基准测试程序，用于性能对比。
- **yat_simple_test.c**：Yat_Casched简化功能测试程序，验证基本调度能力。
- **yat_scheduler_test.c**：Yat_Casched调度器完整功能测试程序。

### 测试内容

1. **调度策略验证**：验证SCHED_YAT_CASCHED调度策略是否正确注册和执行。
2. **缓存感知调度**：测试任务是否优先调度到上次运行的CPU上。
3. **负载均衡**：测试多CPU环境下的任务分配和负载均衡效果。
4. **性能对比**：与CFS调度器进行性能对比分析。

### debugfs调试支持

测试程序自动挂载debugfs，可查看调度器内部状态：

- `/sys/kernel/debug/yat_casched/history_table`：查看缓存历史记录
- `/sys/kernel/debug/yat_casched/accelerator_table`：查看调度加速表

## ⚠️ 注意事项

- 如遇权限问题请先 `chmod +x *.sh`。
- 这些脚本直接用QEMU虚拟机加载 bzImage 和 initramfs，无需将内核安装到主机系统。
- 只需保证 arch/x86/boot/bzImage 和 boot_test_scripts/xxx.cpio.gz 是最新编译生成版本。
- 每次修改测试程序源码后，记得重新编译并重新生成initramfs。

## 🛠️ 测试脚本详解

### CFS测试
- `build_CFS_test.sh`：编译CFS测试程序，生成CFS_test.cpio.gz
- `start_CFS_test.sh`：启动QEMU加载CFS测试环境

### Yat_Casched简化测试
- `build_yat_simple_test.sh`：编译Yat_Casched测试程序，生成yat_simple_test.cpio.gz
- `start_yat_simple_test.sh`：启动QEMU加载Yat_Casched测试环境

### ARM64支持
- `start_with_arm64.sh`：使用ARM64架构启动内核测试

## 📋 日志文件说明

- `CFS_qemu.log`：CFS测试运行日志
- `yat_simple_qemu.log`：简化测试运行日志
- `yat_simple_qemu_arm64.log`：ARM64测试运行日志

## 🔍 调试功能

测试环境支持以下调试功能：

1. **debugfs挂载**：自动挂载debugfs到/sys/kernel/debug
2. **trace支持**：参考"挂载trace指令"文件
3. **实时状态查看**：通过debugfs查看调度器内部状态
