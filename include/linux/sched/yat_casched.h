#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED

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
};

/* 全局优先队列节点 */
struct yat_global_task_node {
    struct list_head list;         /* 链表节点 */
    struct task_struct *task;      /* 任务指针 */
    int priority;                  /* 优先级 */
    u64 wcet;                      /* WCET */
    u64 enqueue_time;              /* 入队时间 */
};

/* 全局优先队列 */
struct yat_global_task_pool {
    struct list_head head;         /* 队列头 */
    spinlock_t lock;               /* 队列锁 */
    unsigned int nr_tasks;         /* 队列中任务数 */
};

struct sched_yat_casched_entity {
    struct list_head run_list;      /* 运行队列链表节点 */
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    int last_cpu;                   /* 上次运行的CPU */
    u64 per_cpu_recency[NR_CPUS];   /* 每个CPU的recency值 */
    u64 wcet;                       /* 任务的WCET */
    int priority;                   /* 任务优先级 */
};

struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    unsigned int nr_running;        /* 运行任务数量 */
    struct task_struct *agent;     /* 代理任务 */
    u64 cache_decay_jiffies;       /* 缓存衰减时间 */
    spinlock_t history_lock;       /* 历史表锁 */
    
    /* 全局调度结构 */
    struct yat_global_task_pool *global_pool; /* 全局优先队列 */
    struct yat_accel_table *accel_table;      /* 加速表 */
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

/* 历史表接口 */
void init_cpu_history_tables(struct yat_cpu_history_table *tables);
void destroy_cpu_history_tables(struct yat_cpu_history_table *tables);
void add_history_record(struct yat_cpu_history_table *table, int cpu, u64 timestamp, struct task_struct *task);
struct task_struct *get_history_task(struct yat_cpu_history_table *table, int cpu, u64 timestamp);
void prune_history_table(struct yat_cpu_history_table *table, int cpu, u64 before_timestamp);

/* 全局优先队列接口 */
int yat_global_pool_init(struct yat_global_task_pool *pool);
void yat_global_pool_destroy(struct yat_global_task_pool *pool);
int yat_global_pool_enqueue(struct yat_global_task_pool *pool, struct task_struct *task, int priority, u64 wcet);
struct task_struct *yat_global_pool_dequeue(struct yat_global_task_pool *pool);
struct task_struct *yat_global_pool_peek(struct yat_global_task_pool *pool);
bool yat_global_pool_empty(struct yat_global_task_pool *pool);
void yat_global_pool_remove(struct yat_global_task_pool *pool, struct task_struct *task);
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
