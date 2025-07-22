#include <linux/debugfs.h>
#include <linux/seq_file.h>


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
<<<<<<< HEAD
<<<<<<< HEAD
#include <linux/cpumask.h>
#include <linux/topology.h>

=======
#include <linux/cpumask.h>
#include <linux/topology.h>

/* debugfs 导出接口声明，避免隐式声明和 static 冲突 */
static void yat_debugfs_init(void);
static void yat_debugfs_cleanup(void);

>>>>>>> yat/ft
/* --- 核心数据结构定义 (v2.2 - 最终版) --- */

// 历史记录: 使用双向链表，按时间戳顺序记录历史
struct history_record {
    struct list_head list;
    int dag_id;          // 新增：DAG ID
    int task_id;         // 保持原有：任务 ID
    u64 timestamp;
    u64 exec_time;       // 改名：原expected_exec_time，表示预期执行时间
    u64 actual_exec_time; // 新增：实际执行时间
};

// 历史表: 每个CPU一个，包含一个链表头和锁
struct cpu_history_table {
    struct list_head time_list;
    spinlock_t lock;
};
static struct cpu_history_table history_tables[NR_CPUS];

// 就绪作业池: 存放所有待调度的Yat_Casched任务
static LIST_HEAD(yat_ready_job_pool);
static DEFINE_SPINLOCK(yat_pool_lock);

// 加速表条目: 存放在哈希表中，缓存benefit值
struct accelerator_entry {
    struct hlist_node node;
    struct task_struct *p;
    int cpu;
    u64 benefit;
};

// 加速表: 全局哈希表，用于加速查找最佳(任务,核心)对
#define ACCELERATOR_BITS 10
static DEFINE_HASHTABLE(accelerator_table, ACCELERATOR_BITS);
static DEFINE_SPINLOCK(accelerator_lock);


/* --- 函数前向声明 --- */
static u64 get_crp_ratio(u64 recency);

/* --- 历史表操作函数 --- */

// 初始化历史表
static void init_history_tables(void) {
    int cpu;
    for_each_possible_cpu(cpu) {
        INIT_LIST_HEAD(&history_tables[cpu].time_list);
        spin_lock_init(&history_tables[cpu].lock);
    }
}

// 添加历史记录
static void add_history_record(int cpu, int dag_id, int task_id, u64 expected_exec_time, u64 actual_exec_time) {
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
    
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec) return;

    rec->dag_id = dag_id;
    rec->task_id = task_id;
    rec->timestamp = ktime_get();
    rec->exec_time = expected_exec_time;     // 预期执行时间
    rec->actual_exec_time = actual_exec_time; // 实际执行时间

    spin_lock(&table->lock);
    // 直接插入链表尾部（保证时间戳递增）
    list_add_tail(&rec->list, &table->time_list);
    spin_unlock(&table->lock);
}

// 计算时间区间内的执行时间总和 (从后向前遍历，更高效)
static u64 sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts) {
    u64 sum = 0;
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
    
    spin_lock(&table->lock);
    // 从链表尾部向前遍历
    list_for_each_entry_reverse(rec, &table->time_list, list) {
        if (rec->timestamp <= start_ts) {
            break; // 已超出查询范围的起点，停止遍历
        }
        if (rec->timestamp <= end_ts) {
            sum += rec->exec_time;
        }
    }
    spin_unlock(&table->lock);
    return sum;
}

// 计算多个CPU在时间区间内的总执行时间
static u64 sum_exec_time_multi(const struct cpumask *cpus, u64 start_ts, u64 end_ts) {
    u64 total_sum = 0;
    int cpu;
    for_each_cpu(cpu, cpus) {
        total_sum += sum_exec_time_since(cpu, start_ts, end_ts);
    }
    return total_sum;
}

// 获取任务在指定CPU集合上的最晚执行时间
static u64 get_max_last_exec_time(struct task_struct *p, const struct cpumask *cpus) {
    u64 max_ts = 0;
    int cpu;
    for_each_cpu(cpu, cpus) {
        if (p->yat_casched.per_cpu_recency[cpu] > max_ts) {
            max_ts = p->yat_casched.per_cpu_recency[cpu];
        }
    }
    return max_ts;
}


/* --- Recency 和 Impact 计算 (v3.0 - 三层缓存距离算法) --- */

// 系统参数定义
#define YAT_L2_CORES_PER_CLUSTER 4      // L2缓存簇大小
#define YAT_V2_THRESHOLD 10000          // L1窗口大小 (cycles)
#define YAT_V3_THRESHOLD 100000         // L2窗口大小 (cycles)  
#define YAT_V4_THRESHOLD 500000         // L3窗口大小 (cycles)

// L1 Recency计算 - 重写实现基于任务ID匹配的算法
static s64 calculate_l1_recency(int dag_id, int task_id, int cpu_id, u64 now) {
    s64 dist = 0;
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu_id];
    
    spin_lock(&table->lock);
    // 从最新记录开始倒推扫描
    list_for_each_entry_reverse(rec, &table->time_list, list) {
        // 找到同一个任务的历史记录
        if (rec->dag_id == dag_id && rec->task_id == task_id) {
            spin_unlock(&table->lock);
            return dist; // 返回累积距离
        }
        
        // 累加"别人"的预期执行时间
        dist += rec->exec_time;
        
        // 超出L1窗口则停止
        if (dist > YAT_V2_THRESHOLD) {
            break;
        }
    }
    spin_unlock(&table->lock);
    
    return -1; // L1未命中
}

// L2 Recency计算 - 重写实现簇共享历史
static s64 calculate_l2_recency(int dag_id, int task_id, int cpu_id, u64 now) {
    s64 dist = 0;
    int cluster_id = cpu_id / YAT_L2_CORES_PER_CLUSTER;
    int cluster_start = cluster_id * YAT_L2_CORES_PER_CLUSTER;
    int cluster_end = cluster_start + YAT_L2_CORES_PER_CLUSTER;
    int cpu;
    
    // 收集簇内所有CPU的历史记录到临时列表
    struct list_head cluster_history;
    struct history_record *rec, *cluster_rec, *tmp_rec;
    
    INIT_LIST_HEAD(&cluster_history);
    
    // 遍历簇内所有CPU的历史
    for (cpu = cluster_start; cpu < cluster_end && cpu < NR_CPUS; cpu++) {
        struct cpu_history_table *table = &history_tables[cpu];
        
        spin_lock(&table->lock);
        list_for_each_entry(rec, &table->time_list, list) {
            // 创建临时记录副本
            cluster_rec = kmalloc(sizeof(*cluster_rec), GFP_ATOMIC);
            if (!cluster_rec) {
                spin_unlock(&table->lock);
                continue;
            }
            
            cluster_rec->dag_id = rec->dag_id;
            cluster_rec->task_id = rec->task_id;
            cluster_rec->timestamp = rec->timestamp;
            cluster_rec->exec_time = rec->exec_time;
            cluster_rec->actual_exec_time = rec->actual_exec_time;
            
            // 按时间戳有序插入
            struct list_head *pos;
            struct history_record *existing;
            bool inserted = false;
            
            list_for_each(pos, &cluster_history) {
                existing = list_entry(pos, struct history_record, list);
                if (cluster_rec->timestamp < existing->timestamp) {
                    list_add_tail(&cluster_rec->list, pos);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                list_add_tail(&cluster_rec->list, &cluster_history);
            }
        }
        spin_unlock(&table->lock);
    }
    
    // 倒推扫描簇历史
    list_for_each_entry_reverse(cluster_rec, &cluster_history, list) {
        if (cluster_rec->dag_id == dag_id && cluster_rec->task_id == task_id) {
            // 找到目标任务
            goto cleanup_l2_found;
        }
        
        dist += cluster_rec->exec_time;
        
        if (dist > YAT_V3_THRESHOLD) {
            dist = -1; // L2未命中
            break;
        }
    }
    
    // 如果循环正常结束但没找到任务，也是未命中
    if (dist >= 0 && dist <= YAT_V3_THRESHOLD) {
        // 这种情况是遍历完整个窗口都没找到，应该是未命中
        dist = -1;
    }
    
cleanup_l2_found:
cleanup_l2:
    // 清理临时列表
    list_for_each_entry_safe(cluster_rec, tmp_rec, &cluster_history, list) {
        list_del(&cluster_rec->list);
        kfree(cluster_rec);
    }
    
    return dist;
}

// L3 Recency计算 - 重写实现全局历史
static s64 calculate_l3_recency(int dag_id, int task_id, u64 now) {
    s64 dist = 0;
    int cpu;
    struct history_record *rec, *global_rec, *tmp_rec;
    struct list_head global_history;
    
    INIT_LIST_HEAD(&global_history);
    
    // 收集全局历史记录
    for_each_possible_cpu(cpu) {
        struct cpu_history_table *table = &history_tables[cpu];
        
        spin_lock(&table->lock);
        list_for_each_entry(rec, &table->time_list, list) {
            global_rec = kmalloc(sizeof(*global_rec), GFP_ATOMIC);
            if (!global_rec) {
                spin_unlock(&table->lock);
                continue;
            }
            
            global_rec->dag_id = rec->dag_id;
            global_rec->task_id = rec->task_id;
            global_rec->timestamp = rec->timestamp;
            global_rec->exec_time = rec->exec_time;
            global_rec->actual_exec_time = rec->actual_exec_time;
            
            // 按时间戳有序插入
            struct list_head *pos;
            struct history_record *existing;
            bool inserted = false;
            
            list_for_each(pos, &global_history) {
                existing = list_entry(pos, struct history_record, list);
                if (global_rec->timestamp < existing->timestamp) {
                    list_add_tail(&global_rec->list, pos);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                list_add_tail(&global_rec->list, &global_history);
            }
        }
        spin_unlock(&table->lock);
    }
    
    // 倒推扫描全局历史
    list_for_each_entry_reverse(global_rec, &global_history, list) {
        if (global_rec->dag_id == dag_id && global_rec->task_id == task_id) {
            goto cleanup_l3_found;
        }
        
        dist += global_rec->exec_time;
        
        if (dist > YAT_V4_THRESHOLD) {
            dist = -1;
            break;
        }
    }
    
    // 如果循环正常结束但没找到任务，也是未命中
    if (dist >= 0 && dist <= YAT_V4_THRESHOLD) {
        dist = -1; // L3未命中
    }
    
cleanup_l3_found:
cleanup_l3:
    // 清理临时列表
    list_for_each_entry_safe(global_rec, tmp_rec, &global_history, list) {
        list_del(&global_rec->list);
        kfree(global_rec);
    }
    
    return dist;
}

// 新的三层Recency计算主函数 - 重写实现
static u64 calculate_recency(struct task_struct *p, int cpu_id) {
    u64 now = ktime_get();
    s64 l1_dist, l2_dist, l3_dist;
    int dag_id = 0;  // 默认DAG ID，可从任务结构体获取
    int task_id = p->pid;
    
    // 如果任务有关联的作业，使用作业的DAG ID
    if (p->yat_casched.job && p->yat_casched.job->dag) {
        dag_id = p->yat_casched.job->dag->dag_id;
        task_id = p->yat_casched.job->job_id;
    }
    
    // L1层：单核私有
    l1_dist = calculate_l1_recency(dag_id, task_id, cpu_id, now);
    if (l1_dist >= 0 && l1_dist <= YAT_V2_THRESHOLD) {
        return (u64)l1_dist; // L1命中
    }
    
    // L2层：簇共享
    l2_dist = calculate_l2_recency(dag_id, task_id, cpu_id, now);
    if (l2_dist >= 0 && l2_dist <= YAT_V3_THRESHOLD) {
        return (u64)l2_dist; // L2命中
    }
    
    // L3层：全局共享
    l3_dist = calculate_l3_recency(dag_id, task_id, now);
    if (l3_dist >= 0 && l3_dist <= YAT_V4_THRESHOLD) {
        return (u64)l3_dist; // L3命中
    }
    
    // 完全未命中，返回最大值表示冷缓存
    return YAT_V4_THRESHOLD + 1;
}

/*
 * 新的CRP模型函数 - 支持三层缓存区间
 * 输入: recency - 三层缓存距离算法的结果
 * 输出: crp_ratio (0-1000) - 执行时间占WCET的千分比
 */
static u64 get_crp_ratio(u64 recency)
{
    // L1区间 [0, v2]
    if (recency <= YAT_V2_THRESHOLD) {
        // L1命中：delta1 = 0.3，线性插值
        return 300 + (recency * 200) / YAT_V2_THRESHOLD; // 300~500
    }
    
    // L2区间 (v2, v3]
    if (recency <= YAT_V3_THRESHOLD) {
        // L2命中：delta2 = 0.5，线性插值
        u64 l2_offset = recency - YAT_V2_THRESHOLD;
        u64 l2_range = YAT_V3_THRESHOLD - YAT_V2_THRESHOLD;
        return 500 + (l2_offset * 300) / l2_range; // 500~800
    }
    
    // L3区间 (v3, v4]
    if (recency <= YAT_V4_THRESHOLD) {
        // L3命中：delta3 = 0.8，线性插值
        u64 l3_offset = recency - YAT_V3_THRESHOLD;
        u64 l3_range = YAT_V4_THRESHOLD - YAT_V3_THRESHOLD;
        return 800 + (l3_offset * 200) / l3_range; // 800~1000
    }
    
    // 完全未命中：使用WCET
    return 1000;
}

/*
 * AJLR的CRP (Cache Recency Profile) 模型函数
 * 输入: recency (ns) - 缓存新鲜度，表示自上次执行后其他任务的执行时间总和
 * 输出: crp_ratio (0-1000) - 执行时间占WCET的千分比，1000表示完全冷缓存
 */
static u64 get_crp_ratio_old(u64 recency)
{
    /* 基于RTNS'23论文的CRP分段函数 */
    if (recency < 1000)          /* 1us内，极热缓存 */
        return 400;              /* WCET的40% */
    else if (recency < 10000)    /* 10us内，很热缓存 */
        return 600;              /* WCET的60% */
    else if (recency < 100000)   /* 100us内，热缓存 */
        return 750;              /* WCET的75% */
    else if (recency < 1000000)  /* 1ms内，温缓存 */
        return 850;              /* WCET的85% */
    else if (recency < 10000000) /* 10ms内，冷缓存 */
        return 950;              /* WCET的95% */
    
    return 1000;                 /* 完全冷缓存，执行时间=WCET */
}

/*
 * 模拟获取任务WCET的函数
 */
static u64 get_wcet(struct task_struct *p)
{
    // 初步实现：给所有任务一个固定的WCET，例如10ms
    return 10 * 1000 * 1000; // 10ms in ns
}
<<<<<<< HEAD
=======
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
>>>>>>> yat/yat_casched-dev
=======
>>>>>>> yat/ft

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / 1000)	/* 1ms - 更短的时间片 */
#define YAT_TIME_SLICE		(4 * NSEC_PER_MSEC)	/* 4ms 默认时间片 */

<<<<<<< HEAD
/*
 * 初始化Yat_Casched运行队列
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    struct rq *main_rq = container_of(rq, struct rq, yat_casched);

    // 在第一次初始化时，初始化全局数据结构
    static bool global_init_done = false;
    if (!global_init_done) {
        printk(KERN_INFO "======Yat_Casched: Global Init======\n");
        init_history_tables();
        hash_init(accelerator_table); // 初始化哈希表
        global_init_done = true;
    }

    printk(KERN_INFO "======init yat_casched rq for cpu %d======\n", main_rq->cpu);
=======
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
    // 对于AJLR接口，使用默认DAG ID 0
    add_history_record(cpu, 0, job_id, exec_time, exec_time);
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
 * 初始化Yat_Casched运行队列
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    struct rq *main_rq = container_of(rq, struct rq, yat_casched);

    // 在第一次初始化时，初始化全局数据结构
    static atomic_t global_init_done = ATOMIC_INIT(0);

    // 只允许第一个调用者执行全局初始化
    if (atomic_cmpxchg(&global_init_done, 0, 1) == 0) {
        printk(KERN_INFO "======Yat_Casched: Global Init======\n");
        init_history_tables();
        hash_init(accelerator_table); // 初始化哈希表
    }
    printk(KERN_INFO "======init yat_casched rq for cpu %d======\n", main_rq->cpu);

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
>>>>>>> yat/yat_casched-dev
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
 * select_task_rq - 职责减弱
 * 在我们的新模型中，决策由全局调度器做出。
 * 这个函数只在特殊情况下被调用，或者可以简单返回一个默认值。
 */
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    // 决策已移至全局调度器，这里返回task_cpu以兼容框架
    return task_cpu;
}

/*
 * 将任务加入全局任务池，并保持有序
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct list_head *pos;
    struct task_struct *entry;

    // 初始化新任务
    if (p->yat_casched.wcet == 0) {
        p->yat_casched.wcet = get_wcet(p);
        // ... 其他初始化 ...
    }

    spin_lock(&yat_pool_lock);
    
    /* 初始化任务的Yat_Casched实体 */
    if (list_empty(&p->yat_casched.run_list)) {
        INIT_LIST_HEAD(&p->yat_casched.run_list);
        p->yat_casched.vruntime = 0;
        p->yat_casched.last_cpu = -1;  /* 使用-1表示未初始化 */
        p->yat_casched.job = NULL;     /* 初始化作业指针 */
    }
    
    /* 添加到运行队列 */
    list_add_tail(&p->yat_casched.run_list, &rq->yat_casched.tasks);
    rq->yat_casched.nr_running++;
    
    /* AJLR处理：创建或更新作业信息 */
    yat_ajlr_enqueue_task(rq, p, flags);
}

/*
 * 将任务从就绪池移除
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    spin_lock(&yat_pool_lock);
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        rq->yat_casched.nr_running--;
    }
    spin_unlock(&yat_pool_lock);
}

/*
 * pick_next_task - 从本地运行队列取任务
 * 因为我们保证每个核心最多一个任务，所以逻辑很简单
 */
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct task_struct *p = NULL;
    struct sched_yat_casched_entity *yat_se;
    int best_priority = INT_MAX;
    
    if (yat_rq->nr_running == 0)
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
    u64 delta_exec;
    int dag_id = 0, task_id = p->pid;
    u64 expected_exec = 0;
    
    update_curr_yat_casched(rq);
    
    delta_exec = rq_clock_task(rq) - p->se.exec_start;
    
    // 获取DAG ID和预期执行时间
    if (p->yat_casched.job) {
        task_id = p->yat_casched.job->job_id;
        expected_exec = p->yat_casched.job->wcet;
        if (p->yat_casched.job->dag) {
            dag_id = p->yat_casched.job->dag->dag_id;
        }
    } else {
        expected_exec = get_wcet(p);
    }
    
    /* 更新历史表 - 使用新的接口 */
    add_history_record(rq->cpu, dag_id, task_id, expected_exec, delta_exec);
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

/* --- 全局调度逻辑 (v2.2) --- */

// 步骤1: 更新加速表，计算并缓存所有可能的 benefit
static void update_accelerator_table(void)
{
    struct task_struct *p;
    int i;
    struct hlist_node *tmp;
    struct accelerator_entry *entry;
    int bkt;

    // 清空旧的加速表
    spin_lock(&accelerator_lock);
    hash_for_each_safe(accelerator_table, bkt, tmp, entry, node) {
        hash_del(&entry->node);
        kfree(entry);
    }
    spin_unlock(&accelerator_lock);

    // 遍历就绪池中的任务和所有空闲核心，计算并填充新的benefit
    spin_lock(&yat_pool_lock);
    list_for_each_entry(p, &yat_ready_job_pool, yat_casched.run_list) {
        for_each_online_cpu(i) {
            // 只为当前空闲的核心计算benefit
            if (cpu_rq(i)->yat_casched.nr_running > 0)
                continue;

            u64 recency = calculate_recency(p, i);
            u64 crp_ratio = get_crp_ratio(recency);
            u64 benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;

            if (benefit > 0) {
                entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
                if (!entry) continue;
                entry->p = p;
                entry->cpu = i;
                entry->benefit = benefit;
                
                spin_lock(&accelerator_lock);
                // 使用任务和CPU的地址组合作为唯一的key
                hash_add(accelerator_table, &entry->node, (unsigned long)p + i);
                spin_unlock(&accelerator_lock);
            }
        }
    }
    spin_unlock(&yat_pool_lock);
}


// 步骤2: 根据加速表进行调度决策
static void schedule_yat_casched_tasks(void)
{
    struct accelerator_entry *entry, *best_entry = NULL;
    struct task_struct *p_to_run = NULL;
    int best_cpu = -1;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;
    int bkt;
    
    // 1. 更新加速表，缓存所有当前可行的(任务,核心)对的benefit
    update_accelerator_table();

    // 2. 第一轮：遍历加速表，找到最大的 benefit 值
    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->benefit > max_benefit) {
            max_benefit = entry->benefit;
        }
    }

    // 3. 第二轮：在 benefit 最大的条目中，通过计算 impact 来打破平局
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->benefit == max_benefit) {
            // 只有在此时，才需要计算 Impact
            u64 current_impact = calculate_impact(entry->p, entry->cpu);
            if (current_impact < min_impact) {
                min_impact = current_impact;
                best_entry = entry; // 记录最佳条目
            }
        }
    }
    spin_unlock(&accelerator_lock);

    // 4. 如果找到了最佳选择，则执行调度
    if (best_entry) {
        p_to_run = best_entry->p;
        best_cpu = best_entry->cpu;

        // 从就绪池移除
        spin_lock(&yat_pool_lock);
        list_del_init(&p_to_run->yat_casched.run_list);
        spin_unlock(&yat_pool_lock);

        // 加入目标CPU的本地运行队列
        struct rq *target_rq = cpu_rq(best_cpu);
        struct yat_casched_rq *target_yat_rq = &target_rq->yat_casched;
        struct rq_flags flags;
        
        // 此处需要获取目标rq的锁来安全地修改其运行队列
        rq_lock(target_rq, &flags); // 使用 rq_lock 获取锁
        list_add_tail(&p_to_run->yat_casched.run_list, &target_yat_rq->tasks);
        target_yat_rq->nr_running++;
        rq_unlock(target_rq, &flags); // 使用 rq_unlock 释放锁
        
        // 唤醒目标CPU（如果它在休眠）并请求重新调度
        resched_curr(target_rq);
    }
}

/*
 * 负载均衡 - 现在是我们的主调度入口
 */
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    // 只在CPU 0上执行全局调度，避免多核竞争
    if (rq->cpu == 0) {
        schedule_yat_casched_tasks();
    }
    return 0;
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

插入debugfs

*/
static struct dentry *yat_debugfs_dir;
static struct dentry *yat_ready_pool_file;
static struct dentry *yat_accelerator_file;
static struct dentry *yat_history_file;
// debugfs 显示加速表内容
static int yat_accelerator_show(struct seq_file *m, void *v)
{
    struct accelerator_entry *entry;
    int bkt, count = 0;
    seq_printf(m, "Yat_Casched Accelerator Table:\nIdx | PID | CPU | Benefit\n");
    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry && entry->p) {
            seq_printf(m, "%3d | %5d | %3d | %8llu\n", count, entry->p->pid, entry->cpu, entry->benefit);
            count++;
        }
    }
    spin_unlock(&accelerator_lock);
    seq_printf(m, "Total: %d\n", count);
    return 0;
}

static int yat_accelerator_open(struct inode *inode, struct file *file)
{
    return single_open(file, yat_accelerator_show, NULL);
}

static const struct file_operations yat_accelerator_fops = {
    .owner = THIS_MODULE,
    .open = yat_accelerator_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

// debugfs 显示历史表内容（每个CPU）
static int yat_history_show(struct seq_file *m, void *v)
{
    int cpu, count = 0;
    struct history_record *rec;
    seq_printf(m, "Yat_Casched History Table (per CPU):\nCPU | PID | Timestamp | ExecTime\n");
    for_each_possible_cpu(cpu) {
        spin_lock(&history_tables[cpu].lock);
        list_for_each_entry(rec, &history_tables[cpu].time_list, list) {
            seq_printf(m, "%3d | %5d | %10llu | %8llu\n", cpu, rec->task_id, rec->timestamp, rec->exec_time);
            count++;
        }
        spin_unlock(&history_tables[cpu].lock);
    }
    seq_printf(m, "Total: %d\n", count);
    return 0;
}

static int yat_history_open(struct inode *inode, struct file *file)
{
    return single_open(file, yat_history_show, NULL);
}

static const struct file_operations yat_history_fops = {
    .owner = THIS_MODULE,
    .open = yat_history_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

// debugfs 显示当前任务池内容
static int yat_ready_pool_show(struct seq_file *m, void *v)
{
    struct task_struct *p;
    int count = 0;
    seq_printf(m, "Yat_Casched Ready Pool:\nIdx | PID | Prio | WCET\n");
    spin_lock(&yat_pool_lock);
    list_for_each_entry(p, &yat_ready_job_pool, yat_casched.run_list) {
        seq_printf(m, "%3d | %5d | %4d | %6llu\n", count, p->pid, p->prio, p->yat_casched.wcet);
        count++;
    }
    spin_unlock(&yat_pool_lock);
    seq_printf(m, "Total: %d\n", count);
    return 0;
}

static int yat_ready_pool_open(struct inode *inode, struct file *file)
{
    return single_open(file, yat_ready_pool_show, NULL);
}

static const struct file_operations yat_ready_pool_fops = {
    .owner = THIS_MODULE,
    .open = yat_ready_pool_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static void yat_debugfs_init(void)
{
    printk(KERN_INFO "[yat] debugfs init called\n");
    yat_debugfs_dir = debugfs_create_dir("yat_casched", NULL);
    if (IS_ERR_OR_NULL(yat_debugfs_dir)) {
        printk(KERN_ERR "[yat] debugfs_create_dir failed: %ld\n", PTR_ERR(yat_debugfs_dir));
        yat_debugfs_dir = NULL;
        return;
    } else {
        printk(KERN_INFO "[yat] debugfs dir created successfully: %p\n", yat_debugfs_dir);
    }

    yat_ready_pool_file = debugfs_create_file("ready_pool", 0444, yat_debugfs_dir, NULL, &yat_ready_pool_fops);
    if (IS_ERR_OR_NULL(yat_ready_pool_file)) {
        printk(KERN_ERR "[yat] debugfs create ready_pool file failed: %ld\n", PTR_ERR(yat_ready_pool_file));
        yat_ready_pool_file = NULL;
    } else {
        printk(KERN_INFO "[yat] debugfs ready_pool file created: %p\n", yat_ready_pool_file);
    }

    yat_accelerator_file = debugfs_create_file("accelerator_table", 0444, yat_debugfs_dir, NULL, &yat_accelerator_fops);
    if (IS_ERR_OR_NULL(yat_accelerator_file)) {
        printk(KERN_ERR "[yat] debugfs create accelerator_table file failed: %ld\n", PTR_ERR(yat_accelerator_file));
        yat_accelerator_file = NULL;
    } else {
        printk(KERN_INFO "[yat] debugfs accelerator_table file created: %p\n", yat_accelerator_file);
    }

    yat_history_file = debugfs_create_file("history_table", 0444, yat_debugfs_dir, NULL, &yat_history_fops);
    if (IS_ERR_OR_NULL(yat_history_file)) {
        printk(KERN_ERR "[yat] debugfs create history_table file failed: %ld\n", PTR_ERR(yat_history_file));
        yat_history_file = NULL;
    } else {
        printk(KERN_INFO "[yat] debugfs history_table file created: %p\n", yat_history_file);
    }
}

static void yat_debugfs_cleanup(void)
{
    if (!IS_ERR_OR_NULL(yat_debugfs_dir)) {
        debugfs_remove_recursive(yat_debugfs_dir);
        yat_debugfs_dir = NULL;
    }
    yat_ready_pool_file = NULL;
    yat_accelerator_file = NULL;
    yat_history_file = NULL;
}

/*
 * 延迟初始化 debugfs，确保在 debugfs 挂载后再创建文件
 */
static int __init yat_debugfs_late_init(void)
{
    printk(KERN_INFO "[yat] late debugfs init called\n");
    yat_debugfs_init();
    return 0;
}
late_initcall(yat_debugfs_late_init);


/*
 * Yat_Casched调度类定义
 */
DEFINE_SCHED_CLASS(yat_casched) = {
    //.next_in_same_domain = NULL, // 根据内核版本决定是否需要
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
    s64 l1_dist, l2_dist, l3_dist;
    int dag_id = 0; // 默认DAG ID
    
    // L1层：单核私有
    l1_dist = calculate_l1_recency(dag_id, job_id, cpu_id, now);
    if (l1_dist >= 0 && l1_dist <= YAT_V2_THRESHOLD) {
        return (u64)l1_dist; // L1命中
    }
    
    // L2层：簇共享
    l2_dist = calculate_l2_recency(dag_id, job_id, cpu_id, now);
    if (l2_dist >= 0 && l2_dist <= YAT_V3_THRESHOLD) {
        return (u64)l2_dist; // L2命中
    }
    
    // L3层：全局共享
    l3_dist = calculate_l3_recency(dag_id, job_id, now);
    if (l3_dist >= 0 && l3_dist <= YAT_V4_THRESHOLD) {
        return (u64)l3_dist; // L3命中
    }
    
    // 完全未命中
    return YAT_V4_THRESHOLD + 1;
}

/* 更新AJLR历史记录添加 - 使用新接口 */
static int yat_add_history_record(int cpu, int job_id, u64 timestamp, u64 exec_time)
{
    // 对于AJLR接口，使用默认DAG ID 0
    add_history_record(cpu, 0, job_id, exec_time, exec_time);
    return 0;
}

/* 更新Yat_Casched运行队列 */
void update_yat_casched_rq(struct rq *rq)
{
    struct task_struct *p;
    u64 now = rq_clock_task(rq);
    
    /* 遍历所有就绪任务，更新它们的虚拟运行时间 */
    list_for_each_entry(p, &rq->yat_casched.tasks, yat_casched.run_list) {
        if (p->state == TASK_RUNNING) {
            update_curr_yat_casched(rq);
        }
    }
    
    /* AJLR调度 */
    yat_schedule_ajlr(rq);
}

/* 更新任务的执行时间戳和历史记录 */
void update_task_exec_time(struct task_struct *p, u64 exec_time)
{
    struct rq *rq = task_rq(p);
    u64 now = rq_clock_task(rq);
    
    /* 更新任务的执行时间戳 */
    p->se.exec_start = now;
    
    /* 更新历史记录 */
    if (p->yat_casched.job) {
        p->yat_casched.job->per_cpu_last_ts[rq->cpu] = now;
        add_history_record(rq->cpu, p->yat_casched.job->job_id, now, exec_time);
    }
}

/* 清理过期的历史记录 */
void cleanup_old_history_records(int cpu, u64 cutoff_time)
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

/* 初始化Yat_Casched调度类 */
void init_yat_casched_class(void)
{
    /* 这里可以添加一些全局的初始化代码 */
}

/* 销毁Yat_Casched调度类 */
void destroy_yat_casched_class(void)
{
    /* 这里可以添加一些全局的清理代码 */
}

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
static void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_job *job;
    struct yat_dag_task *default_dag;
    int user_priority;
    
    /* 检查任务是否已有关联的作业 */
    if (!p->yat_casched.job) {
        /* 获取默认DAG */
       
        default_dag = yat_get_default_dag();
        if (!default_dag)
            return;
        
        /* 获取用户设置的优先级 - 关键修复点 */
        user_priority = p->rt_priority;  /* 直接使用rt_priority字段 */
        
        /* 为新任务创建作业，使用用户设置的优先级 */
        job = yat_create_job(p->pid, user_priority, 1000000, default_dag);  /* 默认1ms WCET */
        if (job) {
            p->yat_casched.job = job;
            job->task = p;
            
            printk(KERN_INFO "yat_casched: Task PID %d enqueued with priority %d (rt_priority=%d)\n", 
                   p->pid, user_priority, p->rt_priority);
        }
    } else {
        /* 如果作业已存在，更新优先级 */
        user_priority = p->rt_priority;
        p->yat_casched.job->job_priority = user_priority;
        
        printk(KERN_INFO "yat_casched: Task PID %d priority updated to %d\n", 
               p->pid, user_priority);
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
    
    if (job1 && job2 && job3) {
        /* 设置依赖关系：job1 -> job2 -> job3 */
        yat_job_add_dependency(job2, job1);
        yat_job_add_dependency(job3, job2);
        
        printk(KERN_INFO "yat_casched: Created test DAG with 3 jobs\n");
    } else {
        printk(KERN_ERR "yat_casched: Failed to create test jobs\n");
    }
}
