# Yat_Casched 决赛版实践方案 v1.0：最小化实现

## 目标

本方案旨在用**最小的、最可行的改动**，将“最大化加速时间效益”的核心思想融入我们现有的`yat_casched.c`代码中。我们暂时**不引入全局加速表**，而是通过**即时计算（On-the-fly Calculation）**的方式来做出调度决策，所有修改将尽可能限制在`yat_casched.c`和其相关的头文件内。

---

## 1. 数据结构修改 (必要改动)

这是唯一需要修改头文件的地方。我们需要在`struct sched_yat_casched_entity`中添加必要的字段来支持计算。

**文件**: `include/linux/sched/yat_casched.h` (或 `sched.h` 中你定义该结构体的地方)

```c
struct sched_yat_casched_entity {
    struct list_head run_list;
    u64 vruntime;
    
    /* --- 新增字段 --- */
    // 记录任务在每个CPU上最后一次被换下的时间戳
    u64 per_cpu_recency[NR_CPUS]; 
    
    // 任务的WCET，初期可以设为一个固定值或通过其他方式获取
    u64 wcet; 
    
    /* --- 保留字段，用于调试或退化逻辑 --- */
    int last_cpu;
};
```

---

## 2. 模拟核心函数 (在 yat_casched.c 中实现)

在`yat_casched.c`的顶部，我们先定义两个“桩函数”（Stub/Mock Function）来模拟CRP模型和WCET获取，让我们能聚焦于调度逻辑。

```c
// kernel/sched/yat_casched.c

/*
 * 模拟CRP模型函数
 * 输入: recency (ns) - 任务上次运行距今的时间
 * 输出: crp_ratio (0-1000) - 执行时间占WCET的千分比
 */
static u64 get_crp_ratio(u64 recency)
{
    // 这是一个简化的分段函数，模拟CRP曲线
    if (recency < 10000) // 10us以内，极热
        return 600; // 实际执行时间是WCET的60%
    if (recency < 100000) // 100us以内，温
        return 800; // 实际执行时间是WCET的80%
    
    return 1000; // 否则视为完全冷，执行时间=WCET
}

/*
 * 模拟获取任务WCET的函数
 */
static u64 get_wcet(struct task_struct *p)
{
    // 初步实现：给所有任务一个固定的WCET，例如10ms
    return 10 * 1000 * 1000; // 10ms in ns
}
```

---

## 3. 核心调度逻辑修改 (纯c文件修改)

### 3.1 `select_task_rq_yat_casched` (为任务选择CPU) - **关键改动**

当任务唤醒时，我们**即时计算**它去往每个可用CPU的`Speedup_Benefit`，并选择效益最高的那个。

```c
// kernel/sched/yat_casched.c

int select_task_rq_yat_casched(struct task_struct *p, int prev_cpu, int wake_flags)
{
    int best_cpu = -1;
    u64 max_benefit = 0;
    int i;

    // 首次运行的任务，先初始化WCET和recency
    if (p->yat_casched.wcet == 0) {
        p->yat_casched.wcet = get_wcet(p);
        for (i = 0; i < NR_CPUS; i++) {
            p->yat_casched.per_cpu_recency[i] = 0; // 设为0表示从未运行
        }
    }

    /* 遍历所有允许的CPU，计算去往每个CPU的效益 */
    for_each_cpu(i, p->cpus_ptr) {
        if (!cpu_online(i))
            continue;

        u64 recency = ktime_get() - p->yat_casched.per_cpu_recency[i];
        u64 crp_ratio = get_crp_ratio(recency); // 返回千分比
        
        // Speedup_Benefit = (1 - crp_ratio) * WCET
        u64 benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;

        if (benefit > max_benefit) {
            max_benefit = benefit;
            best_cpu = i;
        }
    }

    /* 如果所有CPU都没有收益（例如新任务），则退化为负载最轻策略 */
    if (best_cpu == -1) {
        best_cpu = select_idle_sibling(p); // 尝试找空闲CPU
        if (best_cpu == -1) {
             // 退化为初赛的负载最轻策略...
             // (此处省略，可复用初赛代码)
        }
    }
    
    return best_cpu;
}
```

### 3.2 `put_prev_task_yat_casched` (任务被换下时) - **关键改动**

当一个任务`p`在CPU `rq->cpu`上被换下时，我们必须更新它的`recency`时间戳。`put_prev_task`是一个绝佳的Hook点。

```c
// kernel/sched/yat_casched.c

void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    update_curr_yat_casched(rq); // 已有的vruntime更新
    
    /* --- 新增逻辑：更新recency时间戳 --- */
    p->yat_casched.per_cpu_recency[rq->cpu] = ktime_get();
}
```

### 3.3 `pick_next_task_yat_casched` (为CPU选择任务) - **暂时不变**

在v1.0中，我们**暂时不修改`pick_next_task`**。它仍然使用简单的FIFO策略从运行队列中取任务。因为`select_task_rq`已经保证了进入这个队列的任务都是经过“效益最大化”选择的，所以FIFO在初期是可接受的。这大大降低了实现的复杂度。

---

## 4. 总结与优势

这个v1.0方案的优势在于：

-   **改动最小**: 只需修改`sched_yat_casched_entity`结构体和`yat_casched.c`中的两个核心函数。
-   **逻辑清晰**: 完美复现了“最大化加速时间效益”的核心思想，但避免了复杂的全局数据结构和锁。
-   **可验证**: 实现后，可以通过`printk`打印`benefit`值，直观地验证调度决策是否符合预期。
-   **可迭代**: 在此版本成功运行后，下一步可以再考虑引入“加速表”来优化`pick_next_task`，或者优化`update_accelerator_table`的开销，迭代路径非常清晰。

现在，你可以按照这个方案，开始动手修改`yat_casched.c`了！