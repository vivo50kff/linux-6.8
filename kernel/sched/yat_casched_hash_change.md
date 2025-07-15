# Yat_Casched 历史表二维哈希化修改记录

## 修改目的
将原有的 `cpu_history[NR_CPUS]` 数组结构改为二维哈希表，每个CPU维护一个独立的哈希表记录在该CPU上执行过的任务及其执行时间戳，提升查找效率和可扩展性，支持更精细的缓存热度管理。

## 主要变更点

### 头文件 `include/linux/sched/yat_casched.h`
- 引入 `<linux/hashtable.h>`。
- 新增 `YAT_CASCHED_HASH_BITS` 宏定义，设置哈希桶数量。
- 新增 `struct yat_task_history_entry` 结构体，包含 `pid`、`exec_time`、`last_update_time`。
- 新增 `struct yat_cpu_history` 结构体，每个CPU一个独立的哈希表和锁。
- 将 `struct yat_casched_rq` 中的 `cpu_history[NR_CPUS]` 替换为 `struct yat_cpu_history cpu_history[NR_CPUS]`。

### 实现文件 `kernel/sched/yat_casched.c`
- 引入 `<linux/hashtable.h>` 和 `<linux/slab.h>`。
- 新增二维哈希表操作相关静态函数：
  - `yat_find_task_entry()`：在特定CPU的哈希表中按PID查找任务记录。
  - `yat_update_task_entry()`：更新或新建特定CPU上的任务执行记录。
  - `yat_get_task_exec_time()`：获取任务在特定CPU上的执行时间。
  - `yat_cleanup_cpu_expired_entries()`：清理特定CPU上的过期历史记录。
  - `yat_find_best_cpu_for_task()`：基于历史执行时间查找最适合的CPU。
- 修改 `init_yat_casched_rq()`，初始化每个CPU的独立哈希表和锁。
- 修改 `select_task_rq_yat_casched()`，使用二维哈希表查找执行时间最多的CPU。
- 修改 `pick_next_task_yat_casched()`，使用二维哈希表记录任务执行时间并定期清理。
- 修改 `rq_offline_yat_casched()`，下线时释放所有CPU的哈希表内存。

## 代码示例片段
```c
// 头文件 - 二维哈希表结构
struct yat_task_history_entry {
    struct hlist_node hash_node;
    pid_t pid;
    u64 exec_time;              // 在此CPU上的执行时间
    u64 last_update_time;
};

struct yat_cpu_history {
    DECLARE_HASHTABLE(task_hash, YAT_CASCHED_HASH_BITS);
    spinlock_t lock;            // 每个CPU独立的锁
    u64 last_cleanup_time;
};

struct yat_casched_rq {
    ...
    struct yat_cpu_history cpu_history[NR_CPUS]; // 每个CPU一个哈希表
};

// 查找任务在特定CPU上的历史记录
static struct yat_task_history_entry *yat_find_task_entry(struct yat_cpu_history *cpu_hist, pid_t pid) {
    ...
}

// 基于历史执行时间查找最适合的CPU
static int yat_find_best_cpu_for_task(struct yat_casched_rq *yat_rq, pid_t pid, const struct cpumask *allowed_cpus) {
    ...
}
```

## 二维哈希表设计说明
- **第一维**：按CPU编号索引 `cpu_history[cpu_id]`
- **第二维**：每个CPU内部的哈希表，以任务PID为键存储执行记录
- **优势**：
  - 细粒度锁控制，每个CPU的哈希表有独立的锁
  - 精确记录任务在每个CPU上的执行时间累计
  - 支持基于历史执行时间的智能CPU选择算法

## 优势说明
- **查找效率提升**：每个CPU独立的哈希表，查找时间复杂度为 O(1)（平均）。
- **内存效率**：只存储活跃任务在各CPU上的执行历史，按需分配。
- **精细化调度**：基于任务在各CPU上的历史执行时间进行智能调度决策。
- **并发性能**：每个CPU独立的锁，减少锁竞争，提升多核并发性能。
- **缓存热度支持**：记录执行时间戳，支持精确的缓存热度检测和过期清理。
- **可扩展性**：哈希表大小可配置，适应不同规模的系统和工作负载。

## 调度策略改进
- **CPU选择算法**：优先选择任务历史执行时间最多的CPU，提升缓存命中率。
- **负载均衡**：在缓存亲和性和负载均衡之间找到平衡点。
- **动态适应**：根据任务执行模式动态调整CPU选择策略。

## 兼容性说明
- 相关接口和调度逻辑保持兼容，原有功能均保留。
- 需确保所有CPU哈希表内存的正确释放，避免内存泄漏。
- 每个CPU独立的锁机制，需注意锁的正确使用避免死锁。
- 二维结构增加了内存开销，但提供了更精细的调度控制。

## 性能考虑
- **内存开销**：每个CPU维护独立哈希表，总内存使用量为 O(CPU数量 × 活跃任务数)。
- **锁粒度**：细粒度锁减少了锁竞争，但需要正确的锁管理。
- **清理机制**：定期清理过期记录，防止内存无限增长。

---
如需进一步优化或有疑问，请联系开发负责人。