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

## 6. 参考
- [Linux 内核文档：debugfs](https://www.kernel.org/doc/html/latest/filesystems/debugfs.html)
- [内核源码：kernel/sched/yat_casched.c]

---
如需导出更多结构或格式定制，可联系 GitHub Copilot 协助修改内核代码。
