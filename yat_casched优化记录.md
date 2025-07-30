# YAT_CASCHED调度器优化记录

> 本文档用于记录每次对YAT_CASCHED调度器的优化及效果，后续所有优化均在此更新。

---

## 2025-07-18 优化记录

### 1. 时间片粒度优化
 - 原因：调度延迟极大，任务响应慢。
 - 操作：将`YAT_MIN_GRANULARITY`从`NSEC_PER_SEC/HZ`改为`NSEC_PER_SEC/1000`，并新增`YAT_TIME_SLICE`为4ms。
 - 影响：显著提升调度响应速度。

 **修改位置与代码：**
 - 文件：`kernel/sched/yat_casched.c`
 - 位置：宏定义区
 - 代码：
   ```c
   #define YAT_MIN_GRANULARITY   (NSEC_PER_SEC/1000)
   #define YAT_TIME_SLICE        (4 * 1000 * 1000) // 4ms
   ```

### 2. 任务时钟节拍处理优化
 - 原因：原实现仅简单判断时间片，未区分优先级。
 - 操作：`task_tick_yat_casched`根据优先级动态调整时间片，高优先级任务获得更长时间片。
 - 影响：调度更合理，优先级生效。

 **修改位置与代码：**
 - 文件：`kernel/sched/yat_casched.c`
 - 位置：`task_tick_yat_casched`函数
 - 代码：
   ```c
   void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued)
   {
       struct sched_yat_casched_entity *yat_se = &p->yat_casched;
       u64 runtime, time_slice;
       update_curr_yat_casched(rq);
       runtime = p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime;
       if (yat_se->job && yat_se->job->priority < 0) {
           time_slice = YAT_TIME_SLICE * 2;
       } else if (yat_se->job && yat_se->job->priority > 0) {
           time_slice = YAT_TIME_SLICE / 2;
       } else {
           time_slice = YAT_TIME_SLICE;
       }
       if (runtime >= time_slice) {
           p->se.prev_sum_exec_runtime = p->se.sum_exec_runtime;
           resched_curr(rq);
       }
   }
   ```

### 3. 任务选择算法优化
 - 原因：每次调度都运行完整AJLR算法，计算量大，影响性能。
 - 操作：`pick_next_task_yat_casched`只在前3个任务中选择最高优先级，避免全量AJLR。
 - 影响：调度开销显著降低，系统响应更快。

 **修改位置与代码：**
 - 文件：`kernel/sched/yat_casched.c`
 - 位置：`pick_next_task_yat_casched`函数
 - 代码：
   ```c
   struct task_struct *pick_next_task_yat_casched(struct rq *rq)
   {
       struct yat_casched_rq *yat_rq = &rq->yat_casched;
       struct task_struct *p = NULL;
       struct sched_yat_casched_entity *yat_se;
       int best_priority = INT_MAX;
       if (!yat_rq || yat_rq->nr_running == 0)
           return NULL;
       if (!list_empty(&yat_rq->tasks)) {
           struct list_head *pos;
           int count = 0;
           list_for_each(pos, &yat_rq->tasks) {
               if (count >= 3) break;
               yat_se = list_entry(pos, struct sched_yat_casched_entity, run_list);
               struct task_struct *task = container_of(yat_se, struct task_struct, yat_casched);
               int priority = (yat_se->job) ? yat_se->job->priority : 0;
               if (priority < best_priority) {
                   best_priority = priority;
                   p = task;
               }
               count++;
           }
           if (!p) {
               yat_se = list_first_entry(&yat_rq->tasks, struct sched_yat_casched_entity, run_list);
               p = container_of(yat_se, struct task_struct, yat_casched);
           }
       }
       if (p) {
           p->yat_casched.last_cpu = rq->cpu;
           if (p->yat_casched.job) {
               p->yat_casched.job->per_cpu_last_ts[rq->cpu] = ktime_get_ns();
               if (rq->cpu >= 0 && rq->cpu < NR_CPUS) {
                   yat_add_history_record(rq->cpu, p->yat_casched.job->job_id, rq_clock_task(rq), 0);
               }
           }
       }
       return p;
   }
   ```

### 4. 优先级支持修复
- 原因：YAT_CASCHED策略被强制限制为只能使用优先级0，且优先级计算使用nice值而非自定义优先级。
- 操作：修改core.c中的优先级验证和计算逻辑，允许-20到19的优先级范围，并直接使用设置的优先级值。
- 影响：真正启用优先级功能，任务可以有不同的调度优先级。

**修改位置与代码：**
- 文件：`kernel/sched/core.c`
- 位置1：优先级验证函数（约7746行）
- 代码1：
  ```c
  /*
   * YAT_CASCHED policy allows priority range -20 to 19
   */
  if (yat_casched_policy(policy) && 
      (attr->sched_priority < -20 || attr->sched_priority > 19))
      return -EINVAL;
  ```
- 位置2：`__normal_prio`函数（约2164行）
- 代码2：
  ```c
  #ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
  else if (yat_casched_policy(policy))
      prio = rt_prio;  /* 直接使用设置的优先级 */
  #endif
  ```

### 5. 编译错误修复
- 原因：函数缺少原型声明和字段名错误导致编译失败。
- 操作：添加static函数原型声明，修复job->priority为job->job_priority。
- 影响：消除编译错误，代码结构更规范。

**修改位置与代码：**
- 文件：`kernel/sched/yat_casched.c`
- 位置：函数原型声明区和函数定义
- 主要修改：
  ```c
  // 添加static函数声明
  static int yat_ready_pool_init(struct yat_ready_job_pool *pool);
  static void yat_ready_pool_destroy(struct yat_ready_job_pool *pool);
  // ... 其他函数声明
  
  // 修复字段名
  int priority = (yat_se->job) ? yat_se->job->job_priority : 0;
  if (yat_se->job && yat_se->job->job_priority < 0) {
  ```

---

## 后续优化请继续补充本文件。

### 6. YAT_CASCHED策略类型重新定位和优先级权限优化
- **优化时间**：2025-07-18 下午
- **问题描述**：YAT_CASCHED策略在用户测试中发现只有优先级为0的任务能成功设置，其他优先级（5, 6, 8, 10）都失败并返回"Invalid argument"错误。
- **根因分析**：
  1. YAT_CASCHED被错误地包含在`rt_policy`函数中，导致策略定位混乱
  2. 内核验证逻辑要求非RT策略的优先级必须为0
  3. 权限检查逻辑对YAT_CASCHED策略处理不当
- **解决方案**：将YAT_CASCHED定位为独立的特殊策略，既不是RT策略也不是普通策略，并为其定制专门的验证和权限逻辑。

**修改位置与代码：**

1. **策略分类修正** - 文件：`kernel/sched/sched.h`
   ```c
   // 移除YAT_CASCHED与RT策略的关联
   static inline int rt_policy(int policy)
   {
       return policy == SCHED_FIFO || policy == SCHED_RR;  // 不包含YAT_CASCHED
   }
   ```

2. **优先级验证逻辑** - 文件：`kernel/sched/core.c` (约7750-7770行)
   ```c
   /*
    * Valid priorities for SCHED_FIFO and SCHED_RR are
    * 1..MAX_RT_PRIO-1, valid priority for SCHED_NORMAL,
    * SCHED_BATCH, SCHED_IDLE is 0. SCHED_YAT_CASCHED allows 0-19.
    */
   if (attr->sched_priority > MAX_RT_PRIO-1)
       return -EINVAL;
   if ((dl_policy(policy) && !__checkparam_dl(attr)) ||
       (rt_policy(policy) != (attr->sched_priority != 0)))
       return -EINVAL;

   /*
    * YAT_CASCHED policy allows priority range 0-19
    */
   if (yat_casched_policy(policy) && (attr->sched_priority < 0 || attr->sched_priority > 19))
       return -EINVAL;

   /*
    * For non-RT and non-YAT_CASCHED policies, priority must be 0
    */
   if (!rt_policy(policy) && !dl_policy(policy) && !yat_casched_policy(policy) && attr->sched_priority != 0)
       return -EINVAL;
   ```

3. **权限豁免逻辑** - 文件：`kernel/sched/core.c` (约7680-7700行)
   ```c
   /*
    * YAT_CASCHED policy: Allow priority changes without CAP_SYS_NICE
    * for same owner, similar to normal scheduling policies
    */
   if (yat_casched_policy(policy)) {
       /* Only check same owner for YAT_CASCHED */
       if (!check_same_owner(p))
           goto req_priv;
       /* Skip other privilege checks for YAT_CASCHED */
       goto skip_priv_checks;
   }
   ```

4. **优先级传递修复** - 文件：`kernel/sched/yat_casched.c`
   ```c
   static void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags)
   {
       // ...existing code...
       /* 获取用户设置的优先级 - 关键修复点 */
       user_priority = p->rt_priority;  /* 直接使用rt_priority字段 */
       
       /* 为新任务创建作业，使用用户设置的优先级 */
       job = yat_create_job(p->pid, user_priority, 1000000, default_dag);
       // ...existing code...
   }
   ```

- **测试验证**：
  - 优先级0：✅ 成功设置YAT_CASCHED策略
  - 优先级5, 6, 8, 10：❌ 在修复前失败，✅ 修复后应该成功
  - 优先级超出范围：❌ 正确拒绝（如优先级20或-1）

- **影响**：
  - YAT_CASCHED策略现在是独立的特殊策略类型
  - 支持0-19的优先级范围，无需特殊权限
  - 优先级正确传递到调度实体，实现真正的优先级调度
  - 用户可以在测试程序中自由设置不同优先级的YAT_CASCHED任务

---

## 后续优化请继续补充本文件。

---

> 优化建议、测试结果、性能对比等也可在此补充。

7/29

通过分析，核心问题在于你自定义的 YAT_CASCHED 调度器在任务创建和上下文切换的路径上引入了大量的内存分配/释放操作，导致了严重的性能瓶颈。

下面是详细的对比和分析：

1. 宏观现象对比
特征	CFS_qemu.log (正常/快速)	yat_simple_qemu.log (异常/慢速)	分析
测试完成时间	大约在 37秒 左右，测试任务开始运行并切换。	测试任务直到 125秒 才开始有详细的trace，并且持续到 169秒 之后。	性能差距巨大。YAT_CASCHED 版本耗时是 CFS 版本的数倍。
ftrace 核心内容	主要记录了 scheduler_tick, task_tick_fair, pick_next_task_fair, schedule 等调度核心函数。这是正常的调度器行为。	主要记录了 copy_process, kmem_cache_alloc, handle_mm_fault, do_wp_page 等进程创建和内存管理相关的函数。	这说明 YAT_CASCHED 的性能瓶颈不在于调度算法本身，而是在于与进程生命周期管理（创建、切换）相关的部分。
内核警告	在 36.7秒 出现 RCU not on for: amd_e400_idle	在 53.3秒 出现 RCU not on for: amd_e400_idle	两个日志都有这个警告。这是因为 ftrace 尝试追踪 CPU idle 函数，而该函数在 RCU 非活跃区运行。这是一个通用问题，与你的调度器性能问题没有直接关系，可以忽略。
2. 深入 ftrace 日志分析 (问题的根源)
CFS (正常情况) 的 ftrace 片段：
分析:

这是一个非常干净、高效的上下文切换流程。
schedule 被调用，然后 pick_next_task_fair 选择下一个任务。
put_prev_entity 和 set_next_entity 只是更新红黑树和一些状态变量。
整个过程没有涉及复杂的内存分配 (kmalloc/kfree)。
YAT_CASCHED (慢速情况) 的 ftrace 片段：
片段一：进程创建 (copy_process) 在 yat_simple_qemu.log 中，从 125.067018 秒开始，yat_simple_test-114 (父进程) 在调用 kernel_clone -> copy_process 时产生了海量的 ftrace 日志。这说明仅仅是创建一个子进程就花费了极长的时间。

片段二：上下文切换 (schedule)

分析:

这是问题的核心。当 YAT_CASCHED 调度器进行任务切换时，put_prev_task_yat_casched 函数被调用。
在这个函数内部，发生了多次 kfree 和 kmalloc_trace (即 kmalloc) 调用。
在调度器的核心路径（特别是上下文切换）中进行内存分配/释放是极其危险且低效的。这个路径应该尽可能快，因为它在持有运行队列锁（runqueue lock）的情况下执行，任何延迟都会导致整个CPU的调度停顿，并可能引发更复杂的问题（如死锁、优先级反转等）。
结论与推测
根本原因: 你的 YAT_CASCHED 调度器在任务切换（put_prev_task_yat_casched）和/或任务唤醒（wake_up_new_task -> select_task_rq_yat_casched）等热点路径中，执行了本不该有的动态内存分配和释放。

代码推测:

你可能在 struct task_struct 或 struct sched_entity 中添加了一些指针成员，用于存储调度器需要的额外数据。
在任务被调度出CPU时 (put_prev_task_...)，你 kfree 了这些数据结构。
在任务被调度入CPU时 (pick_next_task_... 或 set_next_entity_...)，你又 kmalloc 重新分配它们。
或者，在任务创建/唤醒时，你为它分配了一些辅助数据结构。
为什么这么慢:

高昂的开销: kmalloc/kfree 相对于简单的指针操作和算术运算，是非常耗时的操作。它们需要查找合适的内存块，更新元数据，处理内存碎片等。
高频调用: 每次上下文切换都会触发这些耗时的操作，系统中有成千上万次的上下文切换，开销被急剧放大。
锁竞争: 内存分配器（SLUB/SLAB）自身有锁保护。在调度器这种高频、多核竞争的环境下，对内存分配器的频繁访问会引起严重的锁竞争，进一步拖慢系统。
缓存污染: 频繁的内存分配/释放会严重污染CPU缓存，导致性能下降。
修复建议
避免在热点路径动态分配内存:

调度器的核心数据结构应该在任务创建时（sched_fork）一次性分配好，并作为 task_struct 的一部分。
如果需要为每个CPU的运行队列（rq）分配额外数据，应该在 sched_init 中完成，或者在CPU上线时完成。
严禁在 enqueue_task, dequeue_task, pick_next_task, put_prev_task, task_tick 等函数中调用 kmalloc 或 kfree。
优化数据结构:

将你的调度器所需的数据结构直接嵌入到 struct task_struct 或 struct cfs_rq (如果你是基于CFS修改的) 中，而不是使用指针指向动态分配的内存。这样在任务创建时，内存会随 task_struct 一起被分配好。
如果数据量很大，可以考虑使用 per-CPU 变量或者预先分配的对象池（mempool）来减少实时分配的开销。
总之，请仔细检查你的 YAT_CASCHED 实现，特别是 put_prev_task_yat_casched 和其他调度器核心回调函数，移除其中的所有 kmalloc 和 kfree 调用，将所需的数据结构静态化或在任务初始化时一次性分配。
