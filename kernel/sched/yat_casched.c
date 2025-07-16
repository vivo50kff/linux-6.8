/*
 * Yat_Casched - Cache-Aware Scheduler (Decision Final Version)
 *
 * 这是一个基于全局缓存感知性的调度器实现。
 * 主要特性：
 * - 全局优先队列统一管理所有待调度任务
 * - 二维加速表支持(task,cpu)→benefit查找
 * - CPU历史表记录时间戳→任务映射关系
 * - 采用LCIF思想的ajlr调度算法
 */

#include "sched.h"
#include <linux/sched/yat_casched.h>
#include <linux/hashtable.h>
#include <linux/slab.h>

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / HZ)

/* 全局数据结构 */
static struct yat_global_task_pool *global_task_pool = NULL;
static struct yat_accel_table *global_accel_table = NULL;

/*
 * 加速表操作函数
 */

/* 生成(task_pid, cpu_id)的哈希值 */
static u32 yat_accel_hash(pid_t task_pid, int cpu_id)
{
    return hash_32((u32)task_pid ^ (u32)cpu_id, YAT_ACCEL_HASH_BITS);
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

/* 查找加速表条目 */
static struct yat_accel_entry *yat_accel_find_entry(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    u32 hash_val = yat_accel_hash(task_pid, cpu_id);
    
    if (!table || !table->hash_buckets)
        return NULL;
    
    hlist_for_each_entry(entry, &table->hash_buckets[hash_val], hash_node) {
        if (entry->task_pid == task_pid && entry->cpu_id == cpu_id)
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
    
    entry->task_pid = task_pid;
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
            if (time_before(entry->last_update_time, before_time)) {
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
static u32 yat_history_hash(u64 timestamp)
{
    return hash_64(timestamp, YAT_HISTORY_HASH_BITS);
}

/* 初始化CPU历史表 */
void init_cpu_history_tables(struct yat_cpu_history_table *tables)
{
    int cpu, i;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        tables[cpu].hash_buckets = kcalloc(1 << YAT_HISTORY_HASH_BITS, sizeof(struct hlist_head), GFP_KERNEL);
        if (tables[cpu].hash_buckets) {
            for (i = 0; i < (1 << YAT_HISTORY_HASH_BITS); i++) {
                INIT_HLIST_HEAD(&tables[cpu].hash_buckets[i]);
            }
        }
        spin_lock_init(&tables[cpu].lock);
        tables[cpu].last_cleanup_time = jiffies;
    }
}

/* 销毁CPU历史表 */
void destroy_cpu_history_tables(struct yat_cpu_history_table *tables)
{
    struct yat_history_entry *entry;
    struct hlist_node *tmp;
    int cpu, i;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        if (!tables[cpu].hash_buckets)
            continue;
        
        spin_lock(&tables[cpu].lock);
        for (i = 0; i < (1 << YAT_HISTORY_HASH_BITS); i++) {
            hlist_for_each_entry_safe(entry, tmp, &tables[cpu].hash_buckets[i], hash_node) {
                hlist_del(&entry->hash_node);
                kfree(entry);
            }
        }
        kfree(tables[cpu].hash_buckets);
        tables[cpu].hash_buckets = NULL;
        spin_unlock(&tables[cpu].lock);
    }
}

/* 添加历史记录 */
void add_history_record(struct yat_cpu_history_table *tables, int cpu, u64 timestamp, struct task_struct *task)
{
    struct yat_history_entry *entry;
    u32 hash_val;
    
    if (!tables || cpu < 0 || cpu >= NR_CPUS || !tables[cpu].hash_buckets || !task)
        return;
    
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry)
        return;
    
    entry->timestamp = timestamp;
    entry->task = task;
    entry->insert_time = jiffies;
    
    hash_val = yat_history_hash(timestamp);
    
    spin_lock(&tables[cpu].lock);
    hlist_add_head(&entry->hash_node, &tables[cpu].hash_buckets[hash_val]);
    spin_unlock(&tables[cpu].lock);
}

/* 查询历史任务 */
struct task_struct *get_history_task(struct yat_cpu_history_table *tables, int cpu, u64 timestamp)
{
    struct yat_history_entry *entry;
    struct task_struct *task = NULL;
    u32 hash_val;
    
    if (!tables || cpu < 0 || cpu >= NR_CPUS || !tables[cpu].hash_buckets)
        return NULL;
    
    hash_val = yat_history_hash(timestamp);
    
    spin_lock(&tables[cpu].lock);
    hlist_for_each_entry(entry, &tables[cpu].hash_buckets[hash_val], hash_node) {
        if (entry->timestamp == timestamp) {
            task = entry->task;
            break;
        }
    }
    spin_unlock(&tables[cpu].lock);
    
    return task;
}

/* 清理过期历史记录 */
void prune_history_table(struct yat_cpu_history_table *tables, int cpu, u64 before_timestamp)
{
    struct yat_history_entry *entry;
    struct hlist_node *tmp;
    int i;
    
    if (!tables || cpu < 0 || cpu >= NR_CPUS || !tables[cpu].hash_buckets)
        return;
    
    spin_lock(&tables[cpu].lock);
    for (i = 0; i < (1 << YAT_HISTORY_HASH_BITS); i++) {
        hlist_for_each_entry_safe(entry, tmp, &tables[cpu].hash_buckets[i], hash_node) {
            if (entry->timestamp < before_timestamp) {
                hlist_del(&entry->hash_node);
                kfree(entry);
            }
        }
    }
    tables[cpu].last_cleanup_time = jiffies;
    spin_unlock(&tables[cpu].lock);
}

/*
 * 全局优先队列操作函数
 */

/* 初始化全局优先队列 */
int yat_global_pool_init(struct yat_global_task_pool *pool)
{
    if (!pool)
        return -EINVAL;
    
    INIT_LIST_HEAD(&pool->head);
    spin_lock_init(&pool->lock);
    pool->nr_tasks = 0;
    
    return 0;
}

/* 销毁全局优先队列 */
void yat_global_pool_destroy(struct yat_global_task_pool *pool)
{
    struct yat_global_task_node *node, *tmp;
    
    if (!pool)
        return;
    
    spin_lock(&pool->lock);
    list_for_each_entry_safe(node, tmp, &pool->head, list) {
        list_del(&node->list);
        kfree(node);
    }
    pool->nr_tasks = 0;
    spin_unlock(&pool->lock);
}

/* 入队（按优先级排序插入） */
int yat_global_pool_enqueue(struct yat_global_task_pool *pool, struct task_struct *task, int priority, u64 wcet)
{
    struct yat_global_task_node *new_node, *node;
    struct list_head *pos;
    
    if (!pool || !task)
        return -EINVAL;
    
    new_node = kmalloc(sizeof(*new_node), GFP_ATOMIC);
    if (!new_node)
        return -ENOMEM;
    
    new_node->task = task;
    new_node->priority = priority;
    new_node->wcet = wcet;
    new_node->enqueue_time = jiffies;
    INIT_LIST_HEAD(&new_node->list);
    
    spin_lock(&pool->lock);
    
    /* 按优先级排序插入，优先级高的在前，优先级相同则WCET大的在前 */
    list_for_each(pos, &pool->head) {
        node = list_entry(pos, struct yat_global_task_node, list);
        if (priority > node->priority || 
            (priority == node->priority && wcet > node->wcet)) {
            break;
        }
    }
    
    list_add_tail(&new_node->list, pos);
    pool->nr_tasks++;
    
    spin_unlock(&pool->lock);
    return 0;
}

/* 出队（取队头） */
struct task_struct *yat_global_pool_dequeue(struct yat_global_task_pool *pool)
{
    struct yat_global_task_node *node;
    struct task_struct *task = NULL;
    
    if (!pool)
        return NULL;
    
    spin_lock(&pool->lock);
    if (!list_empty(&pool->head)) {
        node = list_first_entry(&pool->head, struct yat_global_task_node, list);
        task = node->task;
        list_del(&node->list);
        kfree(node);
        pool->nr_tasks--;
    }
    spin_unlock(&pool->lock);
    
    return task;
}

/* 查看队头（不移除） */
struct task_struct *yat_global_pool_peek(struct yat_global_task_pool *pool)
{
    struct yat_global_task_node *node;
    struct task_struct *task = NULL;
    
    if (!pool)
        return NULL;
    
    spin_lock(&pool->lock);
    if (!list_empty(&pool->head)) {
        node = list_first_entry(&pool->head, struct yat_global_task_node, list);
        task = node->task;
    }
    spin_unlock(&pool->lock);
    
    return task;
}

/* 检查队列是否为空 */
bool yat_global_pool_empty(struct yat_global_task_pool *pool)
{
    bool empty;
    
    if (!pool)
        return true;
    
    spin_lock(&pool->lock);
    empty = list_empty(&pool->head);
    spin_unlock(&pool->lock);
    
    return empty;
}

/* 移除指定任务 */
void yat_global_pool_remove(struct yat_global_task_pool *pool, struct task_struct *task)
{
    struct yat_global_task_node *node, *tmp;
    
    if (!pool || !task)
        return;
    
    spin_lock(&pool->lock);
    list_for_each_entry_safe(node, tmp, &pool->head, list) {
        if (node->task == task) {
            list_del(&node->list);
            kfree(node);
            pool->nr_tasks--;
            break;
        }
    }
    spin_unlock(&pool->lock);
}

/*
 * 初始化Yat_Casched运行队列
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    printk(KERN_INFO "======init yat_casched rq (Decision Final Version)======\n");
    
    INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = jiffies;
    spin_lock_init(&rq->history_lock);
    
    /* 初始化全局优先队列 */
    if (!global_task_pool) {
        global_task_pool = kmalloc(sizeof(struct yat_global_task_pool), GFP_KERNEL);
        if (global_task_pool) {
            yat_global_pool_init(global_task_pool);
        }
    }
    rq->global_pool = global_task_pool;
    
    /* 初始化加速表 */
    if (!global_accel_table) {
        global_accel_table = kmalloc(sizeof(struct yat_accel_table), GFP_KERNEL);
        if (global_accel_table) {
            yat_accel_table_init(global_accel_table);
        }
    }
    rq->accel_table = global_accel_table;
    
    /* 初始化CPU历史表 */
    init_cpu_history_tables(rq->history_tables);
    
    printk(KERN_INFO "yat_casched: Global structures initialized\n");
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
 * 将任务加入运行队列
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    /* 减少printk频率 - 只在第一次enqueue时打印 */
    if (yat_rq->nr_running == 0) {
        printk(KERN_INFO "yat_casched: enqueue first task\n");
    }
    
    /* 初始化任务的Yat_Casched实体 */
    if (list_empty(&p->yat_casched.run_list)) {
        INIT_LIST_HEAD(&p->yat_casched.run_list);
        p->yat_casched.vruntime = 0;
        p->yat_casched.last_cpu = -1;  /* 使用-1表示未初始化 */
    }
    
    /* 添加到运行队列 */
    list_add_tail(&p->yat_casched.run_list, &yat_rq->tasks);
    yat_rq->nr_running++;
}

/*
 * 将任务从运行队列移除
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        yat_rq->nr_running--;
    }
}

/*
 * 选择下一个要运行的任务
 * 使用简单的FIFO策略，但考虑缓存亲和性
 */
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct task_struct *p = NULL;
    
    if (!yat_rq || yat_rq->nr_running == 0)
        return NULL;
    
    /* 选择队列中的第一个任务 */
    if (!list_empty(&yat_rq->tasks)) {
        struct sched_yat_casched_entity *yat_se;
        yat_se = list_first_entry(&yat_rq->tasks, 
                                 struct sched_yat_casched_entity, 
                                 run_list);
        if (yat_se) {
            p = container_of(yat_se, struct task_struct, yat_casched);
        }
    }
    
    if (p) {
        /* 记录当前CPU为任务的最后运行CPU */
        p->yat_casched.last_cpu = rq->cpu;
        
        /* TODO: 在这里添加新历史表记录和加速表更新 */
        if (rq->cpu >= 0 && rq->cpu < NR_CPUS) {
            add_history_record(yat_rq->history_tables, rq->cpu, rq_clock_task(rq), p);
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
 * 任务时钟节拍处理 - 恢复到基本版本
 */
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued)
{
    update_curr_yat_casched(rq);
    
    /* 使用简单的时间片管理，类似于SCHED_NORMAL */
    if (p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime >= YAT_MIN_GRANULARITY) {
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
    destroy_cpu_history_tables(yat_rq->history_tables);
    
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
