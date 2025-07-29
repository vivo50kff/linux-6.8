# YAT_CASCHED ARM64 测试环境

本目录包含用于测试YAT_CASCHED调度器的ARM64内核测试环境。

## 文件说明

- `yat_simple_test.c`: YAT调度器测试程序
- `build_yat_simple_test.sh`: 构建测试环境脚本
- `start_yat_simple_test.sh`: 启动QEMU ARM64测试脚本
- `yat_simple_test.cpio.gz`: 测试用initramfs镜像（构建后生成）

## 目录结构

```
linux-6.8/                    # 项目根目录
├── arch/arm64/boot/Image      # ARM64内核镜像
├── boot_test_arm64/           # ARM64测试环境
│   ├── yat_simple_test.c      # 测试程序源码
│   ├── build_yat_simple_test.sh    # 构建脚本
│   ├── start_yat_simple_test.sh    # 启动脚本
│   └── yat_simple_test.cpio.gz     # 测试镜像
└── yat_simple_test_env_arm64/ # 临时构建目录
```

## 使用方法

**重要说明**：所有脚本均使用相对路径设计，确保在正确的目录中执行命令。

## 快速开始

```bash
# 1. 编译内核（在项目根目录）
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$(nproc)

# 2. 构建和启动测试（在boot_test_arm64目录）
cd boot_test_arm64
./build_yat_simple_test.sh
./start_yat_simple_test.sh
```

### 1. 编译ARM64内核
```bash
# 从项目根目录执行
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$(nproc)
```

### 2. 构建测试环境
```bash
# 进入ARM64测试目录
cd boot_test_arm64
./build_yat_simple_test.sh
```

### 3. 启动测试
```bash
# 在boot_test_arm64目录中执行
./start_yat_simple_test.sh
```

## 测试程序功能

`yat_simple_test.c` 测试程序会：
1. 创建12个子进程
2. 每个进程执行3轮工作循环
3. 使用YAT_CASCHED调度策略
4. 测试调度器的多核负载均衡功能

## QEMU环境配置

- 机器类型: ARM64 virt
- CPU: Cortex-A57 (4核)
- 内存: 2GB
- 串口: ttyAMA0

## 预期输出

测试成功时应该看到：
```
=== YAT_CASCHED 调度器简化测试 ===
当前内核版本测试，PID: 89
父进程PID=89 成功设置为YAT_CASCHED调度策略
开始创建测试任务...
创建子任务 1，PID: 90
创建子任务 2，PID: 91
...
等待所有任务完成...
=== 所有测试任务完成 ===
```

## 故障排除

1. 如果内核镜像不存在，请在项目根目录重新编译ARM64内核：`make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image`
2. 如果QEMU启动失败，检查是否安装了 qemu-system-aarch64 包
3. 如果测试程序编译失败，检查是否安装了 gcc-aarch64-linux-gnu 包
4. 如果出现UBSAN数组越界错误，请确保调度器优先级参数在有效范围内（0-39）

## 性能优化

- 使用ARM64 Image格式（28MB）而非vmlinux（293MB），启动速度提升3-5倍
- 内核路径：`../arch/arm64/boot/Image`（相对于boot_test_arm64目录）
- 优化了任务数量从25个减少到12个，避免过度负载

## 系统要求

- Linux系统（Ubuntu 24.04 推荐）
- GCC ARM64交叉编译器（gcc-aarch64-linux-gnu）
- QEMU ARM64模拟器（qemu-system-aarch64）
- 至少4GB内存用于编译
- 内核源码支持YAT_CASCHED调度器

## 技术细节

- 调度策略编号：SCHED_YAT_CASCHED = 8
- CPU集群设置：CPU_NUM_PER_SET = 2
- 测试任务优先级：0（避免内核数组越界）
- 工作负载：CPU密集型计算 + 短暂休眠
- 内核版本：Linux 6.8.0+ 支持YAT_CASCHED
- 路径设计：所有脚本使用相对路径，便于项目迁移和部署
