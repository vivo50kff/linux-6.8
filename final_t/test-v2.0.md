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
    1.  **主键**：所属 DAG 的优先级。
    2.  **次键**：作业自身的优先级。
    3.  **三键**：作业的 WCET（可选，用于打破僵局）。

### 1.2 加速表 (Accelerator Table)
- **功能不变**：依然是一个二维哈希表 `accel_table[job][cpu]`，用于快速查询一个作业在某个核心上的“利己”`benefit`。
- **`benefit` 计算深化 (可扩展)**：
    - **基础**：`benefit = (1 - crp_ratio) * WCET`。
    - **扩展 (源于论文)**：`crp_ratio` 的计算不仅可以考虑“是否在同一核心”，还可以考虑**空间亲和性**。例如，目标核心与上次运行的核心是否共享L2缓存？这可以作为 `recency` 计算的一个加权因子，使我们的模型更精准。

### 1.3 历史表 (History Table)
- **功能不变**：每个核心维护一个 `cpu_history_table[cpu]`，用于在 `benefit` 相同时，计算“利他”的 `Impact`，作为决胜局（Tie-Breaker）的依据。
- **实现**：依然是哈希表，以时间戳或版本号为 key，作业指针为 value。

---

## 2. 核心调度流程 (AJLR 主循环)

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

## 3. 方案优势与总结

本方案通过**层级化设计**和**引入“就绪池”**，完美对齐了 AJLR 论文的精髓，形成了一个逻辑严谨、理论坚实的决赛级调度器框架。它不再是一个简单的“任务池”调度，而是一个真正理解**程序内在依赖关系**的、面向 **DAG 作业流** 的智能调度系统。