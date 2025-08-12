# TACLeBench 详细说明文档

## 目录
- [项目概述](#项目概述)
- [快速开始](#快速开始)
- [编译系统](#编译系统)
- [基准测试结构](#基准测试结构)
- [使用教程](#使用教程)
- [故障排除](#故障排除)
- [贡献指南](#贡献指南)

## 项目概述

TACLeBench 是一个用于时序分析和实时系统研究的基准测试套件。它包含了多种类型的基准测试程序，用于评估最坏情况执行时间（WCET）分析工具的性能。

### 主要特性
- 支持多种架构编译（主机 x86、ARM Cortex-M4）
- 包含 4 个主要类别的基准测试
- 提供统一的编译和管理系统
- 包含 WCET 分析注解

## 快速开始

### 前置要求
```bash
# 主机编译（必需）
sudo apt install gcc make

# ARM 交叉编译（可选）
sudo apt install gcc-arm-none-eabi
```

### 基本编译
```bash
# 编译所有基准测试（主机版本）
make all-host

# 编译特定类别
make sequential-host    # 仅编译 sequential 基准测试
make kernel-host       # 仅编译 kernel 基准测试

# 清理编译产物
make clean
```

## 编译系统

### Makefile 结构

我们的顶层 Makefile 提供了统一的编译管理系统：

```makefile
# 编译器配置
CC_HOST = gcc                    # 主机编译器
CC_ARM = arm-none-eabi-gcc      # ARM 交叉编译器
CFLAGS = -O2 -Wall -Wextra -Wno-unknown-pragmas

# ARM 交叉编译选项
ARM_FLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
```

### 编译目标

#### 主机版本编译
```bash
make all-host          # 编译所有基准测试
make app-host          # 仅编译 app 类别
make kernel-host       # 仅编译 kernel 类别
make parallel-host     # 仅编译 parallel 类别
make sequential-host   # 仅编译 sequential 类别
```

#### ARM 版本编译
```bash
make all-arm          # 编译所有基准测试（ARM）
make app-arm          # 仅编译 app 类别（ARM）
make kernel-arm       # 仅编译 kernel 类别（ARM）
make parallel-arm     # 仅编译 parallel 类别（ARM）
make sequential-arm   # 仅编译 sequential 类别（ARM）
```

#### 工具命令
```bash
make clean           # 清理所有编译产物
make help           # 显示帮助信息
```

### 编译输出

编译成功后，可执行文件将生成在对应的基准测试目录中：
- **主机版本**：`*.host` 文件
- **ARM 版本**：`*.elf` 文件

例如：
```
bench/sequential/susan/susan.host
bench/kernel/sha/sha.host
bench/app/powerwindow/powerwindow.elf
```

## 基准测试结构

### 目录组织
```
bench/
├── app/              # 应用程序级基准测试
├── kernel/           # 内核级基准测试
├── parallel/         # 并行处理基准测试
└── sequential/       # 顺序处理基准测试
```

### 各类别说明

#### 1. Sequential（顺序处理）
包含传统的单线程算法：
- **susan**: 图像处理算法
- **dijkstra**: 最短路径算法
- **crc**: 循环冗余校验
- **adpcm**: 音频编码/解码

#### 2. Kernel（内核级）
包含底层系统功能：
- **sha**: 安全哈希算法
- **matrix1**: 矩阵运算
- **prime**: 素数计算
- **bitcount**: 位计数算法

#### 3. App（应用程序级）
包含完整的应用程序：
- **powerwindow**: 汽车电动窗控制系统

#### 4. Parallel（并行处理）
包含并行和多任务程序：
- **DEBIE**: 太空碎片检测系统
- **PapaBench**: 无人机控制系统

## 使用教程

### 基本使用流程

#### 1. 环境准备
```bash
# 进入项目目录
cd ~/Yat_CAsched/tacle-bench

# 检查可用的编译目标
make help
```

#### 2. 编译基准测试
```bash
# 编译所有主机版本基准测试
make all-host

# 或者选择性编译
make sequential-host    # 仅编译顺序处理类
```

#### 3. 运行基准测试
```bash
# 运行 susan 图像处理基准测试
./bench/sequential/susan/susan.host \
    bench/sequential/data/susan_input_small.pgm \
    output.pgm -s

# 运行 SHA 算法基准测试
./bench/kernel/sha/sha.host

# 运行 CRC 校验基准测试
./bench/sequential/crc/crc.host
```

#### 4. 查看结果
大多数基准测试会：
- 输出执行时间信息
- 生成结果文件（如图像处理的输出图片）
- 返回校验和用于验证正确性

### 高级使用

#### ARM 交叉编译
```bash
# 编译 ARM 版本
make all-arm

# ARM 可执行文件需要在 ARM 平台或模拟器中运行
# 例如使用 QEMU：
qemu-arm ./bench/kernel/sha/sha.elf
```

#### 自定义编译选项
可以通过环境变量覆盖默认编译选项：
```bash
# 使用不同的优化级别
make all-host CFLAGS="-O3 -Wall -Wextra -Wno-unknown-pragmas"

# 添加调试信息
make all-host CFLAGS="-O2 -g -Wall -Wextra -Wno-unknown-pragmas"
```

#### 并行编译
```bash
# 使用多核并行编译（需要修改 Makefile 支持）
make -j4 all-host
```

### 性能测试示例

#### 1. 测量执行时间
```bash
# 使用 time 命令测量
time ./bench/sequential/susan/susan.host \
    bench/sequential/data/susan_input_small.pgm \
    output.pgm -s

# 使用更精确的测量工具
perf stat ./bench/kernel/sha/sha.host
```

#### 2. 内存使用分析
```bash
# 使用 valgrind 分析内存使用
valgrind --tool=memcheck \
    ./bench/sequential/susan/susan.host \
    bench/sequential/data/susan_input_small.pgm \
    output.pgm -s
```

#### 3. 批量测试脚本
```bash
#!/bin/bash
# 创建批量测试脚本
for benchmark in bench/sequential/*/*.host; do
    echo "测试: $benchmark"
    time $benchmark
    echo "---"
done
```

## 故障排除

### 常见编译问题

#### 1. 编译器警告
```
warning: ignoring '#pragma loopbound min' [-Wunknown-pragmas]
```
**解决方案**：这些警告是正常的，这些 pragma 是用于 WCET 分析工具的注解。已在 Makefile 中添加 `-Wno-unknown-pragmas` 选项来抑制这些警告。

#### 2. 找不到 ARM 编译器
```
arm-none-eabi-gcc: command not found
```
**解决方案**：
```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
```

#### 3. 编译失败
如果某个基准测试编译失败，Makefile 会继续编译其他测试（使用 `|| true`）。检查具体错误信息：
```bash
# 单独编译有问题的基准测试
gcc -O2 -Wall -Wextra -Wno-unknown-pragmas \
    bench/path/to/benchmark/*.c \
    -o benchmark.host
```

### 运行时问题

#### 1. 缺少输入文件
某些基准测试需要输入数据文件，确保数据文件存在：
```bash
ls bench/sequential/data/
```

#### 2. 权限问题
```bash
# 确保可执行文件有执行权限
chmod +x bench/sequential/susan/susan.host
```

### 性能分析

#### WCET 分析注解说明
TACLeBench 包含的特殊注解用于时序分析：
- `_Pragma("loopbound min X max Y")`: 指定循环边界
- `_Pragma("entrypoint")`: 标记分析入口点
- `_Pragma("marker")`: 程序点标记
- `_Pragma("flowrestriction")`: 流约束

这些注解被专门的 WCET 分析工具（如 AbsInt aiT、OTAWA 等）识别和使用。

## 贡献指南

### 添加新的基准测试
1. 在适当的类别目录下创建新文件夹
2. 添加 C 源文件和必要的头文件
3. 确保代码包含适当的 WCET 分析注解
4. 测试编译和运行

### 修改编译系统
1. 编辑顶层 `makefile`
2. 保持向后兼容性
3. 更新文档

### 提交规范
1. 清晰的提交信息
2. 包含测试结果
3. 更新相关文档

---

## 联系信息

如有问题或建议，请通过以下方式联系：
- 项目仓库：https://github.com/tacle/tacle-bench
- 邮件：tacle-bench-dev@lists.tacle.eu

---

*最后更新：2024年*
