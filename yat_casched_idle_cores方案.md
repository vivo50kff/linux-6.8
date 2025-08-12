# Yat_Casched 调度器新方案：基于全局就绪池和空闲核心状态

## 1. 核心设计思想

本方案将 `select_task_rq_yat_casched` 作为核心调度入口，实现一个两阶段的全局调度策略。调度器不再依赖传统的 `balance` 函数进行被动负载均衡，而是主动地、全局地为任务选择最佳的“空闲核心”。

- **全局就绪池**: 所有属于 `SCHED_YAT_CASCHED` 调度策略的任务在创建或唤醒时，首先被放入一个全局的、按优先级排序的就绪队列 `yat_ready_job_pool`。
- **两阶段调度**:
    1. **入队阶段**: 任务进入全局池等待。
    2. **决策与分配阶段**: 当一个任务成为全局池的**队头**，并且系统中存在**“空闲核心”**时，触发调度决策。
- **空闲核心**: “空闲核心”并非指物理上完全空闲的CPU，而是指其上 **`yat_casched` 调度类的本地运行队列长度为0**。这意味着该核心当前没有正在运行或等待运行的 `Yat_Casched` 任务。
- **全局锁**: 为了保证调度决策的原子性（从检查条件、计算最优核心到更新状态），整个决策过程由一个全局锁 `global_schedule_lock` 保护。

## 2. 关键数据结构

- `static LIST_HEAD(yat_ready_job_pool);`
  - 全局就绪任务池，存放所有待调度的 `Yat_Casched` 任务。
- `static DEFINE_SPINLOCK(yat_pool_lock);`
  - 用于保护 `yat_ready_job_pool` 的自旋锁，确保入队和出队操作的原子性。
- `static int idle_cores[8] = {1, 1, 1, 1, 1, 1, 1, 1};`
  - 全局核心状态数组，长度为8（假设8核系统）。
  - `1` 表示核心“空闲”，`0` 表示核心“繁忙”（其 `yat_casched_rq->nr_running > 0`）。
- `static DEFINE_SPINLOCK(global_schedule_lock);`
  - 全局调度决策锁。保护从 `find_best_cpu_for_task` 到更新 `idle_cores` 数组的整个过程。

## 3. 核心函数工作流程

### `select_task_rq_yat_casched` (主调度函数)

这是调度决策的核心，其工作流程如下：

1.  **初始化任务**: 检查并初始化任务的 `wcet` 等 `Yat_Casched` 相关属性。
2.  **加入全局池**:
    - 获取 `yat_pool_lock` 锁。
    - 检查任务是否已在队列中，如果不在，则根据优先级将其有序插入 `yat_ready_job_pool`。
    - 释放 `yat_pool_lock` 锁。
3.  **检查调度条件**:
    - 检查当前任务 `p` 是否为 `yat_ready_job_pool` 的队头 (`is_head_task(p)`)。
    - 检查当前是否存在空闲核心 (`has_idle_cores()`)。
4.  **执行调度决策** (如果同时满足以上两个条件):
    - **获取全局调度锁 `global_schedule_lock`**，确保决策过程不被并发执行。
    - 从全局池 `yat_ready_job_pool` 中移除该任务。
    - 调用 `find_best_cpu_for_task(p)` 计算出最佳的目标CPU `best_cpu`。
    - 如果找到有效的 `best_cpu` (`>= 0`):
        - 更新 `idle_cores[best_cpu] = 0;`，将该核心标记为“繁忙”。
    - **释放全局调度锁 `global_schedule_lock`**。
    - 返回计算出的 `best_cpu`。内核后续将调用 `enqueue_task` 将任务放置到该CPU上。
5.  **不满足调度条件**:
    - 如果任务不是队头，或当前没有空闲核心，则直接返回任务原所在的 `task_cpu`，让任务继续等待。

### `enqueue_task_yat_casched` (任务入本地队列)

此函数在内核确定目标CPU后被调用，负责将任务放入指定CPU的本地运行队列。

1.  获取目标CPU的 `rq` 和 `yat_casched_rq`。
2.  将任务 `p` 添加到 `yat_rq->tasks` 链表中。
3.  增加 `yat_rq->nr_running` 计数。
4.  **重要**: 将 `idle_cores[rq->cpu]` 更新为 `0`，因为该核心的 `Yat_Casched` 队列不再为空。

### `dequeue_task_yat_casched` (任务出本地队列)

当任务执行完毕、休眠或被迁移时，此函数被调用。

1.  从 `yat_rq->tasks` 链表中移除任务 `p`。
2.  减少 `yat_rq->nr_running` 计数。
3.  **检查并更新核心状态**:
    - 如果 `yat_rq->nr_running == 0`，说明该核心的 `Yat_Casched` 队列已空。
    - 将 `idle_cores[rq->cpu]` 更新为 `1`，标记该核心为“空闲”。
    - **（可选优化）**：当一个核心变为空闲时，可以主动检查全局池中是否有等待的任务，并尝试唤醒调度。

## 4. 辅助函数

- `is_head_task(struct task_struct *p)`: 检查 `p` 是否是 `yat_ready_job_pool` 的第一个任务。
- `has_idle_cores(void)`: 遍历 `idle_cores` 数组，检查是否存在值为 `1` 的元素。
- `find_best_cpu_for_task(struct task_struct *p)`: 遍历所有状态为 `1` 的核心，根据缓存感知模型（`calculate_recency`, `calculate_impact`）计算 `benefit` 和 `impact`，选出最佳CPU。

## 5. 整体流程图

```mermaid
graph TD
    A[任务创建/唤醒] --> B{select_task_rq_yat_casched};
    B --> C{任务是否已在全局池?};
    C -- 否 --> D[根据优先级插入 yat_ready_job_pool];
    C -- 是 --> E;
    D --> E{检查调度条件};
    E -- "是队头 & 有空闲核心" --> F[获取 global_schedule_lock];
    F --> G[从全局池移除];
    G --> H[find_best_cpu_for_task];
    H --> I[更新 idle_cores 数组];
    I --> J[释放 global_schedule_lock];
    J --> K[返回 best_cpu];
    E -- "不满足条件" --> L[返回原CPU，任务继续等待];
    
    M[内核] -- "拿到 best_cpu" --> N{enqueue_task_yat_casched};
    N --> O[加入CPU本地队列];
    O --> P[更新 idle_cores[cpu]=0];

    Q[任务结束/休眠] --> R{dequeue_task_yat_casched};
    R --> S[从本地队列移除];
    S --> T{本地队列是否为空?};
    T -- 是 --> U[更新 idle_cores[cpu]=1];
    T -- 否 --> V[结束];
    U --> V;
```
