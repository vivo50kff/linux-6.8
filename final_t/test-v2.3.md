# Yat_Casched 调度器 debugfs 调试教程

## 1. 功能说明
本教程指导如何通过 debugfs 实时查看内核 Yat_Casched 调度器的任务池内容，便于调试和分析调度决策。

## 2. 内核代码修改
已在 `kernel/sched/yat_casched.c` 中添加 debugfs 支持，自动导出当前任务池到 `/sys/kernel/debug/yat_casched/ready_pool`。

- 每次内核初始化时自动创建 debugfs 目录和文件。
- 任务池内容格式化输出，包括任务索引、PID、优先级、WCET。

## 3. 用户态查看方法
1. 确认已加载支持 debugfs 的内核。
2. 挂载 debugfs（如未挂载）：
   ```bash
   mount -t debugfs none /sys/kernel/debug
   ```
3. 查看任务池内容：
   ```bash
   cat /sys/kernel/debug/yat_casched/ready_pool
   ```
   输出示例：
   ```
   Yat_Casched Ready Pool:
   Idx | PID   | Prio | WCET
     0 |  1234 |   10 | 10000
     1 |  1235 |   20 | 10000
   Total: 2
   ```

## 4. 调试建议
- 可在调度决策前后、任务池变动时多次 cat 该文件，观察调度器行为。
- 如需导出加速表、历史表等内容，可按类似方法扩展 debugfs 文件。
- 不建议频繁 cat 或在高负载下持续采集，以免影响性能。

## 5. 关闭与清理
- 内核重启或模块卸载时自动清理 debugfs 目录。
- 如需手动清理，可执行：
   ```bash
   rm -rf /sys/kernel/debug/yat_casched
   ```

## 6. debugfs 创建失败问题分析与修复

### 6.1 问题识别
如果在 QEMU 启动后 `dmesg | grep yat` 显示：
```bash
[    0.820556] ======init yat_casched rq======
[    0.820563] [yat] debugfs init called
[    0.820565] [yat] debugfs_create_dir failed: -2
```
说明 debugfs 创建失败，错误码 `-2` 表示 `-ENOENT`。

### 6.2 根因分析
**问题原因：调度器初始化时机过早**
- 调度器初始化发生在内核启动早期（约 0.82 秒）
- 此时 debugfs 文件系统尚未挂载，导致 `debugfs_create_dir` 返回 `-ENOENT`
- 用户空间的 init 脚本会在后续才挂载 debugfs

### 6.3 解决方案：使用 late_initcall

#### 步骤1：从调度器初始化中移除 debugfs 初始化
在 `init_yat_casched_rq()` 中移除：
```c
// 移除这行
yat_debugfs_init(); // 初始化 debugfs
```

#### 步骤2：添加 late_initcall 机制
在文件末尾添加：
```c
// debugfs 延迟初始化，在所有子系统准备完毕后执行
static int __init yat_debugfs_late_init(void)
{
    printk(KERN_INFO "[yat] late debugfs init called\n");
    yat_debugfs_init();
    return 0;
}

// 使用 late_initcall，确保在 debugfs 挂载后执行
late_initcall(yat_debugfs_late_init);
```

#### 步骤3：更新错误处理逻辑
确保 debugfs 创建函数有正确的错误检测：
```c
if (IS_ERR_OR_NULL(yat_debugfs_dir)) {
    printk(KERN_ERR "[yat] debugfs_create_dir failed: %ld\n", PTR_ERR(yat_debugfs_dir));
    yat_debugfs_dir = NULL;
    return;
}
```

### 6.4 测试验证
修复后的 dmesg 输出应该显示：
```bash
[    0.831600] ======init yat_casched rq======         # 早期调度器初始化
[    3.209753] [yat] late debugfs init called          # 后期 debugfs 初始化  
[    3.215764] [yat] debugfs init called
[    3.220494] [yat] debugfs dir created successfully: 00000000f3db747d
[    3.228638] [yat] debugfs ready_pool file created: 000000006ddbfe67
```

### 6.5 数据为空的问题分析
如果 debugfs 文件创建成功但显示为空（Total: 0），说明：

1. **没有 SCHED_YAT_CASCHED 任务**
   ```bash
   ps -eo pid,comm | grep -v "0 \[" # 查看用户态进程
   ```

2. **任务未进入调度器队列**
   - 检查测试程序是否正确设置了 SCHED_YAT_CASCHED 策略
   - 检查任务是否被正确加入到 yat_ready_job_pool

3. **调度器逻辑问题**
   - 检查 `enqueue_task_yat_casched` 是否被调用
   - 检查任务池、加速表、历史表的更新逻辑

### 6.6 故障排除步骤

1. **确认 debugfs 挂载**
   ```bash
   mount | grep debugfs
   ls /sys/kernel/debug/yat_casched
   ```

2. **运行测试程序**
   ```bash
   /bin/yat_v2_2_test    # 创建 SCHED_YAT_CASCHED 任务
   ```

3. **立即查看数据**
   ```bash
   cat /sys/kernel/debug/yat_casched/ready_pool
   cat /sys/kernel/debug/yat_casched/accelerator_table
   cat /sys/kernel/debug/yat_casched/history_table
   ```

4. **检查内核日志**
   ```bash
   dmesg | grep -E "(yat|enqueue|dequeue)"
   ```

### 6.7 常见问题

**Q: 为什么所有表都显示 Total: 0？**  
A: 这通常意味着没有活跃的 SCHED_YAT_CASCHED 任务，或任务已完成并被清理。

**Q: 如何确认任务使用了正确的调度策略？**  
A: 在测试程序中添加验证代码，或在内核中添加 printk 跟踪 enqueue/dequeue 操作。

### 6.8 实际测试结果分析

基于你的测试输出：
```bash
~ # dmesg | grep yat
[    0.831600] ======init yat_casched rq======         # 调度器初始化成功
[    3.209753] [yat] late debugfs init called          # debugfs 延迟初始化成功
[    3.215764] [yat] debugfs init called
[    3.220494] [yat] debugfs dir created successfully: 00000000f3db747d
[    3.228638] [yat] debugfs ready_pool file created: 000000006ddbfe67
[    3.235775] [yat] debugfs accelerator_table file created: 0000000011a46fe4
[    3.243129] [yat] debugfs history_table file created: 00000000e2fa7400

~ # cat /sys/kernel/debug/yat_casched/ready_pool
Yat_Casched Ready Pool:
Idx | PID | Prio | WCET
Total: 0
```

**问题分析：**

1. **debugfs 创建完全成功** ✅  
   - 所有文件都正确创建，指针地址正常
   - late_initcall 机制工作正常

2. **调度器数据为空的原因** ❌
   - 用户态测试程序 `/bin/yat_v2_2_test` 运行了，但没有数据进入内核调度器
   - 这说明问题在于：任务设置了 `SCHED_YAT_CASCHED` 策略，但调度器的 `enqueue_task_yat_casched` 没有被调用，或者调用了但数据没有正确存储

### 6.9 调试步骤

**立即调试步骤：**

1. **验证测试程序是否正确运行**
   ```bash
   /bin/yat_v2_2_test    # 手动运行测试程序
   echo "Exit code: $?"  # 检查退出码
   ```

2. **在内核中添加调试输出**  
   需要在关键函数中添加 printk：
   ```c
   // 在 enqueue_task_yat_casched 函数开头
   printk(KERN_INFO "[yat] enqueue_task called for PID %d\n", p->pid);
   
   // 在 dequeue_task_yat_casched 函数开头  
   printk(KERN_INFO "[yat] dequeue_task called for PID %d\n", p->pid);
   
   // 在 pick_next_task_yat_casched 函数开头
   printk(KERN_INFO "[yat] pick_next_task called\n");
   ```

3. **重新编译并测试**
   ```bash
   make -j$(nproc)  # 重新编译内核
   # 重新打包 initramfs 并启动 QEMU
   # 运行测试并检查 dmesg
   ```

4. **验证调度器接口**  
   检查调度器是否正确注册到内核调度框架：
   ```bash
   dmesg | grep -i sched  # 查看调度器相关信息
   ```

**可能的问题源：**
- 调度策略设置成功，但内核调度框架没有调用你的调度器函数
- 任务很快完成，在 debugfs 查看之前就已经退出
- 调度器的 enqueue/dequeue 逻辑有 bug，数据没有正确存储到全局数据结构

### 6.10 完整日志分析与问题确认

基于你的完整测试日志，现在可以明确诊断问题：

```bash
==================================================================
   Yat_Casched v2.2 调度器测试环境
==================================================================
[    2.326319] [yat] balance_yat_casched called on CPU 0
[    2.353509] [yat] executing global scheduling on CPU 0
[    2.355776] [yat] schedule_yat_casched_tasks called
[    2.357786] [yat] update_accelerator_table called
[    2.359638] [yat] accelerator table updated: 0 tasks, 0 entries
[    2.362073] [yat] no suitable task found for scheduling
==================================================================
  v2.2 测试完成。
```

**关键发现：**

### 1. **调度器框架工作正常** ✅
- `balance_yat_casched` 被正确调用
- 全局调度逻辑 `schedule_yat_casched_tasks` 正确执行
- 加速表更新机制正常工作

### 2. **核心问题：任务没有进入调度器** ❌
- **关键缺失**：整个日志中没有看到 `[yat] enqueue_task called` 
- **关键缺失**：没有看到 `[yat] switched_to_yat_casched called`
- **结果**：`accelerator table updated: 0 tasks, 0 entries`

### 3. **问题根因：调度策略设置失败或任务没有被内核调度框架路由到 Yat_Casched**

**可能原因：**

1. **SCHED_YAT_CASCHED 策略编号问题**
   - 用户态测试程序中的 `SCHED_YAT_CASCHED = 8` 与内核中的定义不匹配
   - `sched_setscheduler()` 调用失败但没有错误处理

2. **调度器类没有正确注册**
   - `yat_casched_sched_class` 没有被内核调度框架识别
   - 调度器优先级设置问题

3. **任务生命周期太短**
   - 测试程序创建的任务在调度器来得及处理之前就退出了
   - fork() 的子进程没有正确设置调度策略

### 6.11 立即诊断步骤

#### 步骤1：检查调度策略定义一致性
在测试程序和内核头文件中确认 `SCHED_YAT_CASCHED` 值：
```bash
# 检查内核头文件
grep -r "SCHED_YAT_CASCHED" /home/lwd/桌面/linux-6.8/include/
grep -r "SCHED_YAT_CASCHED" /home/lwd/桌面/linux-6.8/boot_test_scripts/
```

#### 步骤2：在用户态程序中添加错误检查
修改测试程序，验证 `sched_setscheduler()` 是否成功：
```c
struct sched_param param = {0};
if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
    perror("sched_setscheduler failed");
    printf("Error: Unable to set YAT_CASCHED policy\n");
    exit(1);
}
printf("Successfully set SCHED_YAT_CASCHED policy\n");
```

#### 步骤3：验证调度器类注册
在内核中添加调度器注册确认：
```c
// 在调度器初始化时添加
printk(KERN_INFO "[yat] yat_casched_sched_class registered at priority %d\n", 
       yat_casched_sched_class.prio);
```

#### 步骤4：确认 enqueue 函数签名正确
检查 `enqueue_task_yat_casched` 函数签名是否与内核接口匹配：
```c
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
```

### 6.12 预期修复后的日志
修复后应该看到：
```bash
[yat] switched_to_yat_casched called: PID=XXX
[yat] enqueue_task called: PID=XXX, comm=XXX, prio=XXX
[yat] accelerator table updated: N tasks, M entries  # N > 0
[yat] selected best task: PID=XXX -> CPU=X, benefit=XXX
```

### 6.13 日志分析确认问题根因

从你的最新完整日志分析，问题已经明确：

```bash
==================================================================
  v2.2 测试完成。
==================================================================
```

**关键发现：**
1. ✅ **调度器框架完全正常** - debugfs 创建成功，调度器被正确调用
2. ✅ **内核编译和启动正常** - 所有初始化都成功
3. ❌ **测试程序运行了但没有输出错误信息** - 这说明可能是静默失败

**下一步诊断：立即检查测试程序**

#### 步骤1：查看测试程序源码
```bash
cat /home/lwd/桌面/linux-6.8/boot_test_scripts/yat_v2_2_test.c
```

#### 步骤2：检查调度策略定义
```bash
grep -r "SCHED_YAT_CASCHED" /home/lwd/桌面/linux-6.8/include/
```

#### 步骤3：验证调度策略编号
确认用户态程序中的 `SCHED_YAT_CASCHED = 8` 与内核定义一致。

### 6.14 预期修复方案

根据经验，最可能的问题是：

1. **调度策略编号不匹配**
   - 用户态定义 `#define SCHED_YAT_CASCHED 8`  
   - 内核定义可能不是 8，导致 `sched_setscheduler()` 失败

2. **测试程序没有错误处理**
   - `sched_setscheduler()` 失败但程序继续运行
   - 任务实际上还是 `SCHED_NORMAL` 策略

3. **权限问题**
   - 设置实时调度策略可能需要特权

#### 立即修复建议：在测试程序中添加错误检查

```c
#include <errno.h>
#include <string.h>

struct sched_param param = {0};
printf("Setting SCHED_YAT_CASCHED policy (value=%d)...\n", SCHED_YAT_CASCHED);

if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
    printf("ERROR: sched_setscheduler failed: %s (errno=%d)\n", 
           strerror(errno), errno);
    printf("Current policy: %d\n", sched_getscheduler(0));
    exit(1);
} else {
    printf("SUCCESS: SCHED_YAT_CASCHED policy set successfully\n");
    printf("Current policy: %d\n", sched_getscheduler(0));
}
```

**请先运行步骤1和步骤2的命令，让我们确认具体的调度策略定义是否匹配。**
- [Linux 内核文档：debugfs](https://www.kernel.org/doc/html/latest/filesystems/debugfs.html)
- [Linux 内核文档：initcall](https://www.kernel.org/doc/html/latest/driver-api/basics.html#c.late_initcall)
- [内核源码：kernel/sched/yat_casched.c]

---
如需导出更多结构或格式定制，可联系 GitHub Copilot 协助修改内核代码。
