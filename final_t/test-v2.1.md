# Yat_Casched 决赛版：基于 AJLR 思想的层级化 DAG 调度器

## 0. 核心思想：源于 AJLR，超越 baseline

重读 RTNS '23 论文后，我们明确最终方案应精准对齐其核心思想：**AJLR (Allocation of parallel Jobs based on Learned cache Recency)**。这不仅是一个调度算法，更是一个面向 **DAG（有向无环图）任务模型** 的、层级化的调度哲学。

因此，我们的目标是构建一个**层级化调度器**：

- **DAG 层**：遵循全局固定优先级（gFPS），高优先级 DAG 的“就绪”作业优先被调度。
- **作业层**：对“就绪”作业，利用我们设计的“**加速表**”和“**历史表**”，做出最优的（作业，核心）分配决策。

---

## 1. 关键机制与数据结构

### 1.1 就绪作业优先队列 (Ready Pool)

- **取代原“全局任务池”**：我们不再维护一个所有任务的混合池，而是维护一个“**就绪作业池**” `yat_ready_job_pool`。
- **入队条件**：一个作业（Job，即DAG中的一个节点实例）只有在其所有前驱作业都执行完毕后，才能进入“就绪池”。
- **排序规则**：
  1. **主键**：所属 DAG 的优先级。
  2. **次键**：作业自身的优先级。
  3. **三键**：作业的 WCET（可选，用于打破僵局）。

### 1.2 加速表 (Accelerator Table)

- **功能不变**：依然是一个二维哈希表 `accel_table[job][cpu]`，用于快速查询一个作业在某个核心上的“利己”`benefit`。
- **`benefit` 计算深化 (可扩展)**：
  - **基础**：`benefit = (1 - crp_ratio) * WCET`。
  - **扩展 (源于论文)**：`crp_ratio` 的计算不仅可以考虑“是否在同一核心”，还可以考虑**空间亲和性**。例如，目标核心与上次运行的核心是否共享L2缓存？这可以作为 `recency` 计算的一个加权因子，使我们的模型更精准。

### 1.3 历史表 (History Table)

- **功能不变**：每个核心维护一个 `cpu_history_table[cpu]`，用于在 `benefit` 相同时，计算“利他”的 `Impact`，作为决胜局（Tie-Breaker）的依据。
- **实现**：每个核心仅维护一个按时间戳升序排列的双向链表，链表节点保存历史记录。

---

## 2 历史表设计与高效检索方案

### 2.1 历史表结构与原理

- 每个核心维护一个 `cpu_history_table`，用于记录该核心上所有任务的历史执行信息。
- **双向链表**：用于按时间戳升序遍历，便于区间统计（如 sum_exec_time），支持高效从尾部反向累加。

#### 结构定义

```c
struct history_record {
    int task_id;           // 任务编号
    u64 timestamp;         // 执行时间戳
    u64 exec_time;         // 本次执行时长
    struct list_head list; // 双向链表节点（用于时间顺序遍历）
};

struct cpu_history_table {
    struct list_head time_list;         // 时间戳升序双向链表头
};

struct cpu_history_table history_tables[NR_CPUS];
```

---

#### 主要接口与操作流程

- 插入历史记录：
  - 新建 `history_record`，插入 `time_list`（按时间戳升序）。
- 查询某任务最近一次执行时间戳：
  - 直接用 `per_cpu_last_ts[cpu]`。
- 区间统计：
  - 用 `per_cpu_last_ts[cpu]` 作为起点，从链表尾部向前累加，直到遇到小于该时间戳的节点为止。

#### 典型接口伪代码

```c
void add_history_record(int cpu, int task_id, u64 timestamp, u64 exec_time) {
    struct history_record *rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    rec->task_id = task_id;
    rec->timestamp = timestamp;
    rec->exec_time = exec_time;
    list_add_tail(&rec->list, &history_tables[cpu].time_list);
}

u64 get_last_exec_time(int task_id, int cpu) {
    return task->per_cpu_last_ts[cpu];
}

// 从链表尾部向前累加，直到遇到小于 start_ts 的节点为止
u64 sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts) {
    u64 sum = 0;
    struct history_record *rec;
    struct list_head *pos = history_tables[cpu].time_list.prev;
    while (pos != &history_tables[cpu].time_list) {
        rec = list_entry(pos, struct history_record, list);
        if (rec->timestamp < start_ts)
            break;
        if (rec->timestamp <= end_ts)
            sum += rec->exec_time;
        pos = pos->prev;
    }
    return sum;
}
```

---

### 2.2 历史表区间与单点查询说明

- 所有区间统计、recency计算、单点查找均通过链表遍历实现。
- 区间统计：从链表尾部向前遍历，遇到timestamp < 起点即停止。
- 单点查找：可正向遍历链表，或维护per_cpu_last_ts数组直接获取。

---

## 3 Recency 多层缓存定义与计算方法

### 3.1 Recency 的定义

对于任务 i 在核 J 上的 recency（记为 recency(i, J)），它是该任务两次命中某层缓存之间，所有其他任务在该缓存上的执行时间之和。如果任务 i 从未命中过该层缓存，则 recency=理论最大时间，这里为了方便计算我们选择该任务两次命中某层缓存之间的时间差。

### 3.2 多层缓存递归计算方法

- **L1 层（私有缓存）**：
  - 查找核 J 上任务 i 上一次执行的时间戳，到本次执行之间，核 J 上所有任务的执行时间之和。
- **L2 层（共享缓存）**：
  - 查找所有与核 J 共用 L2 的核，找到这些核上任务 i 最近一次执行的时间戳（取最大值）。
  - 在该时间戳到本次执行之间，所有这些核上的任务执行时间之和。
- **L3 层（全局共享缓存）**：
  - 查找所有核上任务 i 最近一次执行的时间戳（取最大值）。
  - 在该时间戳到本次执行之间，所有核上的任务执行时间之和。
- **最终 recency**：三层 recency 之和。

#### Recency 计算伔代码

```c
u64 calculate_recency(int task_id, int cpu_id, u64 now) {
    // L1: 私有
    u64 l1_last = get_last_exec_time(task_id, cpu_id);
    u64 l1_sum = sum_exec_time_since(cpu_id, l1_last, now);

    // L2: 共享
    list_t l2_cpus = get_l2_shared_cpus(cpu_id);
    u64 l2_last = max_last_exec_time(task_id, l2_cpus);
    u64 l2_sum = sum_exec_time_multi(l2_cpus, l2_last, now);

    // L3: 全局
    u64 l3_last = max_last_exec_time(task_id, all_cpus);
    u64 l3_sum = sum_exec_time_multi(all_cpus, l3_last, now);

    return l1_sum + l2_sum + l3_sum;
}
```

---

## 4 主调度循环伪代码（整合所有结构）

```c
void yat_casched_schedule(void) {
    // 1. 选出m个最高优先级任务
    list_t candidate_tasks = get_top_m_tasks_from_pool(m);
    list_t candidate_cores = get_idle_cores();

    for (i = 0; i < m; i++) {
        // 2. 对每个候选任务和核心，计算recency和benefit
        for (task in candidate_tasks) {
            for (core in candidate_cores) {
                u64 recency = calculate_recency(task->task_id, core, now);
                u64 crp_ratio = get_crp_ratio(recency);
                u64 benefit = (1 - crp_ratio) * task->wcet;
                // ...存入加速表...
            }
        }
        // 3. MSF/LCIF分配，分配后更新历史表和per_cpu_last_ts
        // ...如前述主循环...
    }
}
```

---

## 5. 核心调度流程 (AJLR 主循环)

这是一个**事件驱动**的、在每次调度时机（如 `schedule()`）被触发的循环。

```c
// AJLR 主调度函数
void yat_casched_schedule(void) {
    // 1. 更新就绪池：检查所有活动DAG，将满足依赖的作业移入 yat_ready_job_pool
    update_ready_pool();

    // 2. 循环分配：只要就绪池不空且有空闲核心，就持续进行分配
    while (!is_ready_pool_empty() && has_idle_cpu()) {
    
        // 3. 从就绪池头部取出最高优先级的作业 A
        job_t* job_A = ready_pool_peek(); 

        // 4. 为作业 A 寻找最佳核心 K
        core_t best_core = find_best_core_for_job(job_A);

        // 5. 如果找到了合适的核心
        if (best_core != INVALID_CORE) {
            // 5.1 分配作业并移出就绪池
            assign_job_to_core(job_A, best_core);
            ready_pool_pop();

            // 5.2 更新核心状态和历史表
            update_core_status(best_core, BUSY);
            add_history_record(best_core, ktime_get(), job_A);

            // 5.3 动态更新加速表（可选，取决于实现策略）
            // 例如，与 job_A 或 best_core 相关的 benefit 值可能需要重新计算
            update_accel_table_for(job_A, best_core);

        } else {
            // 如果当前最高优先级的作业都找不到核心，则无需再试，退出循环
            break; 
        }
    }
}

// 辅助函数：为作业寻找最佳核心
core_t find_best_core_for_job(job_t* job) {
    core_t best_core = INVALID_CORE;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;

    // 候选核心列表，优先空闲核心
    list_t candidate_cores = get_candidate_cores(); 

    // --- 第一轮：比拼 Benefit ---
    // 遍历所有候选核心，找到最大的 benefit 值
    for (core in candidate_cores) {
        u64 current_benefit = accel_table_get(job, core);
        if (current_benefit > max_benefit) {
            max_benefit = current_benefit;
        }
    }

    // --- 第二轮：在最大 Benefit 集合中比拼 Impact ---
    // 遍历所有候选核心，只考虑 benefit 等于 max_benefit 的
    for (core in candidate_cores) {
        if (accel_table_get(job, core) == max_benefit) {
            // 只有在此时，才需要计算 Impact
            u64 current_impact = calculate_impact(job, core);
            if (current_impact < min_impact) {
                min_impact = current_impact;
                best_core = core;
            }
        }
    }
    return best_core;
}
```

---

## 6. 方案优势与总结

本方案通过**层级化设计**和**引入“就绪池”**，完美对齐了 AJLR 论文的精髓，形成了一个逻辑严谨、理论坚实的决赛级调度器框架。它不再是一个简单的“任务池”调度，而是一个真正理解**程序内在依赖关系**的、面向 **DAG 作业流** 的智能调度系统。

---

## 附录：完整的历史表实现示例

```c
#include <linux/list.h>

// 历史记录结构体
struct history_record {
    int task_id;
    u64 timestamp;
    u64 exec_time;
    struct list_head list;        // 用于时间顺序遍历的链表节点
};

// 每个CPU的历史表
struct cpu_history_table {
    struct list_head time_list;         // 时间戳有序链表头
    spinlock_t lock;                    // 保护并发访问
};

static struct cpu_history_table history_tables[NR_CPUS];

// 初始化历史表
void init_history_tables(void) {
    int cpu;
    for_each_possible_cpu(cpu) {
        INIT_LIST_HEAD(&history_tables[cpu].time_list);
        spin_lock_init(&history_tables[cpu].lock);
    }
}

// 添加历史记录
void add_history_record(int cpu, int task_id, u64 timestamp, u64 exec_time) {
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec) return;
    rec->task_id = task_id;
    rec->timestamp = timestamp;
    rec->exec_time = exec_time;
    spin_lock(&table->lock);
    // 直接插入链表尾部（时间戳递增）
    list_add_tail(&rec->list, &table->time_list);
    spin_unlock(&table->lock);
}

// 查询某任务最近一次执行时间戳
u64 get_task_last_timestamp(int cpu, int task_id) {
    u64 last_ts = 0;
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
  
    spin_lock(&table->lock);
    list_for_each_entry(rec, &table->time_list, list) {
        if (rec->task_id == task_id && rec->timestamp > last_ts) {
            last_ts = rec->timestamp;
        }
    }
    spin_unlock(&table->lock);
    return last_ts;
}

// 计算时间区间内的执行时间总和（使用链表，效率更高）
u64 sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts) {
    u64 sum = 0;
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
  
    spin_lock(&table->lock);
    list_for_each_entry(rec, &table->time_list, list) {
        if (rec->timestamp < start_ts) continue;
        if (rec->timestamp > end_ts) break;
        sum += rec->exec_time;
    }
    spin_unlock(&table->lock);
    return sum;
}

// 清理过期的历史记录（可选，用于内存管理）
void cleanup_old_records(int cpu, u64 cutoff_time) {
    struct history_record *rec, *tmp;
    struct cpu_history_table *table = &history_tables[cpu];
  
    spin_lock(&table->lock);
    list_for_each_entry_safe(rec, tmp, &table->time_list, list) {
        if (rec->timestamp >= cutoff_time) break;
      
        list_del(&rec->list);
        kfree(rec);
    }
    spin_unlock(&table->lock);
}
```

### 关键要点

1. **数据结构极简**：
   - 每核仅维护一个时间戳升序的双向链表，无需哈希表。
   - 结合per_cpu_last_ts数组可实现O(1)获取每核每任务最近一次执行时间。
2. **并发安全**：
   - 使用自旋锁保护链表操作
   - 在原子上下文中使用GFP_ATOMIC分配内存
3. **内存管理**：
   - 提供清理机制防止内存泄漏
   - 考虑历史记录的生命周期管理
