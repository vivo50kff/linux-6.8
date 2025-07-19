# YAT_CASCHED 调度类所有接口函数调用时机详细说明

---

## 1. 调度类注册机制

`DEFINE_SCHED_CLASS(yat_casched)` 是 Linux 内核调度框架的标准注册方式。每个调度类（如 CFS、RT、YAT_CASCHED）都实现一组接口函数，内核调度器在合适的时机自动调用这些接口。

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

## 2. 所有接口函数详细调用时机和机制

### 任务生命周期相关
- **enqueue_task_yat_casched**
  - 任务被创建（如 fork）、唤醒（如 wait/信号）、迁移到本 CPU 时调用。
  - 作用：将任务加入 YAT_CASCHED 的就绪队列。
  - 由内核调度框架自动调用，如 `activate_task()`、`wake_up_new_task()`、`try_to_wake_up()` 等路径。

- **dequeue_task_yat_casched**
  - 任务被阻塞（如睡眠、等待 IO）、被迁移到其他 CPU、或被调度出队时调用。
  - 作用：将任务从 YAT_CASCHED 队列移除。
  - 由调度器如 `deactivate_task()`、`schedule()` 等自动调用。

### 任务切换相关
- **pick_next_task_yat_casched**
  - 当前 CPU 需要选择下一个要运行的任务时调用。
  - 作用：从 YAT_CASCHED 队列中选出最高优先级任务。
  - 由 `schedule()` 主调度循环自动调用。

- **put_prev_task_yat_casched**
  - 当前任务被换下 CPU 时调用。
  - 作用：保存任务状态、统计运行时间等。
  - 由 `context_switch()`、`schedule()` 自动调用。

- **set_next_task_yat_casched**
  - 新任务刚被切换到 CPU 时调用。
  - 作用：初始化任务的调度相关时间戳。
  - 由 `context_switch()` 自动调用。

### 优先级/抢占/负载均衡相关
- **prio_changed_yat_casched**
  - 任务优先级发生变化时调用（如 `sched_setscheduler`）。
- **wakeup_preempt_yat_casched**
  - 有新任务唤醒时，判断是否需要抢占当前任务。
- **balance_yat_casched**
  - 负载均衡时调用，决定是否迁移任务到其他 CPU。
- **select_task_rq_yat_casched**
  - 新任务分配 CPU 时调用，决定任务初始运行在哪个 CPU。

### 让出/迁移/亲和性相关
- **yield_task_yat_casched / yield_to_task_yat_casched**
  - 任务主动让出 CPU 或让给指定任务时调用。
- **set_cpus_allowed_yat_casched**
  - 任务 CPU 亲和性发生变化时调用。

### CPU上下线相关
- **rq_online_yat_casched / rq_offline_yat_casched**
  - CPU 上线/下线时调用，做资源初始化/清理。

### 时钟节拍/调度周期相关
- **task_tick_yat_casched**
  - 每个调度周期（时钟节拍）调用，检查时间片、决定是否需要调度。

### 其他
- **switched_to_yat_casched**
  - 任务切换到 YAT_CASCHED 调度类时调用。
- **update_curr_yat_casched**
  - 更新当前任务的虚拟运行时间，调度周期内多次调用。

---

## 3. 调用流程举例（完整）

- **fork() 新建任务**  
  → `wake_up_new_task()`  
  → `activate_task()`  
  → `enqueue_task_yat_casched`

- **任务被唤醒**  
  → `try_to_wake_up()`  
  → `enqueue_task_yat_casched`

- **任务被阻塞/睡眠**  
  → `deactivate_task()`  
  → `dequeue_task_yat_casched`

- **调度器选择任务**  
  → `schedule()`  
  → `pick_next_task_yat_casched`  
  → `set_next_task_yat_casched`  
  → `put_prev_task_yat_casched`

- **优先级变化**  
  → `sched_setscheduler()`  
  → `prio_changed_yat_casched`

- **时钟节拍**  
  → `scheduler_tick()`  
  → `task_tick_yat_casched`

- **负载均衡**  
  → `load_balance()`  
  → `balance_yat_casched`

- **CPU亲和性变化**  
  → `set_cpus_allowed_ptr()`  
  → `set_cpus_allowed_yat_casched`

- **CPU上下线**  
  → `cpu_up()`/`cpu_down()`  
  → `rq_online_yat_casched`/`rq_offline_yat_casched`

- **主动让出CPU**  
  → `yield()`  
  → `yield_task_yat_casched`

- **任务切换到YAT_CASCHED调度类**  
  → `sched_setscheduler()`/`sched_setattr()`  
  → `switched_to_yat_casched`

---

## 4. 总结
这些接口函数**无需手动调用**，只要你的任务 policy 设置为 SCHED_YAT_CASCHED，内核调度框架会在合适时机自动调用这些函数，实现任务的入队、出队、切换、抢占、负载均衡等所有调度行为。

如需进一步细节，可指定具体接口或内核路径展开。
