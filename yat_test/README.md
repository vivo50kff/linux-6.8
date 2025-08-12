# Yat_Casched 调度器专业测试环境

## 📁 目录结构

```text
yat_test/
├── 测试构建脚本
│   ├── build_yat_simple_test.sh          # 基础Yat调度器测试构建
│   ├── build_yat_simple_test_ftrace.sh   # 带ftrace支持的测试构建
│   └── build_priority_test.sh            # 优先级测试构建
├── 测试启动脚本
│   ├── start_yat_simple_test.sh          # 基础测试启动
│   ├── start_yat_simple_test_ftrace.sh   # ftrace测试启动
│   ├── start_priority_test.sh            # 优先级测试启动
│   └── start_with_arm64.sh               # ARM64架构测试启动
├── 测试程序源码
│   ├── tacle_kernel_test.c               # TACLe基准测试程序（内核态）
│   ├── tacle_kernel_test_local.c         # TACLe基准测试程序（本地版）
│   ├── priority_test.c                   # 优先级调度测试程序
│   └── get_wcet.c                        # WCET获取工具
├── 可执行文件
│   ├── tacle_kernel_test                 # 编译后的TACLe测试程序
│   ├── tacle_kernel_test_local           # 编译后的本地TACLe测试程序
│   └── get_wcet                          # 编译后的WCET获取工具
├── 测试环境
│   ├── yat_simple_test_env/              # 基础测试环境（包含initramfs完整目录）
│   ├── yat_simple_test_ftrace_env/       # ftrace测试环境（包含ftrace调试工具）
│   └── priority_test_env/                # 优先级测试环境（轻量化环境）
├── 工具脚本
│   ├── monitor_cache_performance.sh      # 缓存性能监控脚本
│   └── 挂载trace指令                      # ftrace挂载参考指令
├── 日志文件
│   ├── yat_simple_qemu.log               # 基础测试日志
│   ├── yat_simple_qemu_arm64.log         # ARM64测试日志
│   └── priority_test.log                 # 优先级测试日志
├── 压缩包
│   └── yat_simple_test.cpio.gz           # 基础测试环境压缩包
└── README.md                             # 本文档
```

## 🚀 快速开始

### 方法一：基础Yat调度器测试

1. 构建测试环境：
   ```bash
   ./build_yat_simple_test.sh
   ```

2. 启动测试：
   ```bash
   ./start_yat_simple_test.sh
   ```

### 方法二：带ftrace的调度器测试

1. 构建带ftrace支持的测试环境：
   ```bash
   ./build_yat_simple_test_ftrace.sh
   ```

2. 启动ftrace测试：
   ```bash
   ./start_yat_simple_test_ftrace.sh
   ```

### 方法三：优先级调度测试

1. 构建优先级测试环境：
   ```bash
   ./build_priority_test.sh
   ```

2. 启动优先级测试：
   ```bash
   ./start_priority_test.sh
   ```

### 方法四：ARM64架构测试

1. 直接启动ARM64测试：
   ```bash
   ./start_with_arm64.sh
   ```

## 🧪 测试程序详解

### TACLe基准测试套件

**tacle_kernel_test.c** 包含以下基准测试：

- **binarysearch**：二分查找算法
- **bitcount**：位计数算法
- **bitonic**：双调排序算法
- **bsort**：冒泡排序算法
- **complex_updates**：复杂更新操作
- **cosf**：余弦函数计算
- **countnegative**：负数计数
- **cubic**：三次方程求解
- **deg2rad**：角度转弧度
- **fac**：阶乘计算
- **fft**：快速傅里叶变换
- **filterbank**：滤波器组算法

### 优先级测试程序

**priority_test.c** 用于验证：

- 不同优先级任务的调度顺序
- 缓存感知调度的优先级处理
- 实时任务与普通任务的调度关系

### WCET获取工具

**get_wcet.c** 用于：

- 获取各个基准程序的最坏情况执行时间
- 为调度器提供WCET参考数据

## 🔧 测试环境说明

### 基础测试环境 (yat_simple_test_env)

包含：
- 完整的TACLe基准测试套件
- 基础系统工具(busybox、cat、ls等)
- 标准的init启动脚本

### ftrace测试环境 (yat_simple_test_ftrace_env)

包含：
- 所有基础测试环境的内容
- 增强的调试工具(grep、head、tail等)
- ftrace支持和相关工具

### 优先级测试环境 (priority_test_env)

包含：
- 专门的优先级测试程序
- 轻量化的系统环境
- 优先级验证工具

## 📊 性能监控与调试

### ftrace支持

使用ftrace测试环境可以：

1. 跟踪调度器行为：
   ```bash
   echo 1 > /sys/kernel/debug/tracing/events/sched/enable
   ```

2. 查看调度日志：
   ```bash
   cat /sys/kernel/debug/tracing/trace
   ```

### 缓存性能监控

使用monitor_cache_performance.sh脚本：

```bash
./monitor_cache_performance.sh
```

### debugfs调试

测试环境自动挂载debugfs，可查看：

- `/sys/kernel/debug/yat_casched/history_table`：缓存历史记录
- `/sys/kernel/debug/yat_casched/accelerator_table`：调度加速表

## 📋 日志分析

### 测试日志说明

- **yat_simple_qemu.log**：基础测试完整日志
- **yat_simple_qemu_arm64.log**：ARM64架构测试日志
- **priority_test.log**：优先级测试专项日志

### 关键指标

测试程序会输出以下关键指标：

1. **调度延迟**：任务从就绪到运行的时间
2. **缓存命中率**：任务在不同CPU上的执行效率
3. **负载均衡效果**：各CPU的负载分布情况
4. **优先级遵循度**：高优先级任务的调度优先程度

## ⚠️ 注意事项

- 确保内核已编译并生成arch/x86/boot/bzImage
- 如遇权限问题请执行 `chmod +x *.sh`
- 每次修改源码后需要重新构建测试环境
- ARM64测试需要相应的交叉编译工具链支持
- ftrace功能需要内核开启CONFIG_FTRACE相关选项

## 🛠️ 故障排除

### 常见问题

1. **QEMU启动失败**：检查bzImage是否存在
2. **测试程序无法执行**：检查文件权限和依赖
3. **ftrace不可用**：确认内核配置支持ftrace
4. **ARM64启动异常**：检查交叉编译环境

5. 使用gdb调试内核模块
6. 通过printk输出调试信息
7. 利用debugfs查看调度器状态