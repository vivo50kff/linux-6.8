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

## 1.1 接口函数原型（返回类型与参数）

| 接口函数名 | 返回类型 | 输入参数 |
|:---|:---|:---|
| enqueue_task_yat_casched | void | struct rq *rq, struct task_struct *p, int flags |
| dequeue_task_yat_casched | void | struct rq *rq, struct task_struct *p, int flags |
| yield_task_yat_casched | void | struct rq *rq |
| yield_to_task_yat_casched | bool | struct rq *rq, struct task_struct *p |
| wakeup_preempt_yat_casched | void | struct rq *rq, struct task_struct *p, int flags |
| pick_next_task_yat_casched | struct task_struct * | struct rq *rq |
| put_prev_task_yat_casched | void | struct rq *rq, struct task_struct *p |
| set_next_task_yat_casched | void | struct rq *rq, struct task_struct *p, bool first |
| balance_yat_casched | int | struct rq *rq, struct task_struct *prev, struct rq_flags *rf |
| select_task_rq_yat_casched | int | struct task_struct *p, int cpu, int flags |
| set_cpus_allowed_yat_casched | void | struct task_struct *p, struct affinity_context *ctx |
| rq_online_yat_casched | void | struct rq *rq |
| rq_offline_yat_casched | void | struct rq *rq |
| pick_task_yat_casched | struct task_struct * | struct rq *rq |
| task_tick_yat_casched | void | struct rq *rq, struct task_struct *p, int queued |
| switched_to_yat_casched | void | struct rq *rq, struct task_struct *p |
| prio_changed_yat_casched | void | struct rq *rq, struct task_struct *p, int oldprio |
| update_curr_yat_casched | void | struct rq *rq |

---

## 1.2 接口参数和返回值详细说明

- `struct rq *rq`：表示“运行队列”，每个CPU都有自己的rq结构体，管理该CPU上的所有调度信息（如就绪队列、当前任务、负载等）。
- `struct task_struct *p`：表示一个具体的任务（进程或线程），包含所有进程相关信息（如PID、优先级、状态、调度实体等）。
- `int flags`：调度相关的标志位，通常用于区分唤醒、迁移、抢占等不同场景。
- `bool first`：用于标记是否是本次调度周期的第一个任务。
- `struct rq_flags *rf`：调度器内部用于锁和状态保护的结构体。
- `struct affinity_context *ctx`：用于描述任务的CPU亲和性变更。

### 输出说明
- 返回值为 `void` 的函数：只做状态更新或队列操作，无需返回结果。
- 返回值为 `int` 的函数：通常返回CPU编号或迁移/负载均衡的结果。
- 返回值为 `struct task_struct *` 的函数：返回下一个要运行的任务。
- 返回值为 `bool` 的函数：返回是否允许抢占或让出CPU。

### rq结构体说明
- `rq`（runqueue）是每个CPU独立拥有的，内核会为每个CPU分配一个 `struct rq`。
- 所有调度器操作都是针对当前CPU的rq进行，不会跨CPU直接操作。
- 例如：`enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)`，表示把任务p加入当前CPU的就绪队列。

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

## 3.1 fork() 新建任务的完整内核调度流程

1. 用户空间调用 `fork()` 系统调用
2. 进入内核 `SYSCALL_DEFINE0(fork)`（kernel/fork.c）
   - 构造参数，调用 `kernel_clone(&args)`
3. `kernel_clone(struct kernel_clone_args *args)`
   - 调用 `copy_process(NULL, trace, NUMA_NO_NODE, args)`
4. `copy_process()`
   - 复制父进程所有信息，分配新 `task_struct`
   - 调用 `sched_fork(clone_flags, p)`（kernel/sched/core.c）
5. `sched_fork()`
   - 调用 `__sched_fork(clone_flags, p)`，初始化调度相关字段（CFS、RT、YAT_CASCHED等）
6. 唤醒新任务 `wake_up_new_task(p)`
   - 选择目标CPU：`select_task_rq(p, task_cpu(p), WF_FORK)`
   - 设置新任务的CPU：`__set_task_cpu(p, ...)`
   - 获取目标CPU的 `rq`：`rq = __task_rq_lock(p, &rf)`
   - 激活任务：`activate_task(rq, p, ENQUEUE_NOCLOCK)`
7. `activate_task(rq, p, flags)`
   - 调用 `enqueue_task(rq, p, flags)`
   - 进一步调用调度类的 `enqueue_task_yat_casched(rq, p, flags)` 或其他调度器的 `enqueue_task`

**说明：**
- 目标CPU由 `select_task_rq` 决定，最终将新任务插入该CPU的 `rq` 队列。
- 该流程适用于所有调度类（CFS、RT、YAT_CASCHED等），调度器只需实现好接口即可。

---

## 4. 总结
这些接口函数**无需手动调用**，只要你的任务 policy 设置为 SCHED_YAT_CASCHED，内核调度框架会在合适时机自动调用这些函数，实现任务的入队、出队、切换、抢占、负载均衡等所有调度行为。

如需进一步细节，可指定具体接口或内核路径展开。
