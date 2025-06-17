#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED

struct sched_yat_casched_entity {
    struct list_head run_list;      /* 运行队列链表节点 */
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    int last_cpu;                   /* 上次运行的CPU */
};

struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    unsigned int nr_running;        /* 运行任务数量 */
    struct task_struct *agent;     /* 代理任务 */
    u64 cache_decay_jiffies;       /* 缓存衰减时间 */
    spinlock_t history_lock;       /* 历史表锁 */
    struct task_struct *cpu_history[NR_CPUS]; /* CPU历史表 */
};

struct rq;
struct task_struct;
struct rq_flags;  /* 添加rq_flags的前向声明 */
struct affinity_context;  /* 添加affinity_context的前向声明 */

void init_yat_casched_rq(struct yat_casched_rq *rq);
bool yat_casched_prio(struct task_struct *p);
void update_curr_yat_casched(struct rq *rq);
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
struct task_struct *pick_next_task_yat_casched(struct rq *rq);
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first);
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p);
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued);
void yield_task_yat_casched(struct rq *rq);
bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p);
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags);
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);
int select_task_rq_yat_casched(struct task_struct *p, int cpu, int flags);
void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx);
void rq_online_yat_casched(struct rq *rq);
void rq_offline_yat_casched(struct rq *rq);
struct task_struct *pick_task_yat_casched(struct rq *rq);
void switched_to_yat_casched(struct rq *rq, struct task_struct *p);
void prio_changed_yat_casched(struct rq *rq, struct task_struct *p, int oldprio);

extern const struct sched_class yat_casched_sched_class;

#else

static inline void init_yat_casched_rq(struct yat_casched_rq *rq) { }

#endif /* CONFIG_SCHED_CLASS_YAT_CASCHED */

#endif /* _LINUX_SCHED_YAT_CASCHED_H */
