# 4.2.1 调度初始化

## 0. 预备工作 - 解决证书配置问题

在开始调度器开发之前，需要先解决内核编译时可能遇到的证书配置问题。

### 问题描述
编译内核时可能出现如下错误：
```
make[3]: *** 没有规则可制作目标"debian/canonical-certs.pem"，由"certs/x509_certificate_list" 需求。 停止。
make[2]: *** [scripts/Makefile.build:481：certs] 错误 2
```

### 解决方法

#### 方法1：禁用证书验证（推荐）
修改内核配置，禁用证书相关选项：

```bash
make menuconfig
```

在配置菜单中：
- 进入 `Cryptographic API` → `Certificates for signature checking`
- 取消选择或清空以下选项：
  - `File name or PKCS#11 URI of module signing key`
  - `Additional X.509 keys for default system keyring`
  - `X.509 certificates to be preloaded into system keyring`

或者直接编辑 `.config` 文件：

```bash
sed -i 's/CONFIG_MODULE_SIG_KEY="certs\/signing_key.pem"/CONFIG_MODULE_SIG_KEY=""/' .config
sed -i 's/CONFIG_SYSTEM_TRUSTED_KEYS="debian\/canonical-certs.pem"/CONFIG_SYSTEM_TRUSTED_KEYS=""/' .config
```

#### 方法2：创建空的证书文件（临时方案）
```bash
mkdir -p debian
touch debian/canonical-certs.pem
```

解决证书问题后，即可正常进行调度器开发。

在本节之前，先完成对应的部署（去年初赛P25）。。。。。。
## 添加配置选项 CONFIG_SCHED_CLASS_YAT_CASCHED 和方法 set_sched_class_yat_casched_on()

在 `init/Kconfig` 添加：

```c
config SCHED_CLASS_YAT_CASCHED
    bool "YAT_CASCHED(Cache Awareness) sched class"
    default n
    help
      A scheduling mechanism based on cache awareness.
```

定义了一个名为 `SCHED_CLASS_YAT_CASCHED` 的内核配置选项。用户可以通过 `make menuconfig` 或直接编辑 `.config` 文件来启用或禁用此调度类。如果启用（设为 y），这会允许用户创建一种采用基于 cache awareness 的固定优先级调度策略。

在 `init/main.c` 添加：

```c
unsigned int sched_class_yat_casched_on;
static int __init set_sched_class_yat_casched_on(char *str)
{
    sched_class_yat_casched_on = 1;
    return 1;
}
early_param("fully-paritioned-fixed-priority-scheduling_class", set_sched_class_yat_casched_on);
```

- `unsigned int sched_class_yat_casched_on;` 定义了一个全局变量，用于指示“fully partitioned fixed‐priority scheduling”类是否启用。
- `set_sched_class_yat_casched_on` 是初始化函数，early_param 宏将内核参数与初始化函数关联。

上述代码实现了一个可配置的调度类（YAT_CASCHED），并通过内核启动参数控制其启用状态。在内核启动时，如果检测到指定的启动参数，则会启用此调度类。这样设计便能根据需要启用或禁用特定的调度行为，而无需重新编译内核。

## 注册 Yat_Casched 调度类

可以直接模仿其他调度类的注册方式，具体步骤如下：

### 1. 创建头文件

在 `include/linux/sched` 目录下创建 `yat_casched.h` 文件，写入如下内容：

```c
#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
#include <linux/llist.h>
struct yat_casched_dispatch_q {
};
struct sched_yat_casched_entity {
    u64 slice;
};
#else /* !CONFIG_SCHED_CLASS_YAT_CASCHED */
#endif /* CONFIG_SCHED_CLASS_YAT_CASCHED */
#endif /* _LINUX_SCHED_YAT_CASCHED_H */
```

### 2. 修改链接脚本

在 `include/asm-generic/vmlinux.lds.h` 的 `SCHED_DATA` 宏添加：

```c
    *(__yat_casched_sched_class)
```

此时完整 `SCHED_DATA` 宏即为：

```c
#define SCHED_DATA \
    STRUCT_ALIGN(); \
    __sched_class_highest = .; \
    *(__stop_sched_class) \
    *(__dl_sched_class) \
    *(__rt_sched_class) \
    *(__fair_sched_class) \
    *(__yat_casched_sched_class) \
    *(__idle_sched_class) \
    __sched_class_lowest = .;
```

### 3. 添加调度策略常量

在 `include/uapi/linux/sched.h` 的 Scheduling policies 处添加#define SCHED_YAT_CASCHED 8：

```c
#define SCHED_NORMAL 0
#define SCHED_FIFO 1
#define SCHED_RR 2
#define SCHED_BATCH 3
/* SCHED_ISO: reserved but not implemented yet */
#define SCHED_IDLE 5
#define SCHED_DEADLINE 6
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
#define SCHED_YAT_CASCHED 8
#endif
```

### 4. 修改调度实体结构体

在 `include/linux/sched.h` 的调度实体结构体处添加struct sched_yat_casched_entity yat_casched;：

```c
struct sched_entity se;
 struct sched_rt_entity rt;
 struct sched_dl_entity dl;
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
 struct sched_yat_casched_entity yat_casched;
#endif
 struct sched_dl_entity *dl_server;
 const struct sched_class *sched_class;
```
同时别忘了在开头添加声明`#include<linux/sched/yat_casched.h>`
### 5. 初始化任务

在 `init/init_task.c` 添加：

```c
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
    .yat_casched = {
        .slice = 0,
    },
#endif
```

### 6. 注册调度类
修改 kernel/sched/core.c 里的__setscheduler_prio 函数，并在 for_each_possible_cpu 函数里
初始化 yat_casched 的任务队列：

```c
static void __setscheduler_prio(struct task_struct *p, int prio)
{
    if (dl_prio(prio)) {
        p->sched_class = &dl_sched_class;
    }
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
    else if (yat_casched_prio(p)) {
        p->sched_class = &yat_casched_sched_class;
        printk("\n\n======select yat_casched yat_casched yat_casched yat_casched======\n\n");
    }
#endif
    else if (rt_prio(prio)) {
        p->sched_class = &rt_sched_class;
    }
    else
        p->sched_class = &fair_sched_class;
    p->prio = prio;
}
```

在 `for_each_possible_cpu` 初始化 yat_casched 的任务队列：

```c
    init_rt_rq(&rq->rt);
    init_dl_rq(&rq->dl);
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
    printk("======init yat_casched rq======\n");
    init_yat_casched_rq(&rq->yat_casched);
#endif
```

### 7. 添加调度策略判断

在 `kernel/sched/sched.h` 写入 yat_casched 调度策略，并使其成为有效的调度策略：

```c
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
static inline int yat_casched_policy(int policy)
{
    return policy == SCHED_YAT_CASCHED;
}
#endif

static inline bool valid_policy(int policy)
{
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
    return idle_policy(policy) || fair_policy(policy) ||
        rt_policy(policy) || dl_policy(policy) || yat_casched_policy(policy);
#else
    return idle_policy(policy) || fair_policy(policy) ||
        rt_policy(policy) || dl_policy(policy);
#endif
}
```

并定义 yat_casched 任务队列 `yat_casched_rq`：

```c
#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED
struct yat_casched_rq {
    struct task_struct *agent; /* protected by e->lock and rq->lock */
};
#endif /* CONFIG_SCHED_CLASS_YAT_CASCHED */
```

### 8. 创建接口文件

创建 `kernel/sched/yat_casched.h` 写入接口声明：

```c
#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED


/* 前置声明，这些结构体在 kernel/sched/sched.h 中定义 */
struct rq;
struct rq_flags;
struct affinity_context;
struct yat_casched_rq;  // 这个在 sched.h 中已经定义了
struct sched_class;
struct sched_yat_casched_entity {
    u64 slice;
    int last_cpu;
};
/* 调度类声明 */
extern const struct sched_class yat_casched_sched_class;

/* 核心函数声明 */
extern bool yat_casched_prio(struct task_struct *p);
extern void init_yat_casched_rq(struct yat_casched_rq *rq);

/* 调度类接口函数声明 */
extern void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
extern void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
extern void yield_task_yat_casched(struct rq *rq);
extern bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p);
extern void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags);
extern struct task_struct *pick_next_task_yat_casched(struct rq *rq);
extern void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p);
extern void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first);
extern int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);
extern int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags);
extern void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx);
extern void rq_online_yat_casched(struct rq *rq);
extern void rq_offline_yat_casched(struct rq *rq);
extern struct task_struct *pick_task_yat_casched(struct rq *rq);
extern void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued);
extern void switched_to_yat_casched(struct rq *rq, struct task_struct *task);
extern void prio_changed_yat_casched(struct rq *rq, struct task_struct *task, int oldprio);
extern void update_curr_yat_casched(struct rq *rq);

#endif /* CONFIG_SCHED_CLASS_YAT_CASCHED */
#endif /* _LINUX_SCHED_YAT_CASCHED_H */
```

创建`kernel/sched/yat_casched.c` 以及末尾定义 yat_casched 调度类继承属性：

```c
#include "sched.h"  

// 核心函数实现
bool yat_casched_prio(struct task_struct *p) { 
    return p->policy == SCHED_YAT_CASCHED; // 判断是否为yat_casched策略
}

void init_yat_casched_rq(struct yat_casched_rq *rq) {
    if (rq) {
        rq->agent = NULL;
    }
}

// 调度类接口函数空实现
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags) {}
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags) {}
void yield_task_yat_casched(struct rq *rq) {}
bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p) { return false; }
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags) {}
struct task_struct *pick_next_task_yat_casched(struct rq *rq) { return NULL; }
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p) {}
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first) {}
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf) { return 0; }
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags) { return task_cpu; }
void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx) {}
void rq_online_yat_casched(struct rq *rq) {}
void rq_offline_yat_casched(struct rq *rq) {}
struct task_struct *pick_task_yat_casched(struct rq *rq) { return NULL; }
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued) {}
void switched_to_yat_casched(struct rq *this_rq, struct task_struct *task) {}
void prio_changed_yat_casched(struct rq *this_rq, struct task_struct *task, int oldprio) {}
void update_curr_yat_casched(struct rq *rq) {}

// 调度类定义
DEFINE_SCHED_CLASS(yat_casched) = {
    .enqueue_task       = enqueue_task_yat_casched,
    .dequeue_task       = dequeue_task_yat_casched,
    .yield_task         = yield_task_yat_casched,
    .yield_to_task      = yield_to_task_yat_casched,
    .wakeup_preempt     = wakeup_preempt_yat_casched,
    .pick_next_task     = pick_next_task_yat_casched,
    .put_prev_task      = put_prev_task_yat_casched,
    .set_next_task      = set_next_task_yat_casched,
#ifdef CONFIG_SMP
    .balance            = balance_yat_casched,
    .select_task_rq     = select_task_rq_yat_casched,
    .set_cpus_allowed   = set_cpus_allowed_yat_casched,
    .rq_online          = rq_online_yat_casched,
    .rq_offline         = rq_offline_yat_casched,
#endif
#ifdef CONFIG_SCHED_CORE
    .pick_task          = pick_task_yat_casched,
#endif
    .task_tick          = task_tick_yat_casched,
    .switched_to        = switched_to_yat_casched,
    .prio_changed       = prio_changed_yat_casched,
    .update_curr        = update_curr_yat_casched,
#ifdef CONFIG_UCLAMP_TASK
    .uclamp_enabled     = 0,
#endif
};




```

至此我们实现了一个空调度器，也即实现了调度的初始化
记得make menuconfig勾选 `CONFIG_SCHED_CLASS_YAT_CASCHED` 选项。

![示例图片]( 1749834558617.jpg "这是一个示例图片")

之后就能make编译了。

### yat-casched 调度器的分步编译与测试建议



1. **增量编译调度模块**
   - 如果 yat-casched 相关文件编译无误，再进行一次增量编译（如只改 .c 文件）：
     ```sh
     make -j$(nproc)
     ```
   - 这样只会重新编译受影响的目标文件和最终内核镜像。

2. **全量编译与安装**
   - 如果涉及头文件或全局接口变动，建议执行：
     ```sh
     make clean
     make -j$(nproc)
     ```

3. **后续测试流程同上**

---

通过先编译 yat_casched.o，可以在不耗费大量时间的情况下，快速验证你的调度器代码是否能通过编译。

### 9. 实现思路与关键步骤（代参考）

1. **设计数据结构**  （在linux-6.8/kernel/sched/yat_casched.c里面）
   在任务结构体（如 `struct sched_yat_casched_entity` 或 `struct task_struct`）中，增加一个字段用于记录“上次运行的CPU”：
   ```c
   struct sched_yat_casched_entity {
       u64 slice;
       int last_cpu; // 新增字段，记录上次运行的CPU
   };
   ```

2. **初始化字段**  
   在任务初始化时，将 `last_cpu` 设为 -1（表示尚未分配过CPU）：
   ```c
   void init_sched_yat_casched_entity(struct sched_yat_casched_entity *ent)
   {
       ent->slice = 0;
       ent->last_cpu = -1;
   }
   ```

3. **调度逻辑实现**  
   在 `select_task_rq_yat_casched` 或类似函数中，实现如下逻辑：
   - 如果 `last_cpu` 有效且该CPU可用，则优先分配到该CPU。
   - 否则，按默认策略分配。
   ```c
   int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
   {
       struct sched_yat_casched_entity *ent = &p->yat_casched;
       if (ent->last_cpu >= 0 && cpu_online(ent->last_cpu)) {
           return ent->last_cpu;
       }
       // 否则返回默认CPU
       return task_cpu;
   }
   ```

4. **更新历史表**  
   每次任务被实际调度到某个CPU时，更新 `last_cpu` 字段：
   ```c
   void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first)
   {
       p->yat_casched.last_cpu = cpu_of(rq);
       // ...其他逻辑...
   }
   ```

通过上述步骤，调度器能够实现“任务优先分配到上次运行的CPU”的简单调度逻辑，有助于提升缓存命中率和任务局部性。


