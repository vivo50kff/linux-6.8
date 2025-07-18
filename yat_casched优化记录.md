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

---

> 优化建议、测试结果、性能对比等也可在此补充。
