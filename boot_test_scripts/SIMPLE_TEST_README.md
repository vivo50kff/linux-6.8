# YAT_CASCHED 简化测试脚本

这个目录包含了YAT_CASCHED调度器的简化测试环境。

## 文件说明

- `yat_simple_test.c` - 简化的调度器测试程序
- `build_yat_simple_test.sh` - 构建测试环境脚本
- `start_yat_simple_test.sh` - QEMU启动脚本

## 使用方法

### 1. 构建测试环境

```bash
cd /home/yatsched/linux-6.8/boot_test_scripts
./build_yat_simple_test.sh
```

这个脚本会：
- 编译测试程序
- 创建最小化的initramfs
- 生成 `yat_simple_test.cpio.gz` 文件

### 2. 启动测试

```bash
./start_yat_simple_test.sh
```

这个脚本会：
- 启动QEMU虚拟机
- 加载编译好的内核和测试环境
- 运行YAT_CASCHED调度器测试

### 3. 测试内容

测试程序会创建5个进程，每个进程：
- 尝试设置YAT_CASCHED调度策略
- 执行CPU密集型工作
- 显示当前运行的CPU核心
- 测试CPU迁移和调度行为

## 预期输出

成功运行时应该看到：
1. 内核启动信息
2. YAT调度器初始化消息
3. 测试程序的调度行为输出
4. CPU迁移和负载均衡信息

## 故障排除

如果遇到问题：
1. 确保内核已经成功编译
2. 检查 `vmlinux` 文件是否存在
3. 查看 `yat_simple_qemu.log` 日志文件

## 退出QEMU

在QEMU控制台中：
- 按 `Ctrl+A` 然后按 `X` 退出
- 或者在shell中输入 `halt` 命令
