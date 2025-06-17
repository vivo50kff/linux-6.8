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
    struct list_head run_list;  /* 运行队列链表节点 */
    u64 slice;                   /* 时间片 */
    int last_cpu;               /* 上次运行的CPU */
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