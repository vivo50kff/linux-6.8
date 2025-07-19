#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED

/* 常量定义 */
#define YAT_INVALID_CORE        (-1)
#define YAT_MAX_DAG_PRIORITY    100

/* 作业状态枚举 */
enum yat_job_state {
    YAT_JOB_WAITING = 0,
    YAT_JOB_READY,
    YAT_JOB_RUNNING,
    YAT_JOB_COMPLETED
};

/* 加速表哈希位数 (2^8 = 256个桶，用于(task,cpu)组合) */
#define YAT_ACCEL_HASH_BITS    8
/* 历史表哈希位数 (2^7 = 128个桶，用于时间戳) */
#define YAT_HISTORY_HASH_BITS  7

/* 前向声明哈希表节点 */
struct hlist_node;

/* 加速表条目：存储(task,cpu)→benefit映射 */
struct yat_accel_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    pid_t task_pid;                /* 任务PID */
    int job_id;                    /* 作业ID (兼容性) */
    int cpu_id;                    /* CPU编号 */
    u64 benefit;                   /* 加速值（benefit） */
    u64 wcet;                      /* 任务的WCET */
    u64 crp_ratio;                 /* CRP比率（千分比表示，避免浮点） */
    u64 last_update_time;          /* 最后更新时间 */
};

/* 加速表：二维哈希表，支持(task,cpu)→benefit查找 */
struct yat_accel_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 全局锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
};

/* 历史表条目：存储时间戳→任务映射 */
struct yat_history_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    u64 timestamp;                 /* 时间戳作为键 */
    struct task_struct *task;      /* 任务指针 */
    u64 insert_time;               /* 插入时间 */
};

/* CPU历史表：每个CPU维护时间戳→任务的映射 */
struct yat_cpu_history_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 该CPU历史表的锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
    struct list_head time_list;    /* 时间链表 */
};

/* 历史记录条目：基于链表的历史表实现 */
struct yat_history_record {
    struct list_head list;         /* 链表节点 */
    int job_id;                    /* 作业ID */
    u64 timestamp;                 /* 时间戳 */
    u64 exec_time;                 /* 执行时间 */
};

/* 简化版本：移除复杂的DAG和Job结构体
 * 所有调度信息都集成到 sched_yat_casched_entity 中
 */

/*
// 以下结构体在简化版本中被移除：
// struct yat_dag_task - DAG任务结构
// struct yat_job - 作业结构  
// struct yat_ready_job_pool - 就绪作业池
// struct yat_global_task_pool - 全局优先队列
// struct yat_global_task_node - 全局优先队列节点
*/

struct sched_yat_casched_entity {
    struct list_head run_list;      /* CPU本地队列 */
    struct list_head ready_list;    /* 全局就绪队列 */
    
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 wcet;                       /* 最坏情况执行时间 */
    int priority;                   /* 统一的优先级 */
    int job_id;                     /* 作业ID */
    enum yat_job_state state;       /* 作业状态 */
    u64 per_cpu_last_ts[NR_CPUS];   /* 每个CPU的最后时间戳 */
    int last_cpu;                   /* 上次运行的CPU */
    
    /* 保留CFS兼容性 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    u64 per_cpu_recency[NR_CPUS];   /* 每个CPU的recency值 */
};

struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    struct list_head ready_tasks;   /* 全局就绪任务列表 */
    unsigned int nr_running;        /* 运行任务数量 */
    struct task_struct *agent;     /* 代理任务 */
    u64 cache_decay_jiffies;       /* 缓存衰减时间 */
    spinlock_t history_lock;       /* 历史表锁 */
    
    /* 简化的调度结构 */
    struct yat_accel_table *accel_table;              /* 加速表 */
    struct yat_cpu_history_table history_tables[NR_CPUS]; /* CPU历史表 */
};

struct rq;
struct task_struct;
struct rq_flags;  /* 添加rq_flags的前向声明 */
struct affinity_context;  /* 添加affinity_context的前向声明 */

void init_yat_casched_rq(struct yat_casched_rq *rq);
bool yat_casched_prio(struct task_struct *p);
void update_curr_yat_casched(struct rq *rq);

/* 加速表接口 */
int yat_accel_table_init(struct yat_accel_table *table);
void yat_accel_table_destroy(struct yat_accel_table *table);
int yat_accel_table_insert(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio);
int yat_accel_table_update(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio);
u64 yat_accel_table_lookup(struct yat_accel_table *table, pid_t task_pid, int cpu_id);
int yat_accel_table_delete(struct yat_accel_table *table, pid_t task_pid, int cpu_id);
void yat_accel_table_cleanup(struct yat_accel_table *table, u64 before_time);

/* 简化版本接口：移除复杂的DAG和Job管理 */

/* 简化的AJLR算法接口 */
u64 yat_calculate_recency(int job_id, int cpu_id, u64 now);
u64 yat_calculate_impact(struct task_struct *task, int core);
int yat_find_best_core_for_task(struct task_struct *task);
void yat_schedule_ajlr(struct yat_casched_rq *rq);
void yat_update_ready_tasks(struct yat_casched_rq *rq);
u64 yat_get_crp_ratio(u64 recency);
u64 yat_get_max_last_exec_time(int job_id, int *cpu_list, int cpu_count);
u64 yat_sum_exec_time_multi(int *cpu_list, int cpu_count, u64 start_ts, u64 end_ts);
void yat_cleanup_old_records(int cpu, u64 cutoff_time);

/* 简化的调度器接口 */
void yat_schedule_global_tasks(void);
bool yat_cpu_is_idle(int cpu);
int yat_assign_task_to_cpu(struct task_struct *task, int target_cpu);
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
