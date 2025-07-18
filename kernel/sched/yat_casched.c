/*
 * Yat_Casched - AJLR层级化DAG调度器 (Decision Final Version)
 *
 * 基于AJLR (Allocation of parallel Jobs based on Learned cache Recency)思想的
 * 层级化调度器实现：
 * 
 * 核心特性：
 * - DAG层：遵循全局固定优先级（gFPS），高优先级DAG的"就绪"作业优先被调度
 * - 作业层：利用"加速表"和"历史表"，做出最优的（作业，核心）分配决策
 * - 就绪作业优先队列统一管理所有待调度作业
 * - 二维加速表支持(job,cpu)→benefit查找
 * - 双向链表历史表记录时间戳→任务映射关系
 * - 采用AJLR的LCIF/MSF调度算法
 */

#include "sched.h"
#include <linux/sched/yat_casched.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/spinlock.h>

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / 1000)	/* 1ms - 更短的时间片 */
#define YAT_TIME_SLICE		(4 * NSEC_PER_MSEC)	/* 4ms 默认时间片 */

/* 全局数据结构 */
static struct yat_ready_job_pool *global_ready_pool = NULL;     /* 就绪作业池 */
static struct yat_accel_table *global_accel_table = NULL;       /* 加速表 */
static struct yat_cpu_history_table global_history_tables[NR_CPUS]; /* 历史表 */
static struct list_head global_dag_list = LIST_HEAD_INIT(global_dag_list); /* 活动DAG列表 */
static DEFINE_SPINLOCK(global_dag_lock);                       /* DAG列表锁 */
static int next_dag_id = 1;                                    /* DAG ID分配器 */
static int next_job_id = 1;                                    /* 作业ID分配器 */

/*
 * AJLR核心数据结构管理函数
 */

/* 前置声明 */
static void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags);
static void yat_ajlr_dequeue_task(struct rq *rq, struct task_struct *p, int flags);
struct yat_job *yat_create_job_for_task(struct task_struct *task, int job_priority, u64 wcet, struct yat_dag_task *dag);
void yat_complete_task_job(struct task_struct *task);
void yat_create_test_dag(void);

/* Ready Pool 函数声明 */
static int yat_ready_pool_init(struct yat_ready_job_pool *pool);
static void yat_ready_pool_destroy(struct yat_ready_job_pool *pool);
static int yat_ready_pool_enqueue(struct yat_ready_job_pool *pool, struct yat_job *job);
static struct yat_job *yat_ready_pool_dequeue(struct yat_ready_job_pool *pool);
static struct yat_job *yat_ready_pool_peek(struct yat_ready_job_pool *pool);
static bool yat_ready_pool_empty(struct yat_ready_job_pool *pool);
static void yat_ready_pool_remove(struct yat_ready_job_pool *pool, struct yat_job *job);

/* History 函数声明 */
static void yat_init_history_tables(struct yat_cpu_history_table *tables);
static void yat_destroy_history_tables(struct yat_cpu_history_table *tables);
static int yat_add_history_record(int cpu, int job_id, u64 timestamp, u64 exec_time);
static u64 yat_get_task_last_timestamp(int cpu, int job_id);
static u64 yat_sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts);

/* 初始化就绪作业池 */
static int yat_ready_pool_init(struct yat_ready_job_pool *pool)
{
    if (!pool)
        return -EINVAL;
    
    INIT_LIST_HEAD(&pool->head);
    spin_lock_init(&pool->lock);
    pool->nr_jobs = 0;
    
    return 0;
}

/* 销毁就绪作业池 */
static void yat_ready_pool_destroy(struct yat_ready_job_pool *pool)
{
    struct yat_job *job, *tmp;
    
    if (!pool)
        return;
    
    spin_lock(&pool->lock);
    list_for_each_entry_safe(job, tmp, &pool->head, ready_list) {
        list_del(&job->ready_list);
    }
    pool->nr_jobs = 0;
    spin_unlock(&pool->lock);
}

/* 就绪作业池入队（按优先级排序） */
static int yat_ready_pool_enqueue(struct yat_ready_job_pool *pool, struct yat_job *job)
{
    struct yat_job *existing_job;
    struct list_head *pos;
    
    if (!pool || !job)
        return -EINVAL;
    
    spin_lock(&pool->lock);
    
    /* 按优先级排序插入：DAG优先级 > 作业优先级 > WCET */
    list_for_each(pos, &pool->head) {
        existing_job = list_entry(pos, struct yat_job, ready_list);
        
        /* 主键：DAG优先级 */
        if (job->dag->priority > existing_job->dag->priority)
            break;
        if (job->dag->priority < existing_job->dag->priority)
            continue;
            
        /* 次键：作业优先级 */
        if (job->job_priority > existing_job->job_priority)
            break;
        if (job->job_priority < existing_job->job_priority)
            continue;
            
        /* 三键：WCET（大的在前） */
        if (job->wcet > existing_job->wcet)
            break;
    }
    
    list_add_tail(&job->ready_list, pos);
    job->state = YAT_JOB_READY;
    pool->nr_jobs++;
    
    spin_unlock(&pool->lock);
    return 0;
}

/* 就绪作业池出队（取队头） */
static struct yat_job *yat_ready_pool_dequeue(struct yat_ready_job_pool *pool)
{
    struct yat_job *job = NULL;
    
    if (!pool)
        return NULL;
    
    spin_lock(&pool->lock);
    if (!list_empty(&pool->head)) {
        job = list_first_entry(&pool->head, struct yat_job, ready_list);
        list_del_init(&job->ready_list);
        job->state = YAT_JOB_RUNNING;
        pool->nr_jobs--;
    }
    spin_unlock(&pool->lock);
    
    return job;
}

/* 查看队头（不移除） */
static struct yat_job *yat_ready_pool_peek(struct yat_ready_job_pool *pool)
{
    struct yat_job *job = NULL;
    
    if (!pool)
        return NULL;
    
    spin_lock(&pool->lock);
    if (!list_empty(&pool->head)) {
        job = list_first_entry(&pool->head, struct yat_job, ready_list);
    }
    spin_unlock(&pool->lock);
    
    return job;
}

/* 检查就绪池是否为空 */
static bool yat_ready_pool_empty(struct yat_ready_job_pool *pool)
{
    bool empty;
    
    if (!pool)
        return true;
    
    spin_lock(&pool->lock);
    empty = list_empty(&pool->head);
    spin_unlock(&pool->lock);
    
    return empty;
}

/* 从就绪池移除指定作业 */
static void yat_ready_pool_remove(struct yat_ready_job_pool *pool, struct yat_job *job)
{
    if (!pool || !job)
        return;
    
    spin_lock(&pool->lock);
    if (!list_empty(&job->ready_list)) {
        list_del_init(&job->ready_list);
        pool->nr_jobs--;
    }
    spin_unlock(&pool->lock);
}

/*
 * DAG和作业管理函数
 */

/* 创建DAG */
struct yat_dag_task *yat_create_dag(int dag_id, int dag_priority)
{
    struct yat_dag_task *dag;
    
    dag = kmalloc(sizeof(*dag), GFP_KERNEL);
    if (!dag)
        return NULL;
    
    dag->dag_id = (dag_id <= 0) ? next_dag_id++ : dag_id;
    dag->priority = dag_priority;
    dag->total_jobs = 0;
    dag->completed_jobs = 0;
    INIT_LIST_HEAD(&dag->dag_list);
    INIT_LIST_HEAD(&dag->jobs);
    spin_lock_init(&dag->lock);
    
    spin_lock(&global_dag_lock);
    list_add_tail(&dag->dag_list, &global_dag_list);
    spin_unlock(&global_dag_lock);
    
    return dag;
}

/* 销毁DAG */
void yat_destroy_dag(struct yat_dag_task *dag)
{
    struct yat_job *job, *tmp;
    
    if (!dag)
        return;
    
    spin_lock(&global_dag_lock);
    list_del(&dag->dag_list);
    spin_unlock(&global_dag_lock);
    
    spin_lock(&dag->lock);
    list_for_each_entry_safe(job, tmp, &dag->jobs, job_list) {
        list_del(&job->job_list);
        kfree(job);
    }
    spin_unlock(&dag->lock);
    
    kfree(dag);
}

/* 创建作业 */
struct yat_job *yat_create_job(int job_id, int job_priority, u64 wcet, struct yat_dag_task *dag)
{
    struct yat_job *job;
    int i;
    
    if (!dag)
        return NULL;
    
    job = kmalloc(sizeof(*job), GFP_KERNEL);
    if (!job)
        return NULL;
    
    job->job_id = (job_id <= 0) ? next_job_id++ : job_id;
    job->job_priority = job_priority;
    job->wcet = wcet;
    job->dag = dag;
    job->task = NULL;
    job->state = YAT_JOB_WAITING;
    job->pending_predecessors = 0;
    
    for (i = 0; i < NR_CPUS; i++)
        job->per_cpu_last_ts[i] = 0;
    
    INIT_LIST_HEAD(&job->job_list);
    INIT_LIST_HEAD(&job->ready_list);
    INIT_LIST_HEAD(&job->predecessors);
    INIT_LIST_HEAD(&job->successors);
    
    spin_lock(&dag->lock);
    list_add_tail(&job->job_list, &dag->jobs);
    dag->total_jobs++;
    spin_unlock(&dag->lock);
    
    return job;
}

/* 销毁作业 */
void yat_destroy_job(struct yat_job *job)
{
    if (!job)
        return;
    
    if (job->dag) {
        spin_lock(&job->dag->lock);
        list_del(&job->job_list);
        job->dag->total_jobs--;
        spin_unlock(&job->dag->lock);
    }
    
    kfree(job);
}

/* 添加作业依赖关系 */
void yat_job_add_dependency(struct yat_job *job, struct yat_job *predecessor)
{
    if (!job || !predecessor)
        return;
    
    /* 增加待处理前驱计数 */
    job->pending_predecessors++;
    
    /* TODO: 实现完整的依赖图管理 */
}

/* 作业完成处理 */
void yat_job_complete(struct yat_job *job)
{
    if (!job)
        return;
    
    job->state = YAT_JOB_COMPLETED;
    
    if (job->dag) {
        spin_lock(&job->dag->lock);
        job->dag->completed_jobs++;
        spin_unlock(&job->dag->lock);
    }
    
    /* TODO: 唤醒后继作业 */
}

/*
 * 历史表管理函数 - 基于双向链表的新实现
 */

/* 初始化历史表 */
static void yat_init_history_tables(struct yat_cpu_history_table *tables)
{
    int cpu;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        INIT_LIST_HEAD(&tables[cpu].time_list);
        spin_lock_init(&tables[cpu].lock);
        tables[cpu].last_cleanup_time = jiffies;
    }
}

/* 销毁历史表 */
static void yat_destroy_history_tables(struct yat_cpu_history_table *tables)
{
    struct yat_history_record *rec, *tmp;
    int cpu;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        spin_lock(&tables[cpu].lock);
        list_for_each_entry_safe(rec, tmp, &tables[cpu].time_list, list) {
            list_del(&rec->list);
            kfree(rec);
        }
        spin_unlock(&tables[cpu].lock);
    }
}

/* 添加历史记录 */
static int yat_add_history_record(int cpu, int job_id, u64 timestamp, u64 exec_time)
{
    struct yat_history_record *rec;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return -EINVAL;
    
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec)
        return -ENOMEM;
    
    rec->job_id = job_id;
    rec->timestamp = timestamp;
    rec->exec_time = exec_time;
    
    spin_lock(&global_history_tables[cpu].lock);
    /* 直接插入链表尾部（时间戳递增） */
    list_add_tail(&rec->list, &global_history_tables[cpu].time_list);
    spin_unlock(&global_history_tables[cpu].lock);
    
    return 0;
}

/* 查询某任务最近一次执行时间戳 */
static u64 yat_get_task_last_timestamp(int cpu, int job_id)
{
    u64 last_ts = 0;
    struct yat_history_record *rec;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return 0;
    
    spin_lock(&global_history_tables[cpu].lock);
    list_for_each_entry(rec, &global_history_tables[cpu].time_list, list) {
        if (rec->job_id == job_id && rec->timestamp > last_ts) {
            last_ts = rec->timestamp;
        }
    }
    spin_unlock(&global_history_tables[cpu].lock);
    
    return last_ts;
}

/* 计算时间区间内的执行时间总和 */
static u64 yat_sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts)
{
    u64 sum = 0;
    struct yat_history_record *rec;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return 0;
    
    spin_lock(&global_history_tables[cpu].lock);
    list_for_each_entry(rec, &global_history_tables[cpu].time_list, list) {
        if (rec->timestamp < start_ts)
            continue;
        if (rec->timestamp > end_ts)
            break;
        sum += rec->exec_time;
    }
    spin_unlock(&global_history_tables[cpu].lock);
    
    return sum;
}

/* 清理过期的历史记录 */
void yat_cleanup_old_records(int cpu, u64 cutoff_time)
{
    struct yat_history_record *rec, *tmp;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return;
    
    spin_lock(&global_history_tables[cpu].lock);
    list_for_each_entry_safe(rec, tmp, &global_history_tables[cpu].time_list, list) {
        if (rec->timestamp >= cutoff_time)
            break;
        
        list_del(&rec->list);
        kfree(rec);
    }
    global_history_tables[cpu].last_cleanup_time = jiffies;
    spin_unlock(&global_history_tables[cpu].lock);
}

/* 初始化加速表 */
int yat_accel_table_init(struct yat_accel_table *table)
{
    if (!table)
        return -EINVAL;
    
    table->hash_buckets = kcalloc(1 << YAT_ACCEL_HASH_BITS, sizeof(struct hlist_head), GFP_KERNEL);
    if (!table->hash_buckets)
        return -ENOMEM;
    
    /* 初始化所有哈希桶 */
    for (int i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        INIT_HLIST_HEAD(&table->hash_buckets[i]);
    }
    
    spin_lock_init(&table->lock);
    table->last_cleanup_time = jiffies;
    
    return 0;
}

/* 销毁加速表 */
void yat_accel_table_destroy(struct yat_accel_table *table)
{
    struct yat_accel_entry *entry;
    struct hlist_node *tmp;
    int i;
    
    if (!table || !table->hash_buckets)
        return;
    
    spin_lock(&table->lock);
    for (i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        hlist_for_each_entry_safe(entry, tmp, &table->hash_buckets[i], hash_node) {
            hlist_del(&entry->hash_node);
            kfree(entry);
        }
    }
    kfree(table->hash_buckets);
    table->hash_buckets = NULL;
    spin_unlock(&table->lock);
}

/*
 * 兼容性：保留原有的加速表操作函数（针对task_pid）
 */

/* 生成(task_pid, cpu_id)的哈希值 */
static u32 yat_accel_hash(pid_t task_pid, int cpu_id)
{
    return hash_32((u32)task_pid ^ (u32)cpu_id, YAT_ACCEL_HASH_BITS);
}

/* 查找加速表条目 */
static struct yat_accel_entry *yat_accel_find_entry(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    u32 hash_val = yat_accel_hash(task_pid, cpu_id);
    
    if (!table || !table->hash_buckets)
        return NULL;
    
    hlist_for_each_entry(entry, &table->hash_buckets[hash_val], hash_node) {
        if (entry->job_id == task_pid && entry->cpu_id == cpu_id)  /* 使用job_id字段 */
            return entry;
    }
    return NULL;
}

/* 插入加速表条目 */
int yat_accel_table_insert(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio)
{
    struct yat_accel_entry *entry;
    u32 hash_val;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    spin_lock(&table->lock);
    
    /* 检查是否已存在 */
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (entry) {
        spin_unlock(&table->lock);
        return -EEXIST;
    }
    
    /* 创建新条目 */
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
        spin_unlock(&table->lock);
        return -ENOMEM;
    }
    
    entry->job_id = task_pid;  /* 兼容性：将task_pid存储在job_id字段中 */
    entry->cpu_id = cpu_id;
    entry->benefit = benefit;
    entry->wcet = wcet;
    entry->crp_ratio = crp_ratio;
    entry->last_update_time = jiffies;
    
    hash_val = yat_accel_hash(task_pid, cpu_id);
    hlist_add_head(&entry->hash_node, &table->hash_buckets[hash_val]);
    
    spin_unlock(&table->lock);
    return 0;
}

/* 更新加速表条目 */
int yat_accel_table_update(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio)
{
    struct yat_accel_entry *entry;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    spin_lock(&table->lock);
    
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (!entry) {
        spin_unlock(&table->lock);
        return -ENOENT;
    }
    
    entry->benefit = benefit;
    entry->wcet = wcet;
    entry->crp_ratio = crp_ratio;
    entry->last_update_time = jiffies;
    
    spin_unlock(&table->lock);
    return 0;
}

/* 查找benefit值 */
u64 yat_accel_table_lookup(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    u64 benefit = 0;
    
    if (!table || !table->hash_buckets)
        return 0;
    
    spin_lock(&table->lock);
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (entry)
        benefit = entry->benefit;
    spin_unlock(&table->lock);
    
    return benefit;
}

/* 删除加速表条目 */
int yat_accel_table_delete(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    spin_lock(&table->lock);
    
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (!entry) {
        spin_unlock(&table->lock);
        return -ENOENT;
    }
    
    hlist_del(&entry->hash_node);
    kfree(entry);
    
    spin_unlock(&table->lock);
    return 0;
}

/* 清理过期条目 */
void yat_accel_table_cleanup(struct yat_accel_table *table, u64 before_time)
{
    struct yat_accel_entry *entry;
    struct hlist_node *tmp;
    int i;
    
    if (!table || !table->hash_buckets)
        return;
    
    spin_lock(&table->lock);
    for (i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        hlist_for_each_entry_safe(entry, tmp, &table->hash_buckets[i], hash_node) {
            if (time_before((unsigned long)entry->last_update_time, (unsigned long)before_time)) {
                hlist_del(&entry->hash_node);
                kfree(entry);
            }
        }
    }
    table->last_cleanup_time = jiffies;
    spin_unlock(&table->lock);
}

/*
 * 历史表操作函数
 */

/* 生成时间戳的哈希值 */
/*
 * 初始化Yat_Casched运行队列 - AJLR版本
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    printk(KERN_INFO "======init yat_casched rq (AJLR Final Version)======\n");
    
    INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = jiffies;
    spin_lock_init(&rq->history_lock);
    
    /* 初始化AJLR核心结构 */
    
    /* 1. 初始化就绪作业池 */
    if (!global_ready_pool) {
        global_ready_pool = kmalloc(sizeof(struct yat_ready_job_pool), GFP_KERNEL);
        if (global_ready_pool) {
            yat_ready_pool_init(global_ready_pool);
            printk(KERN_INFO "yat_casched: global_ready_pool allocated successfully\n");
        } else {
            printk(KERN_ERR "yat_casched: Failed to allocate global_ready_pool\n");
        }
    }
    rq->ready_pool = global_ready_pool;
    
    /* 确保 ready_pool 不为 NULL，如果分配失败则创建临时的 */
    if (!rq->ready_pool) {
        printk(KERN_WARNING "yat_casched: Creating fallback ready_pool\n");
        rq->ready_pool = kmalloc(sizeof(struct yat_ready_job_pool), GFP_KERNEL);
        if (rq->ready_pool) {
            yat_ready_pool_init(rq->ready_pool);
        } else {
            printk(KERN_ERR "yat_casched: Critical error - cannot allocate ready_pool\n");
            return;
        }
    }
    
    /* 2. 初始化DAG列表 */
    INIT_LIST_HEAD(&rq->active_dags);
    /* global_dag_list 和 global_dag_lock 已经在声明时静态初始化 */
    
    /* 3. 初始化加速表 */
    if (!global_accel_table) {
        global_accel_table = kmalloc(sizeof(struct yat_accel_table), GFP_KERNEL);
        if (global_accel_table) {
            yat_accel_table_init(global_accel_table);
            printk(KERN_INFO "yat_casched: global_accel_table allocated successfully\n");
        } else {
            printk(KERN_ERR "yat_casched: Failed to allocate global_accel_table\n");
        }
    }
    rq->accel_table = global_accel_table;
    
    /* 确保 accel_table 不为 NULL */
    if (!rq->accel_table) {
        printk(KERN_WARNING "yat_casched: Creating fallback accel_table\n");
        rq->accel_table = kmalloc(sizeof(struct yat_accel_table), GFP_KERNEL);
        if (rq->accel_table) {
            yat_accel_table_init(rq->accel_table);
        } else {
            printk(KERN_ERR "yat_casched: Critical error - cannot allocate accel_table\n");
        }
    }
    
    /* 4. 初始化历史表（使用旧的哈希表实现保持兼容性） */
    yat_init_history_tables(rq->history_tables);
    
    /* 新的历史表（AJLR双向链表实现） */
    yat_init_history_tables(global_history_tables);
    
    printk(KERN_INFO "yat_casched: AJLR structures initialized\n");
}

/*
 * 检查任务是否属于Yat_Casched调度类
 */
bool yat_casched_prio(struct task_struct *p)
{
    return (p->policy == SCHED_YAT_CASCHED);
}

/*
 * 更新任务的虚拟运行时间
 */
void update_curr_yat_casched(struct rq *rq)
{
    struct task_struct *curr = rq->curr;
    u64 now = rq_clock_task(rq);
    u64 delta_exec;

    if (curr->sched_class != &yat_casched_sched_class)
        return;

    delta_exec = now - curr->se.exec_start;
    if (unlikely((s64)delta_exec < 0))
        delta_exec = 0;

    curr->se.exec_start = now;
    curr->yat_casched.vruntime += delta_exec;
}

/*
 * CPU选择算法（简化版本，后续可基于加速表优化）
 */
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    struct yat_casched_rq *yat_rq;
    int best_cpu;
    int cpu;
    unsigned long min_load = ULONG_MAX;
    
    /* 当前简化为负载均衡策略，后续可集成加速表查找 */
    best_cpu = task_cpu;
    for_each_cpu(cpu, &p->cpus_mask) {
        if (!cpu_online(cpu))
            continue;
            
        yat_rq = &cpu_rq(cpu)->yat_casched;
        if (yat_rq->nr_running < min_load) {
            min_load = yat_rq->nr_running;
            best_cpu = cpu;
        }
    }
    
    return best_cpu;
}

/*
 * 将任务加入运行队列 - AJLR版本
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    /* 减少printk频率 - 只在第一次enqueue时打印 */
    if (yat_rq->nr_running == 0) {
        printk(KERN_INFO "yat_casched: enqueue first task (AJLR)\n");
    }
    
    /* 初始化任务的Yat_Casched实体 */
    if (list_empty(&p->yat_casched.run_list)) {
        INIT_LIST_HEAD(&p->yat_casched.run_list);
        p->yat_casched.vruntime = 0;
        p->yat_casched.last_cpu = -1;  /* 使用-1表示未初始化 */
        p->yat_casched.job = NULL;     /* 初始化作业指针 */
    }
    
    /* 添加到运行队列 */
    list_add_tail(&p->yat_casched.run_list, &yat_rq->tasks);
    yat_rq->nr_running++;
    
    /* AJLR处理：创建或更新作业信息 */
    yat_ajlr_enqueue_task(rq, p, flags);
}

/*
 * 将任务从运行队列移除 - AJLR版本
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        yat_rq->nr_running--;
    }
    
    /* AJLR处理：更新作业状态 */
    yat_ajlr_dequeue_task(rq, p, flags);
}

/*
 * 选择下一个要运行的任务 - 优化版本
 * 减少AJLR复杂度，提高调度效率
 */
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct task_struct *p = NULL;
    struct sched_yat_casched_entity *yat_se;
    int best_priority = INT_MAX;
    
    if (!yat_rq || yat_rq->nr_running == 0)
        return NULL;
    
    /* 快速路径：简化的优先级调度，不运行完整AJLR */
    if (!list_empty(&yat_rq->tasks)) {
        struct list_head *pos;
        int count = 0;
        
        /* 在前3个任务中选择最高优先级 */
        list_for_each(pos, &yat_rq->tasks) {
            if (count >= 3) break;
            
            yat_se = list_entry(pos, struct sched_yat_casched_entity, run_list);
            struct task_struct *task = container_of(yat_se, struct task_struct, yat_casched);
            
            int priority = (yat_se->job) ? yat_se->job->job_priority : 0;
            
            if (priority < best_priority) {
                best_priority = priority;
                p = task;
            }
            count++;
        }
        
        /* 如果没找到，使用第一个任务 */
        if (!p) {
            yat_se = list_first_entry(&yat_rq->tasks, 
                                     struct sched_yat_casched_entity, 
                                     run_list);
            p = container_of(yat_se, struct task_struct, yat_casched);
        }
    }
    
    if (p) {
        /* 记录当前CPU为任务的最后运行CPU */
        p->yat_casched.last_cpu = rq->cpu;
        
        /* 如果任务关联了作业，更新作业信息 */
        if (p->yat_casched.job) {
            p->yat_casched.job->per_cpu_last_ts[rq->cpu] = ktime_get_ns();
            /* 简化的历史记录 */
            if (rq->cpu >= 0 && rq->cpu < NR_CPUS) {
                yat_add_history_record(rq->cpu, p->yat_casched.job->job_id, rq_clock_task(rq), 0);
            }
        }
    }
    
    return p;
}

/*
 * 设置下一个任务
 */
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first)
{
    p->se.exec_start = rq_clock_task(rq);
}

/*
 * 放置前一个任务
 */
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    update_curr_yat_casched(rq);
}

/*
 * 任务时钟节拍处理 - 优化版本
 */
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued)
{
    struct sched_yat_casched_entity *yat_se = &p->yat_casched;
    u64 runtime, time_slice;
    
    update_curr_yat_casched(rq);
    
    /* 计算当前任务的运行时间 */
    runtime = p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime;
    
    /* 基于优先级调整时间片：高优先级 = 更长时间片 */
    if (yat_se->job && yat_se->job->job_priority < 0) {
        time_slice = YAT_TIME_SLICE * 2;  /* 高优先级任务 */
    } else if (yat_se->job && yat_se->job->job_priority > 0) {
        time_slice = YAT_TIME_SLICE / 2;  /* 低优先级任务 */
    } else {
        time_slice = YAT_TIME_SLICE;      /* 普通优先级任务 */
    }
    
    /* 检查是否超过时间片 */
    if (runtime >= time_slice) {
        p->se.prev_sum_exec_runtime = p->se.sum_exec_runtime;
        resched_curr(rq);
    }
}

/*
 * 抢占检查
 */
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    /* 简单实现：总是抢占 */
    resched_curr(rq);
}

/*
 * 负载均衡
 */
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    return 0;  /* 暂时不实现复杂的负载均衡 */
}

/* 以下是其他必需的接口函数的简单实现 */

void yield_task_yat_casched(struct rq *rq)
{
    /* 让出CPU，将当前任务移到队列末尾 */
}

bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    return false;
}

void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx)
{
    /* CPU亲和性设置 */
}

void rq_online_yat_casched(struct rq *rq)
{
    /* CPU上线处理 */
}

void rq_offline_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    /* CPU下线处理 - 清理新的历史表数据结构 */
    yat_destroy_history_tables(yat_rq->history_tables);
    
    /* 注意：全局结构不在这里释放，因为它们可能被其他CPU使用 */
    printk(KERN_INFO "yat_casched: CPU offline cleanup completed\n");
}

struct task_struct *pick_task_yat_casched(struct rq *rq)
{
    return pick_next_task_yat_casched(rq);
}

void switched_to_yat_casched(struct rq *rq, struct task_struct *task)
{
    /* 任务切换到Yat_Casched调度类时的处理 */
}

void prio_changed_yat_casched(struct rq *rq, struct task_struct *task, int oldprio)
{
    /* 优先级变化处理 */
}

/*
 * Yat_Casched调度类定义
 */
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

/*
 * Recency计算和核心调度算法
 */

/* 获取多个CPU上某任务的最近执行时间戳（取最大值） */
u64 yat_get_max_last_exec_time(int job_id, int *cpu_list, int cpu_count)
{
    u64 max_ts = 0, ts;
    int i;
    
    for (i = 0; i < cpu_count; i++) {
        ts = yat_get_task_last_timestamp(cpu_list[i], job_id);
        if (ts > max_ts)
            max_ts = ts;
    }
    
    return max_ts;
}

/* 计算多个CPU的执行时间总和 */
u64 yat_sum_exec_time_multi(int *cpu_list, int cpu_count, u64 start_ts, u64 end_ts)
{
    u64 total_sum = 0;
    int i;
    
    for (i = 0; i < cpu_count; i++) {
        total_sum += yat_sum_exec_time_since(cpu_list[i], start_ts, end_ts);
    }
    
    return total_sum;
}

/* 简化的CRP比率计算 */
u64 yat_get_crp_ratio(u64 recency)
{
    /* 简化实现：recency越大，CRP比率越大 */
    /* 返回千分比形式的比率，避免浮点运算 */
    if (recency == 0)
        return 0;
    
    /* 假设最大recency为某个阈值，进行归一化 */
    u64 max_recency = 1000000; /* 1秒，根据实际情况调整 */
    if (recency >= max_recency)
        return 1000; /* 100% */
    
    return (recency * 1000) / max_recency;
}

/* 计算多层缓存recency */
u64 yat_calculate_recency(int job_id, int cpu_id, u64 now)
{
    u64 l1_recency = 0, l2_recency = 0, l3_recency = 0;
    u64 l1_last, l2_last, l3_last;
    int *all_cpus;
    int i;
    
    /* 动态分配CPU数组以避免栈溢出 */
    all_cpus = kmalloc(NR_CPUS * sizeof(int), GFP_ATOMIC);
    if (!all_cpus) {
        /* 如果分配失败，只计算L1缓存新鲜度 */
        l1_last = yat_get_task_last_timestamp(cpu_id, job_id);
        if (l1_last > 0)
            l1_recency = yat_sum_exec_time_since(cpu_id, l1_last, now);
        return l1_recency;
    }
    
    /* L1层（私有缓存）：单个CPU */
    l1_last = yat_get_task_last_timestamp(cpu_id, job_id);
    if (l1_last > 0)
        l1_recency = yat_sum_exec_time_since(cpu_id, l1_last, now);
    
    /* L2层（共享缓存）：简化为所有CPU共享L2 */
    for (i = 0; i < NR_CPUS; i++)
        all_cpus[i] = i;
    l2_last = yat_get_max_last_exec_time(job_id, all_cpus, NR_CPUS);
    if (l2_last > 0)
        l2_recency = yat_sum_exec_time_multi(all_cpus, NR_CPUS, l2_last, now);
    
    /* L3层（全局共享缓存）：与L2相同，简化处理 */
    l3_last = l2_last;
    l3_recency = l2_recency;
    
    kfree(all_cpus);
    return l1_recency + l2_recency + l3_recency;
}

/* 计算Impact（简化实现） */
u64 yat_calculate_impact(struct yat_job *job, int core)
{
    /* 简化实现：Impact = 当前核心上等待队列长度 * 平均执行时间 */
    struct yat_casched_rq *yat_rq = &cpu_rq(core)->yat_casched;
    return yat_rq->nr_running * job->wcet;
}

/* 为作业寻找最佳核心 - AJLR核心算法 */
int yat_find_best_core_for_job(struct yat_job *job)
{
    int best_core = YAT_INVALID_CORE;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;
    u64 current_benefit, current_impact;
    u64 recency, crp_ratio;
    u64 now = ktime_get_ns();
    int cpu;
    
    /* 第一轮：比拼Benefit */
    for_each_online_cpu(cpu) {
        /* 计算recency和benefit */
        recency = yat_calculate_recency(job->job_id, cpu, now);
        crp_ratio = yat_get_crp_ratio(recency);
        current_benefit = ((1000 - crp_ratio) * job->wcet) / 1000;
        
        if (current_benefit > max_benefit) {
            max_benefit = current_benefit;
        }
        
        /* 更新加速表 */
        if (global_accel_table) {
            yat_accel_table_insert(global_accel_table, job->job_id, cpu, 
                                 current_benefit, job->wcet, crp_ratio);
        }
    }
    
    /* 第二轮：在最大Benefit集合中比拼Impact */
    for_each_online_cpu(cpu) {
        if (global_accel_table) {
            current_benefit = yat_accel_table_lookup(global_accel_table, 
                                                   job->job_id, cpu);
        } else {
            /* 重新计算benefit */
            recency = yat_calculate_recency(job->job_id, cpu, now);
            crp_ratio = yat_get_crp_ratio(recency);
            current_benefit = ((1000 - crp_ratio) * job->wcet) / 1000;
        }
        
        if (current_benefit == max_benefit) {
            current_impact = yat_calculate_impact(job, cpu);
            if (current_impact < min_impact) {
                min_impact = current_impact;
                best_core = cpu;
            }
        }
    }
    
    return best_core;
}

/* 更新就绪池：检查所有活动DAG，将满足依赖的作业移入就绪池 */
void yat_update_ready_pool(struct yat_casched_rq *rq)
{
    struct yat_dag_task *dag;
    struct yat_job *job;
    
    if (!rq) {
        printk(KERN_ERR "yat_casched: yat_update_ready_pool called with NULL rq\n");
        return;
    }
    
    if (!rq->ready_pool) {
        printk(KERN_ERR "yat_casched: ready_pool is NULL in yat_update_ready_pool\n");
        return;
    }
    
    spin_lock(&global_dag_lock);
    list_for_each_entry(dag, &global_dag_list, dag_list) {
        spin_lock(&dag->lock);
        list_for_each_entry(job, &dag->jobs, job_list) {
            if (job->state == YAT_JOB_WAITING && job->pending_predecessors == 0) {
                yat_ready_pool_enqueue(rq->ready_pool, job);
            }
        }
        spin_unlock(&dag->lock);
    }
    spin_unlock(&global_dag_lock);
}

/* AJLR主调度函数 */
void yat_schedule_ajlr(struct yat_casched_rq *rq)
{
    struct yat_job *job;
    int best_core;
    u64 now = ktime_get_ns();
    
    if (!rq) {
        printk(KERN_ERR "yat_casched: yat_schedule_ajlr called with NULL rq\n");
        return;
    }
    
    if (!rq->ready_pool) {
        printk(KERN_ERR "yat_casched: ready_pool is NULL in yat_schedule_ajlr\n");
        return;
    }
    
    /* 1. 更新就绪池 */
    yat_update_ready_pool(rq);
    
    /* 2. 循环分配：只要就绪池不空且有空闲核心，就持续进行分配 */
    while (!yat_ready_pool_empty(rq->ready_pool)) {
        
        /* 3. 从就绪池头部取出最高优先级的作业 */
        job = yat_ready_pool_peek(rq->ready_pool);
        if (!job)
            break;
        
        /* 4. 为作业寻找最佳核心 */
        best_core = yat_find_best_core_for_job(job);
        
        /* 5. 如果找到了合适的核心 */
        if (best_core != YAT_INVALID_CORE) {
            /* 5.1 分配作业并移出就绪池 */
            yat_ready_pool_dequeue(rq->ready_pool);
            
            /* 5.2 更新历史表 */
            yat_add_history_record(best_core, job->job_id, now, job->wcet);
            job->per_cpu_last_ts[best_core] = now;
            
            /* 5.3 如果作业有对应的Linux任务，调度它 */
            if (job->task && best_core == smp_processor_id()) {
                /* 在当前CPU上直接调度 */
                resched_curr(cpu_rq(best_core));
            }
            
        } else {
            /* 如果当前最高优先级的作业都找不到核心，则退出循环 */
            break;
        }
    }
}

/*
 * AJLR调度器接口函数
 */

/* 为Linux任务创建关联的作业 */
struct yat_job *yat_create_job_for_task(struct task_struct *task, int job_priority, u64 wcet, struct yat_dag_task *dag)
{
    struct yat_job *job;
    
    if (!task || !dag)
        return NULL;
    
    job = yat_create_job(0, job_priority, wcet, dag);
    if (job) {
        job->task = task;
        task->yat_casched.job = job;
    }
    
    return job;
}

/* 完成任务对应的作业 */
void yat_complete_task_job(struct task_struct *task)
{
    struct yat_job *job;
    
    if (!task || !task->yat_casched.job)
        return;
    
    job = task->yat_casched.job;
    yat_job_complete(job);
    
    /* 清除关联 */
    task->yat_casched.job = NULL;
    job->task = NULL;
}

/* 检查CPU是否空闲 */
/* 创建默认DAG用于传统任务 */
static struct yat_dag_task *yat_get_default_dag(void)
{
    static struct yat_dag_task *default_dag = NULL;
    
    if (!default_dag) {
        default_dag = yat_create_dag(0, YAT_MAX_DAG_PRIORITY / 2); /* 中等优先级 */
    }
    
    return default_dag;
}

/* 任务入队时的AJLR处理 */
void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_job *job;
    struct yat_dag_task *default_dag;
    
    /* 检查任务是否已有关联的作业 */
    if (!p->yat_casched.job) {
        /* 获取默认DAG */
        default_dag = yat_get_default_dag();
        if (!default_dag)
            return;
            
        /* 为新任务创建作业 */
        job = yat_create_job(p->pid, p->prio, 1000000, default_dag);  /* 默认1ms WCET */
        if (job) {
            p->yat_casched.job = job;
            job->task = p;
        }
    }
}

/* 任务出队时的AJLR处理 */
void yat_ajlr_dequeue_task(struct rq *rq, struct task_struct *p, int flags)
{
    /* 更新作业的最后执行时间戳 */
    if (p->yat_casched.job) {
        p->yat_casched.job->per_cpu_last_ts[rq->cpu] = ktime_get_ns();
        
        /* 添加历史记录 */
        if (p->se.sum_exec_runtime > p->se.prev_sum_exec_runtime) {
            u64 exec_time = p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime;
            yat_add_history_record(rq->cpu, p->yat_casched.job->job_id,
                                 ktime_get_ns(), exec_time);
        }
    }
}

/* 创建测试用的DAG和作业 */
void yat_create_test_dag(void)
{
    struct yat_dag_task *dag;
    struct yat_job *job1, *job2, *job3;
    
    /* 创建一个测试DAG */
    dag = yat_create_dag(1, 100); /* 高优先级DAG */
    if (!dag) {
        printk(KERN_ERR "yat_casched: Failed to create test DAG\n");
        return;
    }
    
    /* 创建三个作业 */
    job1 = yat_create_job(1, 50, 1000000, dag); /* 1ms WCET */
    job2 = yat_create_job(2, 40, 2000000, dag); /* 2ms WCET */
    job3 = yat_create_job(3, 30, 1500000, dag); /* 1.5ms WCET */
    
    if (job1 && job2 && job3) {
        /* 设置依赖关系：job1 -> job2 -> job3 */
        yat_job_add_dependency(job2, job1);
        yat_job_add_dependency(job3, job2);
        
        printk(KERN_INFO "yat_casched: Created test DAG with 3 jobs\n");
    } else {
        printk(KERN_ERR "yat_casched: Failed to create test jobs\n");
    }
}
