# Yat_Casched 调度器 - 操作演示指南

## 📋 使用前检查清单

在开始测试前，请确认：
- [ ] 已进入项目目录：`cd linux-6.8`
- [ ] 内核已编译完成：存在 `arch/x86/boot/bzImage` 文件
- [ ] 系统已安装必要工具：`qemu-system-x86`、`busybox-static`

## 🎬 操作演示

### 演示一：完整测试流程（推荐新手）

```bash
# 步骤1：进入项目目录
cd linux-6.8

# 步骤2：一键准备环境
./boot_test_scripts/prepare_test_environment.sh
# 预期：看到编译成功和环境创建完成的消息

# 步骤3：启动测试环境
./boot_test_scripts/start_qemu_complete_test.sh
# 预期：进入QEMU虚拟机，看到调度器测试界面
```

**你会看到的效果：**
```
=== 自动化编译和测试脚本 ===
✓ test_yat_casched_complete 编译成功
✓ test_cache_aware_fixed 编译成功
✓ verify_real_scheduling 编译成功
✓ 成功创建 initramfs_complete.cpio.gz
🎉 所有准备工作完成！

[QEMU启动]
==================================================================
   Yat_Casched 缓存感知调度器测试环境
==================================================================
✓ 成功设置 Yat_Casched 调度策略!
新的调度策略: 8
```

### 演示二：快速验证（已熟悉操作）

```bash
# 如果已经运行过第一次，后续可以直接：
./boot_test_scripts/start_qemu_complete_test.sh
```

### 演示三：快速演示（课堂展示）

```bash
# 一键演示，适合时间有限的场合
./boot_test_scripts/quick_demo.sh
```

## 🔍 在测试环境中的操作

### 进入测试环境后，你可以：

#### 1. 运行基础验证
```bash
verify_real_scheduling
```
**期望结果**：看到调度策略从0变为8，证明调度器工作

#### 2. 运行缓存感知测试
```bash
test_cache_aware_fixed
```
**期望结果**：看到任务在不同CPU间的分配和迁移行为

#### 3. 运行完整功能测试
```bash
test_yat_casched_complete
```
**期望结果**：多进程、多线程全面测试

#### 4. 查看系统信息
```bash
# 查看CPU信息
cat /proc/cpuinfo

# 查看调度器调试信息
cat /proc/sched_debug | head -20

# 查看进程调度类
ps -eo pid,class,comm
```

## ⚠️ 常见问题快速解决

### 问题：脚本无法执行
```bash
# 解决：添加执行权限
chmod +x boot_test_scripts/*.sh
```

### 问题：编译失败
```bash
# 解决：安装编译工具
sudo apt install -y build-essential
```

### 问题：QEMU启动失败
```bash
# 解决：安装QEMU
sudo apt install -y qemu-system-x86
```

### 问题：找不到内核镜像
```bash
# 解决：编译内核
make -j$(nproc)
```

## 🚪 退出方法

- **正常退出QEMU**：按 `Ctrl+A` 然后按 `X`
- **强制退出**：按 `Ctrl+C`

## 🎯 成功验证标准

如果你看到以下信息，说明测试成功：

✅ **调度器真实工作**
```
✓ 成功设置 Yat_Casched 调度策略!
新的调度策略: 8
```

✅ **CPU迁移正常**
```
当前运行在CPU: 0
Task 0: CPU switch 1->0 at iteration 2
```

✅ **多进程稳定**
```
子进程 0-3: 全部成功设置调度策略
```

---

**🎉 现在你已经掌握了完整的Yat_Casched调度器测试方法！**

这个调度器是一个真实的、可工作的Linux内核实现，具有实际的工程价值和学术价值。
