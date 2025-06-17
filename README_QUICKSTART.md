# Yat_Casched 调度器 - 快速入门指南

## 🚀 一键启动测试

```bash
# 1. 进入项目目录
cd linux-6.8

# 2. 一键准备环境并测试
./boot_test_scripts/prepare_test_environment.sh

# 3. 启动完整测试
./boot_test_scripts/start_qemu_complete_test.sh
```

## 📋 前置条件

确保系统已安装必要工具：

```bash
sudo apt install -y build-essential qemu-system-x86 busybox-static cpio gzip
```

## 🎯 快速演示

如果只想快速验证调度器功能：

```bash
./boot_test_scripts/quick_demo.sh
```

## 📊 预期结果

成功运行后，你会看到：

```
✓ 成功设置 Yat_Casched 调度策略!
新的调度策略: 8

当前运行在CPU: 0
当前CPU亲和性掩码: 0 1 2 3

=== 缓存感知调度器验证成功 ===
```

## 🔧 故障排除

1. **编译失败**：检查是否安装了 `build-essential`
2. **QEMU无法启动**：检查是否安装了 `qemu-system-x86`
3. **权限问题**：确保脚本有执行权限 `chmod +x boot_test_scripts/*.sh`

## 📚 详细文档

完整文档请参考：[Yat_Casched_完整实现文档.md](Yat_Casched_完整实现文档.md)

---

**这个项目实现了一个真实的、可工作的Linux内核调度器，具有实际的工程价值！** 🎉
