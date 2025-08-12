# YAT_CASCHED 调度器核心代码结构

---

## 1. 文件位置
- 主要文件：`kernel/sched/yat_casched.c`
- 头文件：`include/linux/sched/yat_casched.h`

---

## 2. 主要数据结构
- `struct yat_casched_rq`：每CPU的 YAT_CASCHED 运行队列
- `struct sched_yat_casched_entity`：任务调度实体，嵌入在 `task_struct` 中
- `struct yat_job`：作业对象，支持 DAG/优先级/历史等
- `struct yat_dag_task`：DAG对象，管理作业依赖
- `struct yat_ready_job_pool`：就绪作业池，优先队列
- `struct yat_accel_table`：加速表，(job,cpu)→benefit
- `struct yat_cpu_history_table`：历史表，记录任务执行时间戳

---

## 3. 关键函数
### 初始化与管理
- `init_yat_casched_rq(struct yat_casched_rq *rq)`：初始化每CPU调度器结构
- `yat_ready_pool_init/destroy/enqueue/dequeue/peek/empty/remove`：就绪池管理
- `yat_create_dag/yat_destroy_dag`：DAG对象管理
- `yat_create_job/yat_destroy_job`：作业对象管理
- `yat_job_add_dependency/yat_job_complete`：作业依赖与完成
- `yat_init_history_tables/yat_destroy_history_tables/yat_add_history_record`：历史表管理
- `yat_accel_table_init/destroy/insert/update/lookup/delete/cleanup`：加速表管理

### 调度器接口（与调度框架对接）
- `enqueue_task_yat_casched`：任务入队
- `dequeue_task_yat_casched`：任务出队
- `pick_next_task_yat_casched`：选择下一个任务
- `put_prev_task_yat_casched`：放置前一个任务
- `set_next_task_yat_casched`：设置下一个任务
- `task_tick_yat_casched`：时钟节拍处理
- `select_task_rq_yat_casched`：选择任务CPU
- `balance_yat_casched`：负载均衡
- `yield_task_yat_casched/yield_to_task_yat_casched`：让出CPU
- `set_cpus_allowed_yat_casched`：设置CPU亲和性
- `rq_online_yat_casched/rq_offline_yat_casched`：CPU上下线
- `switched_to_yat_casched/prio_changed_yat_casched`：切换/优先级变化
- `update_curr_yat_casched`：更新虚拟运行时间

### AJLR算法相关
- `yat_schedule_ajlr`：主调度循环
- `yat_update_ready_pool`：就绪池更新
- `yat_find_best_core_for_job`：为作业分配最佳核心
- `yat_calculate_recency/yat_get_crp_ratio/yat_calculate_impact`：缓存新鲜度与影响力计算

---

## 4. 关键调度流程
1. **任务入队**：`enqueue_task_yat_casched` → 关联作业 → 加入队列
2. **任务出队**：`dequeue_task_yat_casched` → 更新作业状态/历史
3. **选择任务**：`pick_next_task_yat_casched` → 优先级/作业信息选择
4. **任务切换**：`put_prev_task_yat_casched`/`set_next_task_yat_casched`
5. **时钟节拍**：`task_tick_yat_casched` → 时间片/抢占
6. **AJLR主循环**：`yat_schedule_ajlr` → DAG/作业/核心分配

---

## 5. 典型代码片段
```c
// 任务入队
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags) {
    // ...初始化与入队...
    yat_ajlr_enqueue_task(rq, p, flags);
}

// 选择下一个任务
struct task_struct *pick_next_task_yat_casched(struct rq *rq) {
    // ...优先级选择...
    // ...printk 输出进入CPU信息...
}

// 放置前一个任务
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p) {
    // ...printk 输出离开CPU信息...
}
```

---

## 6. 调度类注册
```c
DEFINE_SCHED_CLASS(yat_casched) = {
    .enqueue_task       = enqueue_task_yat_casched,
    .dequeue_task       = dequeue_task_yat_casched,
    .yield_task         = yield_task_yat_casched,
    .yield_to_task      = yield_to_task_yat_casched,
    .wakeup_preempt     = wakeup_preempt_yat_casched,
    .pick_next_task     = pick_next_task_yat_casched,
    .put_prev_task      = put_prev_task_yat_casched,
    .set_next_task      = set_next_task_yat_casched,
    .balance            = balance_yat_casched,
    .select_task_rq     = select_task_rq_yat_casched,
    .set_cpus_allowed   = set_cpus_allowed_yat_casched,
    .rq_online          = rq_online_yat_casched,
    .rq_offline         = rq_offline_yat_casched,
    .pick_task          = pick_task_yat_casched,
    .task_tick          = task_tick_yat_casched,
    .switched_to        = switched_to_yat_casched,
    .prio_changed       = prio_changed_yat_casched,
    .update_curr        = update_curr_yat_casched,
};
```

---

## 7. 常见问题排查建议
- 检查任务是否设置为 SCHED_YAT_CASCHED
- 检查调度器注册和入口函数是否被调用
- 检查内核日志是否有相关 printk 输出
- 检查测试程序是否正确设置调度策略

---

如需进一步细节，可指定具体结构体或函数展开。
