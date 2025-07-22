#include <linux/debugfs.h>
#include <linux/seq_file.h>
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
#include <linux/cpumask.h>
#include <linux/topology.h>

/* debugfs 导出接口声明，避免隐式声明和 static 冲突 */
static void yat_debugfs_init(void);
static void yat_debugfs_cleanup(void);

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

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / 1000)	/* 1ms - 更短的时间片 */
#define YAT_TIME_SLICE		(4 * NSEC_PER_MSEC)	/* 4ms 默认时间片 */

/* --- 函数前向声明 --- */
static u64 get_crp_ratio(u64 recency);
static u64 calculate_impact(struct task_struct *p, int cpu);

/* --- 历史表操作函数 --- */

// 初始化历史表
static void init_history_tables(void) {
    int cpu;
    for_each_possible_cpu(cpu) {
        INIT_LIST_HEAD(&history_tables[cpu].time_list);
        spin_lock_init(&history_tables[cpu].lock);
    }
}

// 添加历史记录 - 修改为只维护同一任务的最新记录
static void add_history_record(int cpu, int dag_id, int task_id, u64 expected_exec_time, u64 actual_exec_time) {
    struct history_record *rec, *existing_rec = NULL;
    struct cpu_history_table *table = &history_tables[cpu];
    
    spin_lock(&table->lock);
    
    // 先查找是否已存在同一任务的记录
    list_for_each_entry(rec, &table->time_list, list) {
        if (rec->dag_id == dag_id && rec->task_id == task_id) {
            existing_rec = rec;
            break;
        }
    }
    
    if (existing_rec) {
        // 如果存在，直接更新现有记录
        existing_rec->timestamp = ktime_get();
        existing_rec->exec_time = expected_exec_time;
        existing_rec->actual_exec_time = actual_exec_time;
        
        // 将更新的记录移到链表尾部以维护时间顺序
        list_move_tail(&existing_rec->list, &table->time_list);
    } else {
        // 如果不存在，创建新记录
        rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
        if (!rec) {
            spin_unlock(&table->lock);
            return;
        }
        
        rec->dag_id = dag_id;
        rec->task_id = task_id;
        rec->timestamp = ktime_get();
        rec->exec_time = expected_exec_time;
        rec->actual_exec_time = actual_exec_time;
        
        // 直接插入链表尾部
        list_add_tail(&rec->list, &table->time_list);
    }
    
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
    struct history_record *rec, *target_rec = NULL;
    struct cpu_history_table *table = &history_tables[cpu_id];
    
    spin_lock(&table->lock);
    
    // 先找到目标任务的记录
    list_for_each_entry(rec, &table->time_list, list) {
        if (rec->dag_id == dag_id && rec->task_id == task_id) {
            target_rec = rec;
            break;
        }
    }
    
    if (!target_rec) {
        spin_unlock(&table->lock);
        return -1; // L1未命中 - 没有找到任务记录
    }
    
    // 从目标任务记录之后的记录开始累计执行时间
    struct list_head *pos = target_rec->list.next;
    while (pos != &table->time_list) {
        rec = list_entry(pos, struct history_record, list);
        
        // 累加其他任务的执行时间
        dist += rec->exec_time;
        
        // 超出L1窗口则停止
        if (dist > YAT_V2_THRESHOLD) {
            dist = -1; // L1未命中
            break;
        }
        
        pos = pos->next;
    }
    
    spin_unlock(&table->lock);
    return dist;
}

// L2 Recency计算 - 修改为基于唯一记录的簇共享历史
static s64 calculate_l2_recency(int dag_id, int task_id, int cpu_id, u64 now) {
    s64 dist = 0;
    int cluster_id = cpu_id / YAT_L2_CORES_PER_CLUSTER;
    int cluster_start = cluster_id * YAT_L2_CORES_PER_CLUSTER;
    int cluster_end = cluster_start + YAT_L2_CORES_PER_CLUSTER;
    int cpu;
    
    // 收集簇内所有CPU的唯一任务记录到临时列表（按时间戳排序）
    struct list_head cluster_history;
    struct history_record *rec, *cluster_rec, *tmp_rec;
    struct history_record *target_rec = NULL;
    
    INIT_LIST_HEAD(&cluster_history);
    
    // 遍历簇内所有CPU，收集每个任务的最新记录
    for (cpu = cluster_start; cpu < cluster_end && cpu < NR_CPUS; cpu++) {
        struct cpu_history_table *table = &history_tables[cpu];
        
        spin_lock(&table->lock);
        list_for_each_entry(rec, &table->time_list, list) {
            // 检查是否已经有同一任务的记录
            struct history_record *existing = NULL;
            list_for_each_entry(existing, &cluster_history, list) {
                if (existing->dag_id == rec->dag_id && existing->task_id == rec->task_id) {
                    // 如果找到更新的记录，替换它
                    if (rec->timestamp > existing->timestamp) {
                        existing->timestamp = rec->timestamp;
                        existing->exec_time = rec->exec_time;
                        existing->actual_exec_time = rec->actual_exec_time;
                    }
                    existing = NULL; // 标记已处理
                    break;
                }
            }
            
            // 如果没有找到相同任务，创建新记录
            if (existing != NULL) {
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
                
                // 按时间戳顺序插入
                struct list_head *pos;
                struct history_record *existing_by_time;
                bool inserted = false;
                
                list_for_each(pos, &cluster_history) {
                    existing_by_time = list_entry(pos, struct history_record, list);
                    if (cluster_rec->timestamp < existing_by_time->timestamp) {
                        list_add_tail(&cluster_rec->list, pos);
                        inserted = true;
                        break;
                    }
                }
                if (!inserted) {
                    list_add_tail(&cluster_rec->list, &cluster_history);
                }
            }
        }
        spin_unlock(&table->lock);
    }
    
    // 找到目标任务记录
    list_for_each_entry(cluster_rec, &cluster_history, list) {
        if (cluster_rec->dag_id == dag_id && cluster_rec->task_id == task_id) {
            target_rec = cluster_rec;
            break;
        }
    }
    
    if (!target_rec) {
        dist = -1; // L2未命中 - 没找到目标任务
        goto cleanup_l2;
    }
    
    // 从目标任务之后的记录开始累计
    bool found_target = false;
    list_for_each_entry(cluster_rec, &cluster_history, list) {
        if (cluster_rec == target_rec) {
            found_target = true;
            continue;
        }
        
        if (found_target) {
            dist += cluster_rec->exec_time;
            
            if (dist > YAT_V3_THRESHOLD) {
                dist = -1; // L2未命中
                break;
            }
        }
    }
    
cleanup_l2:
    // 清理临时列表
    list_for_each_entry_safe(cluster_rec, tmp_rec, &cluster_history, list) {
        list_del(&cluster_rec->list);
        kfree(cluster_rec);
    }
    
    return dist;
}

// L3 Recency计算 - 修改为基于唯一记录的全局历史
static s64 calculate_l3_recency(int dag_id, int task_id, u64 now) {
    s64 dist = 0;
    int cpu;
    struct history_record *rec, *global_rec, *tmp_rec;
    struct list_head global_history;
    struct history_record *target_rec = NULL;
    
    INIT_LIST_HEAD(&global_history);
    
    // 收集全局唯一任务记录
    for_each_possible_cpu(cpu) {
        struct cpu_history_table *table = &history_tables[cpu];
        
        spin_lock(&table->lock);
        list_for_each_entry(rec, &table->time_list, list) {
            // 检查是否已有同一任务的记录
            struct history_record *existing = NULL;
            list_for_each_entry(existing, &global_history, list) {
                if (existing->dag_id == rec->dag_id && existing->task_id == rec->task_id) {
                    // 保留最新的记录
                    if (rec->timestamp > existing->timestamp) {
                        existing->timestamp = rec->timestamp;
                        existing->exec_time = rec->exec_time;
                        existing->actual_exec_time = rec->actual_exec_time;
                    }
                    existing = NULL; // 标记已处理
                    break;
                }
            }
            
            // 如果没找到相同任务，创建新记录
            if (existing != NULL) {
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
                
                // 按时间戳排序插入
                struct list_head *pos;
                struct history_record *existing_by_time;
                bool inserted = false;
                
                list_for_each(pos, &global_history) {
                    existing_by_time = list_entry(pos, struct history_record, list);
                    if (global_rec->timestamp < existing_by_time->timestamp) {
                        list_add_tail(&global_rec->list, pos);
                        inserted = true;
                        break;
                    }
                }
                if (!inserted) {
                    list_add_tail(&global_rec->list, &global_history);
                }
            }
        }
        spin_unlock(&table->lock);
    }
    
    // 找到目标任务记录
    list_for_each_entry(global_rec, &global_history, list) {
        if (global_rec->dag_id == dag_id && global_rec->task_id == task_id) {
            target_rec = global_rec;
            break;
        }
    }
    
    if (!target_rec) {
        dist = -1; // L3未命中
        goto cleanup_l3;
    }
    
    // 从目标任务之后的记录开始累计
    bool found_target = false;
    list_for_each_entry(global_rec, &global_history, list) {
        if (global_rec == target_rec) {
            found_target = true;
            continue;
        }
        
        if (found_target) {
            dist += global_rec->exec_time;
            
            if (dist > YAT_V4_THRESHOLD) {
                dist = -1; // L3未命中
                break;
            }
        }
    }
    
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
 * 模拟获取任务WCET的函数
 */
static u64 get_wcet(struct task_struct *p)
{
    // 初步实现：给所有任务一个固定的WCET，例如10ms
    return 10 * 1000 * 1000; // 10ms in ns
}

/*
 * Impact 计算函数
 */
static u64 calculate_impact(struct task_struct *p, int cpu)
{
    return calculate_lcif_impact(p, cpu);
}

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
    
    INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
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

// LCIF策略：计算预分配对加速表的影响
static u64 calculate_lcif_impact(struct task_struct *candidate_task, int candidate_cpu)
{
    u64 total_impact = 0;
    struct task_struct *p;
    int cpu;
    struct accelerator_entry *entry;
    int bkt;
    
    /* 临时保存当前加速表状态 */
    struct list_head old_benefits;
    struct benefit_backup {
        struct list_head list;
        struct task_struct *task;
        int cpu;
        u64 old_benefit;
        u64 new_benefit;
    };
    
    INIT_LIST_HEAD(&old_benefits);
    
    /* Step 1: 备份当前加速表中所有相关的benefit值 */
    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->p && entry->p != candidate_task) {
            struct benefit_backup *backup = kmalloc(sizeof(*backup), GFP_ATOMIC);
            if (backup) {
                backup->task = entry->p;
                backup->cpu = entry->cpu;
                backup->old_benefit = entry->benefit;
                backup->new_benefit = 0; // 待计算
                list_add(&backup->list, &old_benefits);
            }
        }
    }
    spin_unlock(&accelerator_lock);
    
    /* Step 2: 模拟candidate_task分配到candidate_cpu后的历史状态 */
    /* 临时更新历史记录 */
    u64 now = ktime_get();
    int dag_id = 0, task_id = candidate_task->pid;
    
    if (candidate_task->yat_casched.job) {
        dag_id = candidate_task->yat_casched.job->dag ? candidate_task->yat_casched.job->dag->dag_id : 0;
        task_id = candidate_task->yat_casched.job->job_id;
    }
    
    /* 临时添加历史记录（模拟执行） */
    add_history_record(candidate_cpu, dag_id, task_id, 
                      candidate_task->yat_casched.wcet, 
                      candidate_task->yat_casched.wcet);
    
    /* Step 3: 重新计算所有相关任务的benefit值 */
    struct benefit_backup *backup, *tmp_backup;
    list_for_each_entry_safe(backup, tmp_backup, &old_benefits, list) {
        u64 recency = calculate_recency(backup->task, backup->cpu);
        u64 crp_ratio = get_crp_ratio(recency);
        backup->new_benefit = ((1000 - crp_ratio) * backup->task->yat_casched.wcet) / 1000;
        
        /* 计算影响值（绝对差值） */
        if (backup->new_benefit > backup->old_benefit) {
            total_impact += backup->new_benefit - backup->old_benefit;
        } else {
            total_impact += backup->old_benefit - backup->new_benefit;
        }
    }
    
    /* Step 4: 回滚历史记录的临时修改 */
    /* 找到并删除刚才临时添加的记录 */
    struct cpu_history_table *table = &history_tables[candidate_cpu];
    struct history_record *rec, *tmp_rec;
    
    spin_lock(&table->lock);
    list_for_each_entry_safe(rec, tmp_rec, &table->time_list, list) {
        if (rec->dag_id == dag_id && rec->task_id == task_id && 
            abs((s64)(rec->timestamp - now)) < 1000000) { /* 1ms内的记录 */
            list_del(&rec->list);
            kfree(rec);
            break; /* 只删除最新添加的一个 */
        }
    }
    spin_unlock(&table->lock);
    
    /* Step 5: 清理备份数据 */
    list_for_each_entry_safe(backup, tmp_backup, &old_benefits, list) {
        list_del(&backup->list);
        kfree(backup);
    }
    
    return total_impact;
}

/* 修改调度决策函数，实现完整的LCIF策略 */
static void schedule_yat_casched_tasks(void)
{
    struct accelerator_entry *entry, *best_entry = NULL;
    struct task_struct *p_to_run = NULL;
    int best_cpu = -1;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;
    int bkt;
    
    /* 候选任务列表 - 用于存储benefit相同的候选项 */
    struct list_head candidates;
    struct candidate_entry {
        struct list_head list;
        struct accelerator_entry *entry;
        u64 lcif_impact;
    };
    
    INIT_LIST_HEAD(&candidates);
    
    // 1. 更新加速表，缓存所有当前可行的(任务,核心)对的benefit
    update_accelerator_table();

    // 2. 第一轮：遍历加速表，找到最大的 benefit 值
    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->benefit > max_benefit) {
            max_benefit = entry->benefit;
        }
    }

    // 3. 收集所有benefit最大的候选项
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->benefit == max_benefit) {
            struct candidate_entry *candidate = kmalloc(sizeof(*candidate), GFP_ATOMIC);
            if (candidate) {
                candidate->entry = entry;
                candidate->lcif_impact = 0; // 待计算
                list_add(&candidate->list, &candidates);
            }
        }
    }
    spin_unlock(&accelerator_lock);

    // 4. LCIF策略：对所有候选项计算Impact
    struct candidate_entry *candidate, *tmp_candidate;
    list_for_each_entry(candidate, &candidates, list) {
        candidate->lcif_impact = calculate_lcif_impact(
            candidate->entry->p, candidate->entry->cpu);
    }

    // 5. 选择Impact最小的候选项
    list_for_each_entry_safe(candidate, tmp_candidate, &candidates, list) {
        if (candidate->lcif_impact < min_impact) {
            min_impact = candidate->lcif_impact;
            best_entry = candidate->entry;
        }
    }

    // 6. 执行调度决策
    if (best_entry) {
        p_to_run = best_entry->p;
        best_cpu = best_entry->cpu;

        printk(KERN_INFO "LCIF: Selected task PID=%d for CPU=%d, benefit=%llu, impact=%llu\n",
               p_to_run->pid, best_cpu, max_benefit, min_impact);

        // 从就绪池移除
        spin_lock(&yat_pool_lock);
        list_del_init(&p_to_run->yat_casched.run_list);
        spin_unlock(&yat_pool_lock);

        // 加入目标CPU的本地运行队列
        struct rq *target_rq = cpu_rq(best_cpu);
        struct yat_casched_rq *target_yat_rq = &target_rq->yat_casched;
        struct rq_flags flags;
        
        rq_lock(target_rq, &flags);
        list_add_tail(&p_to_run->yat_casched.run_list, &target_yat_rq->tasks);
        target_yat_rq->nr_running++;
        
        // 更新任务的recency信息和历史记录
        if (p_to_run->yat_casched.last_cpu != best_cpu) {
            if (p_to_run->yat_casched.last_cpu != -1) {
                p_to_run->yat_casched.per_cpu_recency[p_to_run->yat_casched.last_cpu] = 0;
            }
            p_to_run->yat_casched.per_cpu_recency[best_cpu] = ktime_get();
            p_to_run->yat_casched.last_cpu = best_cpu;
            
            /* 正式添加历史记录 */
            int dag_id = 0, task_id = p_to_run->pid;
            if (p_to_run->yat_casched.job) {
                dag_id = p_to_run->yat_casched.job->dag ? p_to_run->yat_casched.job->dag->dag_id : 0;
                task_id = p_to_run->yat_casched.job->job_id;
            }
            add_history_record(best_cpu, dag_id, task_id, 
                             p_to_run->yat_casched.wcet, 0);
        }
        
        rq_unlock(target_rq, &flags);
        
        // 唤醒目标CPU并请求重新调度
        resched_curr(target_rq);
        
        /* 最后更新加速表 - 反映实际调度的影响 */
        update_accelerator_table();
    }
    
    // 7. 清理候选项列表
    list_for_each_entry_safe(candidate, tmp_candidate, &candidates, list) {
        list_del(&candidate->list);
        kfree(candidate);
    }
}

/* --- 函数实现 --- */

/* debugfs 相关函数 */
static struct dentry *yat_debugfs_dir;
static struct dentry *yat_ready_pool_file;
static struct dentry *yat_accelerator_file;
static struct dentry *yat_history_file;

// debugfs 显示加速表内容
static int yat_accelerator_show(struct seq_file *m, void *v)
{
    struct accelerator_entry *entry;
    int bkt, count = 0;
    seq_printf(m, "Yat_Casched Accelerator Table (LCIF-enabled):\n");
    seq_printf(m, "Idx | PID   | CPU | Benefit  | Last_Impact\n");
    seq_printf(m, "----|-------|-----|----------|------------\n");
    
    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry && entry->p) {
            /* 计算当前的LCIF impact作为参考 */
            u64 current_impact = calculate_lcif_impact(entry->p, entry->cpu);
            seq_printf(m, "%3d | %5d | %3d | %8llu | %8llu\n", 
                       count, entry->p->pid, entry->cpu, entry->benefit, current_impact);
            count++;
        }
    }
    spin_unlock(&accelerator_lock);
    seq_printf(m, "\nTotal entries: %d\nStrategy: LCIF (Least Cache Impact First)\n", count);
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
 * 全局调度函数 - 实现全局任务调度
 * 从全局任务池中选择任务并分配到最佳CPU
 */
static void schedule_yat_casched_tasks(void)
{
    struct yat_pool_entry *entry;
    struct yat_pool_entry *best_entry = NULL;
    u64 highest_priority = 0;
    struct task_struct *p_to_run = NULL;
    int best_cpu = -1;
    u64 best_recency = 0;
    
    spin_lock(&yat_pool_lock);
    
    // 遍历全局任务池，寻找最优任务
    list_for_each_entry(entry, &yat_task_pool, list) {
        struct task_struct *p = entry->p;
        int cpu = entry->cpu;
        u64 recency = 0;
        
        // 检查任务是否仍然有效且可调度
        if (!p || p->__state != TASK_RUNNING)
            continue;
        
        // 计算该任务在对应CPU上的recency
        // 对于同一个任务，只考虑最新的一次运行记录
        if (p->yat_casched.last_cpu != -1 && p->yat_casched.last_cpu < NR_CPUS) {
            recency = p->yat_casched.per_cpu_recency[cpu];
        }
        
        // 基于vruntime和recency计算优先级
        u64 priority = (NICE_0_LOAD * 1000000) / (p->yat_casched.vruntime + 1) + recency;
        
        if
        
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
    }
    
    // 更新缓存历史记录
    update_cache_history(rq, p);
    
    // 记录任务执行信息到trace
    trace_sched_put_prev_task(rq, p);
}

/* 获取CPU所属的缓存ID */
static int get_cache_id_for_cpu(int cpu)
{
    /* 这里需要根据具体的硬件架构来确定缓存拓扑
     * 简化实现：假设每个CPU cluster共享一个L2缓存
     * 实际实现中应该通过sysfs或硬件信息来获取
     */
    return cpu / 4; // 假设每4个CPU共享一个缓存
}

/* 获取系统中缓存的总数 */
static int get_nr_caches(void)
{
    /* 简化实现：假设系统有8个CPU，每4个CPU共享一个缓存 */
    return (num_possible_cpus() + 3) / 4;
}

/* 更新缓存历史记录 */
static void update_cache_history(struct rq *rq, struct task_struct *p)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    int cpu = cpu_of(rq);
    int cache_id = get_cache_id_for_cpu(cpu);
    
    if (cache_id >= yat_rq->nr_caches)
        return;
    
    spin_lock(&yat_rq->history_lock);
    yat_rq->cache_histories[cache_id].last_task = p;
    yat_rq->cache_histories[cache_id].last_time = rq_clock(rq);
    spin_unlock(&yat_rq->history_lock);
}

/* 检查任务是否在指定缓存中有历史记录 */
static bool task_has_cache_history(struct yat_casched_rq *yat_rq, 
                                  struct task_struct *p, int cache_id)
{
    bool has_history = false;
    
    if (cache_id >= yat_rq->nr_caches)
        return false;
    
    spin_lock(&yat_rq->history_lock);
    has_history = (yat_rq->cache_histories[cache_id].last_task == p);
    spin_unlock(&yat_rq->history_lock);
    
    return has_history;
}

/* 计算基于缓存感知的调度权重 */
static u64 calculate_cache_aware_weight(struct task_struct *p, int target_cpu)
{
    struct yat_casched_rq *yat_rq = &cpu_rq(target_cpu)->yat_casched;
    int cache_id = get_cache_id_for_cpu(target_cpu);
    u64 weight = 1000; /* 基础权重 */
    
    /* 如果任务在目标缓存中有历史记录，增加权重 */
    if (task_has_cache_history(yat_rq, p, cache_id)) {
        weight += 500; /* 缓存亲和性奖励 */
    }
    
    /* 根据缓存衰减时间调整权重 */
    if (yat_rq->cache_histories[cache_id].last_task == p) {
        u64 time_diff = rq_clock(cpu_rq(target_cpu)) - 
                       yat_rq->cache_histories[cache_id].last_time;
        if (time_diff < yat_rq->cache_decay_jiffies) {
            weight += (yat_rq->cache_decay_jiffies - time_diff) / 1000;
        }
    }
    
    return weight;
}

void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = msecs_to_jiffies(100); /* 100ms缓存衰减时间 */
    spin_lock_init(&rq->history_lock);
    
    /* 初始化缓存历史表 */
    rq->nr_caches = get_nr_caches();
    rq->cache_histories = kzalloc(rq->nr_caches * sizeof(struct cache_history), GFP_KERNEL);
    
    if (rq->cache_histories) {
        int i;
        for (i = 0; i < rq->nr_caches; i++) {
            rq->cache_histories[i].last_task = NULL;
            rq->cache_histories[i].last_time = 0;
        }
    }
}

/* CPU选择函数 - 基于缓存感知调度 */
int select_task_rq_yat_casched(struct task_struct *p, int cpu, int flags)
{
    int best_cpu = cpu;
    u64 best_weight = 0;
    int i;
    
    /* 遍历所有可用的CPU，选择最佳的缓存感知CPU */
    for_each_cpu(i, &p->cpus_mask) {
        if (!cpu_online(i))
            continue;
            
        u64 weight = calculate_cache_aware_weight(p, i);
        
        /* 选择权重最高的CPU */
        if (weight > best_weight) {
            best_weight = weight;
            best_cpu = i;
        }
    }
    
    return best_cpu;
}

/* 负载均衡函数 - 考虑缓存亲和性 */
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    /* 如果队列为空，尝试从其他CPU拉取任务 */
    if (yat_rq->nr_running == 0) {
        return pull_yat_casched_task(rq);
    }
    
    return 0;
}

/* 从其他CPU拉取任务 - 考虑缓存亲和性 */
static int pull_yat_casched_task(struct rq *this_rq)
{
    int this_cpu = cpu_of(this_rq);
    int src_cpu;
    struct rq *src_rq;
    struct task_struct *p;
    
    /* 遍历其他CPU，寻找可以迁移的任务 */
    for_each_online_cpu(src_cpu) {
        if (src_cpu == this_cpu)
            continue;
            
        src_rq = cpu_rq(src_cpu);
        
        /* 检查是否有可迁移的YAT_CASCHED任务 */
        list_for_each_entry(p, &src_rq->yat_casched.tasks, yat_casched.run_list) {
            if (can_migrate_task(p, this_cpu)) {
                /* 迁移任务 */
                if (migrate_yat_casched_task(p, this_rq, src_rq)) {
                    return 1;
                }
            }
        }
    }
    
    return 0;
}

/* 检查任务是否可以迁移到目标CPU */
static bool can_migrate_task(struct task_struct *p, int dest_cpu)
{
    if (!cpumask_test_cpu(dest_cpu, &p->cpus_mask))
        return false;
        
    if (task_running(task_rq(p), p))
        return false;
        
    return true;
}

/* 迁移YAT_CASCHED任务 */
static int migrate_yat_casched_task(struct task_struct *p, struct rq *dest_rq, struct rq *src_rq)
{
    struct rq_flags rf;
    
    /* 双重锁定避免死锁 */
    if (dest_rq < src_rq) {
        rq_lock(dest_rq, &rf);
        rq_lock_nested(src_rq, SINGLE_DEPTH_NESTING);
    } else {
        rq_lock(src_rq, &rf);
        rq_lock_nested(dest_rq, SINGLE_DEPTH_NESTING);
    }
    
    /* 检查任务是否仍然可迁移 */
    if (!can_migrate_task(p, cpu_of(dest_rq))) {
        if (dest_rq < src_rq) {
            rq_unlock_nested(src_rq, SINGLE_DEPTH_NESTING);
            rq_unlock(dest_rq, &rf);
        } else {
            rq_unlock_nested(dest_rq, SINGLE_DEPTH_NESTING);
            rq_unlock(src_rq, &rf);
        }
        return 0;
    }
    
    /* 从源队列移除 */
    dequeue_task_yat_casched(src_rq, p, 0);
    
    /* 设置新的CPU */
    set_task_cpu(p, cpu_of(dest_rq));
    
    /* 加入目标队列 */
    enqueue_task_yat_casched(dest_rq, p, 0);
    
    /* 解锁 */
    if (dest_rq < src_rq) {
        rq_unlock_nested(src_rq, SINGLE_DEPTH_NESTING);
        rq_unlock(dest_rq, &rf);
    } else {
        rq_unlock_nested(dest_rq, SINGLE_DEPTH_NESTING);
        rq_unlock(src_rq, &rf);
    }
    
    return 1;
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
