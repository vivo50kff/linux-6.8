/*
 * Yat_Casched - Cache-Aware Scheduler
 *
 * 这是一个基于缓存感知性的调度器实现。
 * 主要特性：
 * - 维护CPU历史表，记录每个任务上次运行的CPU
 * - 优先将任务调度到上次运行的CPU核心以提高缓存局部性
 * - 在需要负载均衡时智能选择新的CPU核心
 */

#include "sched.h"
#include <linux/sched/yat_casched.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/processor.h>
#include <linux/time.h>
#include <linux/atomic.h>
#include <linux/limits.h>
#include <linux/random.h>
#include <linux/rbtree.h> // <-- 关键修复：添加红黑树头文件
#include <linux/hashtable.h> // <-- 关键优化：添加哈希表头文件

#define CPU_NUM_PER_SET 2  // L2缓存集群的核心数
#define HISTORY_HASHTABLE_BITS 8 // 为历史记录哈希表定义大小

/* debugfs 导出接口声明，避免隐式声明和 static 冲突 */
static void yat_debugfs_init(void);
static void yat_debugfs_cleanup(void);

/* --- 核心数据结构定义 (v2.2 - 最终版) --- */

// 历史记录: 使用双向链表，按时间戳顺序记录历史
struct history_record {
    struct hlist_node hash_node; // <-- 使用哈希链表节点
    struct list_head time_list_node; // <-- 保留链表节点用于时间顺序淘汰
    int task_id;
    u64 timestamp;
    u64 exec_time;
};

// 历史表: 每个缓存一个，包含一个链表头和锁
struct cache_history_table {
    DECLARE_HASHTABLE(records, HISTORY_HASHTABLE_BITS); // <-- 使用哈希表
    struct list_head time_list; // <-- 仍然需要链表来维护时间顺序
    spinlock_t lock;
};

// L1, L2, L3 缓存的历史表
// L1 cache is per-cpu
static struct cache_history_table L1_caches[NR_CPUS];
// 假设每个L2缓存集群有4个核心，这是一个简化
#define L2_CACHE_CLUSTERS (NR_CPUS / CPU_NUM_PER_SET)
static struct cache_history_table L2_caches[NR_CPUS/CPU_NUM_PER_SET]; // 每个集群一个历史表
// L3 cache is global
static struct cache_history_table L3_cache;


// 就绪作业池: 存放所有待调度的Yat_Casched任务
// static LIST_HEAD(yat_ready_job_pool);
// static DEFINE_SPINLOCK(yat_pool_lock);

// 全局调度锁，保护 idle_cores 数组和调度决策过程
// static DEFINE_SPINLOCK(global_schedule_lock);

// 空闲核心状态数组: 1 表示空闲, 0 表示繁忙
// 初始化时假设所有核心都空闲
// static int idle_cores[NR_CPUS] = {[0 ... NR_CPUS-1] = 1};


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

// --- 优化：将重复逻辑封装为标准的 static inline 辅助函数 ---
static inline void __update_cache_level(struct cache_history_table *table, int pid, u64 ts, u64 et)
{
    struct history_record *rec = NULL;
    struct history_record *old_rec = NULL;

    spin_lock(&table->lock);

    // 1. O(1) 查找旧记录
    hash_for_each_possible(table->records, old_rec, hash_node, pid) {
        if (old_rec->task_id == pid) {
            // 找到了，从哈希表和链表中删除
            hash_del(&old_rec->hash_node);
            list_del(&old_rec->time_list_node);
            kfree(old_rec);
            break; // 假设PID唯一，找到即可退出
        }
    }

    // 2. 创建新记录
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec) {
        spin_unlock(&table->lock);
        return;
    }
    rec->task_id = pid;
    rec->timestamp = ts;
    rec->exec_time = et;

    // 3. O(1) 插入新记录
    hash_add(table->records, &rec->hash_node, pid);
    list_add_tail(&rec->time_list_node, &table->time_list);

    spin_unlock(&table->lock);
}

// 初始化历史表
static void init_history_tables(void) {
    int i;
    for_each_possible_cpu(i) {
        hash_init(L1_caches[i].records); // <-- 初始化哈希表
        INIT_LIST_HEAD(&L1_caches[i].time_list);
        spin_lock_init(&L1_caches[i].lock);
    }
    for (i = 0; i < NR_CPUS / CPU_NUM_PER_SET; i++) {
        hash_init(L2_caches[i].records); // <-- 初始化哈希表
        INIT_LIST_HEAD(&L2_caches[i].time_list);
        spin_lock_init(&L2_caches[i].lock);
    }
    hash_init(L3_cache.records); // <-- 初始化哈希表
    INIT_LIST_HEAD(&L3_cache.time_list);
    spin_lock_init(&L3_cache.lock);
}

// 添加历史记录
static void add_history_record(int cpu, struct task_struct *p, u64 exec_time) {
    int l2_cluster_id = cpu / CPU_NUM_PER_SET;
    u64 now = rq_clock_task(cpu_rq(cpu));

    // 分别更新 L1, L2, L3
    __update_cache_level(&L1_caches[cpu], p->pid, now, exec_time);
    __update_cache_level(&L2_caches[l2_cluster_id], p->pid, now, exec_time);
    __update_cache_level(&L3_cache, p->pid, now, exec_time);

    // 更新任务的 per-cpu 最近执行时间戳
    p->yat_casched.per_cpu_recency[cpu] = now;
}

static u64 calculate_and_prune_recency(struct cache_history_table *table, struct task_struct *p)
{
    u64 recency = 0;
    struct history_record *rec, *tmp;
    int task_id = p->pid;
    bool found = false;

    spin_lock(&table->lock);
    // 从链表尾部向前安全遍历（因为要删除节点）
    // --- 修复：使用正确的成员名 time_list_node ---
    list_for_each_entry_safe_reverse(rec, tmp, &table->time_list, time_list_node) {
        if (rec->task_id == task_id) {
            // 找到了上一次的记录，停止累加，并删除该记录
            found = true;
            // _initlist_del_init(&rec->list);
            // kfree(rec);y
            break;
        }
        // 累加其他任务的执行时间
        recency += rec->exec_time;
    }
    spin_unlock(&table->lock);

    if (!found) {
        // 如果没找到历史记录，说明是冷缓存，可以返回一个很大的值;-1表示没找到
        // 这里的recency是该缓存上所有历史任务的总执行时间
        return -1;
    }

    return recency;
}

// // 计算多个CPU在时间区间内的总执行时间
// static u64 sum_exec_time_multi(const struct cpumask *cpus, u64 start_ts, u64 end_ts) {
//     u64 total_sum = 0;
//     int cpu;
//     // 注意：这个函数现在的逻辑可能需要根据新的缓存结构调整
//     // 目前暂时保留，但其调用者 (calculate_recency) 需要被重写
//     for_each_cpu(cpu, cpus) {
//         total_sum += sum_exec_time_since(&L1_caches[cpu], start_ts, end_ts);
//     }
//     return total_sum;
// }

// // 获取任务在指定CPU集合上的最晚执行时间
// static u64 get_max_last_exec_time(struct task_struct *p, const struct cpumask *cpus) {
//     u64 max_ts = 0;
//     int cpu;
//     for_each_cpu(cpu, cpus) {
//         if (p->yat_casched.per_cpu_recency[cpu] > max_ts) {
//             max_ts = p->yat_casched.per_cpu_recency[cpu];
//         }
//     }
//     return max_ts;
// }


/* --- Recency 和 Impact 计算 (v2.1) --- */

// 计算多层缓存 Recency (v3.1 - 选择性计算)
static u64 calculate_recency(struct task_struct *p, int cpu_id) {
    u64 recency;
    int l2_cluster_id = cpu_id / CPU_NUM_PER_SET; // 简化假设

    // 1. 尝试 L1
    recency = calculate_and_prune_recency(&L1_caches[cpu_id], p);
    if (recency != -1 && recency < 1000000) { // 命中且在 L1 范围内 (1ms)
        return recency;
    }

    // 2. 尝试 L2
    recency = calculate_and_prune_recency(&L2_caches[l2_cluster_id], p);
    if (recency != -1 && recency < 10000000) { // 命中且在 L2 范围内 (10ms)
        return recency;
    }

    // 3. 尝试 L3
    recency = calculate_and_prune_recency(&L3_cache, p);
    if (recency != -1 && recency < 50000000) { // 命中且在 L3 范围内 (50ms)
        return recency;
    }

    // 4. 完全冷缓存
    return ULLONG_MAX;
}

//计算将任务 p 放到 cpu_id 上对其他任务的 Impact
// static u64 calculate_impact(struct task_struct *p, int cpu_id) {
//     // 简化实现：计算该决策对所有其他待调度任务的 benefit 损失之和
//     u64 total_impact = 0;
//     struct task_struct *other_p;

//     spin_lock(&yat_pool_lock);
//     list_for_each_entry(other_p, &yat_ready_job_pool, yat_casched.run_list) {
//         if (other_p == p) continue;

//         // 原本 other_p 在 cpu_id 上的 benefit
//         u64 old_recency = calculate_recency(other_p, cpu_id);
//         u64 old_crp = get_crp_ratio(old_recency);
//         u64 old_benefit = ((1000 - old_crp) * other_p->yat_casched.wcet) / 1000;

//         // p 占用 cpu_id 后，other_p 的 recency 会增加 p 的执行时间
//         u64 new_recency = old_recency + p->yat_casched.wcet;
//         u64 new_crp = get_crp_ratio(new_recency);
//         u64 new_benefit = ((1000 - new_crp) * other_p->yat_casched.wcet) / 1000;
        
//         if (old_benefit > new_benefit) {
//             total_impact += (old_benefit - new_benefit);
//         }
//     }
//     spin_unlock(&yat_pool_lock);

//     return total_impact;
// }


/*
 * 模拟CRP模型函数
 * 输入: recency (ns) - 任务上次运行距今的时间
 * 输出: crp_ratio (0-1000) - 执行时间占WCET的千分比
 */
static u64 get_crp_ratio(u64 recency)
{
    // v2.4: 分段线性折线函数，模拟论文CRP曲线
    // 单位：ns
    if (recency < 1000000) { // 0~1ms (L1)
        // 600~800线性递增
        // y = 600 + (recency / 1000000) * (800-600)
        return 600 + (recency * 200) / 1000000;
    }
    if (recency < 10000000) { // 1ms~10ms (L2)
        // 800~950线性递增
        // y = 800 + ((recency-1000000) / 9000000) * (950-800)
        return 800 + ((recency - 1000000) * 150) / 9000000;
    }
    if (recency < 50000000) { // 10ms~50ms (L3)
        // 950~1000线性递增
        // y = 950 + ((recency-10000000) / 40000000) * (1000-950)
        return 950 + ((recency - 10000000) * 50) / 40000000;
    }
    return 1000; // 超过50ms，完全冷
}

/*
 * 模拟获取任务WCET的函数
 */
static u64 get_wcet(struct task_struct *p)
{
    // 返回 1~10ms 的随机值（单位：纳秒）
    // u32 rand_ms = (get_random_u32() % 100000)+1; // 1~100000
    // return (u64)rand_ms  ;   // 1~100ms
    u64 test_wcet=10; // 测试用，假设WCET为10ms
    return test_wcet;
}

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / HZ)

/*
 * 初始化Yat_Casched运行队列
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    // struct rq *main_rq = container_of(rq, struct rq, yat_casched);
    static atomic_t global_init_done = ATOMIC_INIT(0);

    // 只允许第一个调用者执行全局初始化
    if (atomic_cmpxchg(&global_init_done, 0, 1) == 0) {
        // printk(KERN_INFO "======Yat_Casched: Global Init======\n");
        init_history_tables();
        hash_init(accelerator_table); // 初始化哈希表
        // 初始化全局锁
        // spin_lock_init(&global_schedule_lock);
    }
    // printk(KERN_INFO "======init yat_casched rq for cpu %d======\n", main_rq->cpu);

    // INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = jiffies;
    spin_lock_init(&rq->history_lock);
    /* 此处逻辑是错误的，rq->cpu_history不存在，且不应在此处初始化所有CPU的历史表 */
    /*
    for (i = 0; i < NR_CPUS; i++) {
        rq->cpu_history[i] = NULL;
    }
    */
    // 初始化红黑树根节点
    rq->tasks = RB_ROOT_CACHED;
    // ...existing code...
}

/*
 * 检查任务是否属于Yat_Casched调度类
 */
bool yat_casched_prio(struct task_struct *p)
{
    // printk(KERN_INFO "[yat] yat_casched_prio: PID=%d check policy\n", p->pid);
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

/* --- 新模型辅助函数 (v3.0) --- */

// 检查是否有空闲核心
// static bool has_idle_cores(void)
// {
//     int i;
//     bool has_idle = false;
//     // 这个函数必须在 global_schedule_lock 保护下调用
//     for_each_online_cpu(i) {
//         if (idle_cores[i]) {
//             has_idle = true;
//             break;
//         }
//     }
//     return has_idle;
// }

// 检查任务是否为全局池的队头
// static bool is_head_task(struct task_struct *p)
// {
//     struct task_struct *head;
//     bool is_head = false;
//     spin_lock(&yat_pool_lock);
//     if (!list_empty(&yat_ready_job_pool)) {
//         head = list_first_entry(&yat_ready_job_pool, struct task_struct, yat_casched.run_list);
//         if (head == p) {
//             is_head = true;
//         }
//     }
//     spin_unlock(&yat_pool_lock);
//     return is_head;
// }

// 为任务寻找最佳CPU（v3.1 - 完整版）
// 通过计算当前每个空闲核心对于任务的benefit选择最佳核心，
// 如果出现两个相同benefit，则采用最小影响法，计算对其他的impact，选择impact最小的核心
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    // printk(KERN_INFO "[yat] select_task_rq_yat_casched: PID=%d, task_cpu=%d\n", p->pid, task_cpu);
    int best_cpu = -1;
    int idle_count = 0;
    int *idle_cpus = NULL;
    int i;
    int last_cpu;
    bool last_cpu_is_idle = false;

    if(p->yat_casched.last_cpu < 0) {
        // 如果没有上次运行的CPU，直接返回当前任务所在的CPU
        // 这是一个新任务或其上次运行的CPU已下线。
        // 为其寻找一个负载最低的CPU进行初始放置。
        int min_load_cpu = -1;
        unsigned long min_load = ULONG_MAX;

        for_each_online_cpu(i) {
            struct rq *rq = cpu_rq(i);
            // 使用YAT_CASCHED调度类自己的运行队列任务数作为负载衡量标准
            if (rq->yat_casched.nr_running < min_load) {
                min_load = rq->yat_casched.nr_running;
                min_load_cpu = i;
            }
        }

        return min_load_cpu;
    }
    else{
        last_cpu = p->yat_casched.last_cpu;
    }

    idle_cpus = kmalloc_array(NR_CPUS, sizeof(int), GFP_ATOMIC);
    if (!idle_cpus) {
        // 内存分配失败，回退到上次的CPU
        return cpu_online(last_cpu) ? last_cpu : task_cpu;
    }

    // --- 优化后的单次遍历逻辑 ---
    for_each_online_cpu(i) {
        if (cpu_rq(i)->yat_casched.nr_running == 0) {
            // 发现一个空闲核心
            if (i == last_cpu) {
                // 这个空闲核心就是上次运行的核心，这是最佳情况。
                // 直接选定并跳出循环。
                best_cpu = last_cpu;
                last_cpu_is_idle = true;
                break;
            }
            // 记录其他空闲核心
            idle_cpus[idle_count] = i;
            idle_count++;
        }
    }

    if (last_cpu_is_idle) {
        // 已经在循环中找到了最佳选择 (last_cpu)，直接跳转到末尾。
        goto out;
    }

    
    // --- 根据遍历结果进行决策 ---
    if (idle_count == 0) {
        // 没有任何空闲核心，只能选择 last_cpu (即使它很忙)。
        best_cpu = last_cpu;
    } else if (idle_count == 1) {
        // 只有一个空闲核心可供选择。
        best_cpu = idle_cpus[0];
    } else {
        // 有多个空闲核心，计算 benefit 来选择最佳的一个。
        u64 max_benefit = 0;
        best_cpu = idle_cpus[0]; // 默认选第一个作为保底
        int cpu_id;
        for (i = 0; i < idle_count; i++) {
            cpu_id = idle_cpus[i];
            u64 recency = calculate_recency(p, cpu_id);
            u64 crp_ratio = get_crp_ratio(recency);
            u64 benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;
            if (benefit > max_benefit) {
                max_benefit = benefit;
                best_cpu = cpu_id;
            }
        }
        struct accelerator_entry *entry;
        entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
        if (entry) {
            entry->p = p;
            entry->cpu = cpu_id;
            entry->benefit = max_benefit;
            
            spin_lock(&accelerator_lock);
            // 使用任务和CPU的地址组合作为唯一的key。
            // 注意：在实际生产环境中，加入前最好先检查并删除旧条目以避免重复。
            hash_add(accelerator_table, &entry->node, (unsigned long)p + cpu_id);
            spin_unlock(&accelerator_lock);
        }
    }

out:
    kfree(idle_cpus);
    // 确保返回一个有效的在线CPU
    return cpu_online(best_cpu) ? best_cpu : cpumask_any(cpu_online_mask);
}

/*
 * 将任务加入本地运行队列 (v3.0)
 * 这个函数现在由内核在 select_task_rq 之后调用。
 * 它的职责是把任务真正放入目标CPU的本地队列。
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct rb_node **link = &yat_rq->tasks.rb_root.rb_node, *parent = NULL;
    struct sched_yat_casched_entity *se_entry; // <-- 使用正确的类型
    struct task_struct *task_entry;
    u64 key;
    int leftmost = 1;

    /* --- 关键修复 #1：在入队前，彻底清除节点的旧状态 --- */
    RB_CLEAR_NODE(&p->yat_casched.rb_node);

    /* --- 关键修复 #2：确保 wcet 被初始化 (仍然保留以备后用) --- */
    if (unlikely(p->yat_casched.wcet == 0)) {
        p->yat_casched.wcet = get_wcet(p);
    }

    key = (u64)p->pid;

    while (*link) {
        parent = *link;
        // --- 关键修复：第一步，找到 sched_yat_casched_entity ---
        se_entry = rb_entry(parent, struct sched_yat_casched_entity, rb_node);
        // --- 关键修复：第二步，从 entity 找到 task_struct ---
        task_entry = container_of(se_entry, struct task_struct, yat_casched);

        u64 entry_key = (u64)task_entry->pid; // 使用正确的 task_entry
        if (key < entry_key) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
            leftmost = 0;
        }
    }

    rb_link_node(&p->yat_casched.rb_node, parent, link);
    rb_insert_color_cached(&p->yat_casched.rb_node, &yat_rq->tasks, leftmost);
    yat_rq->nr_running++;
    // printk(KERN_INFO "[yat] enqueue_task_yat_casched: PID=%d,CPU=%d, prio=%d, wcet=%llu, key(PID)=%llu\n", p->pid,rq->cpu, p->prio, p->yat_casched.wcet, key);
}


/*
 * 将任务从本地运行队列移除 (v3.0)
 * 任务执行完毕或被抢占时调用。
 * 关键职责：释放其占用的核心，将其标记为空闲，并触发一次调度检查。
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;



    // 确保任务在队列中才执行移除操作
    if (!RB_EMPTY_NODE(&p->yat_casched.rb_node)) {
        rb_erase_cached(&p->yat_casched.rb_node, &yat_rq->tasks);
        RB_CLEAR_NODE(&p->yat_casched.rb_node); // 清除节点状态
        if (yat_rq->nr_running > 0)
            yat_rq->nr_running--;
    }
}

/*
 * pick_next_task - 从本地运行队列取任务
 * 因为我们保证每个核心最多一个任务，所以逻辑很简单
 */
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct rb_node *node = rb_first_cached(&yat_rq->tasks);
    struct sched_yat_casched_entity *se;
    struct task_struct *p;

    if (!node){
        // printk(KERN_INFO "[yat] pick_next_task_yat_casched: CPU=%d return NULL ",rq->cpu);
        return NULL;
    }
        

    // --- 关键修复：分两步走，正确地从节点找到任务结构体 ---
    se = rb_entry(node, struct sched_yat_casched_entity, rb_node);
    p = container_of(se, struct task_struct, yat_casched);
    // printk(KERN_INFO "[yat]  CPU=%d  PID=%d",rq->cpu,p->pid);
    return p;
}

/*
 * 设置下一个任务 (set_next_task)
 *
 * 当调度器决定将任务 `p` 放到CPU上运行时，在实际上下文切换之前，会调用此函数。
 * 它的核心作用是记录任务即将开始执行的时间点。
 * - `p->se.exec_start = rq_clock_task(rq);` 这一行将当前运行队列的时钟 (`rq_clock_task`)
 *   赋值给任务的 `exec_start` 字段。
 * 这个时间戳将在任务被换下CPU时（在 `put_prev_task_yat_casched` 中）被用来计算
 * 该任务本次在CPU上实际运行了多长时间。
 */
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first)
{
    p->se.exec_start = rq_clock_task(rq);
}

/*
 * 放置前一个任务 (put_prev_task)
 *
 * 当一个任务停止在CPU上运行时（例如被抢占、执行完毕或主动放弃CPU），内核会调用此函数。
 * 它的主要职责是：
 * 1. 调用 update_curr_yat_casched 更新该任务的累计执行时间统计。
 * 2. 计算本次执行的持续时间 (delta_exec)。
 * 3. 调用 add_history_record，将本次执行记录（任务PID，CPU，执行时长）添加到该CPU的历史表中。
 *    这个历史记录是后续进行缓存感知调度决策（计算Recency）的关键数据。
 */
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    u64 now = rq_clock_task(rq);
    u64 delta_exec = now - p->se.exec_start;

    update_curr_yat_casched(rq);

    /* --- 关键修复：在这里移除任务 --- */
    // 任务 p 刚刚执行完毕，现在将它从运行队列中正式移除。
    // dequeue_task_yat_casched(rq, p, 0);

    // 更新任务的上次运行CPU，并添加到历史记录
    p->yat_casched.last_cpu = rq->cpu;
    add_history_record(rq->cpu, p, delta_exec);

    /*
     * 触发重新调度，检查队列中是否还有下一个任务。
     * 这是我们之前修复过的，必须保留。
     */
    // resched_curr(rq);
}

/*
 * 任务时钟节拍处理 - 仅做统计，不主动调度
 */
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued)
{
    update_curr_yat_casched(rq);
    // 不主动触发调度请求
}

/*
 * 抢占检查
 */
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    /*
     * 关键修复：如果CPU当前正在运行空闲任务(swapper)，
     * 任何一个新唤醒的任务都必须触发一次重新调度来抢占它。
     * 否则，新任务将被困在运行队列中，永远无法执行，导致系统卡死。
     */
    if (is_idle_task(rq->curr)) {
        resched_curr(rq);
    }
}

/* --- 全局调度逻辑 (v2.2) --- */

// 步骤1: 更新加速表，计算并缓存所有可能的 benefit
// static void update_accelerator_table(void)
// {
//     struct task_struct *p;
//     int i;
//     struct hlist_node *tmp;
//     struct accelerator_entry *entry;
//     int bkt;

//     // 清空旧的加速表
//     spin_lock(&accelerator_lock);
//     hash_for_each_safe(accelerator_table, bkt, tmp, entry, node) {
//         hash_del(&entry->node);
//         kfree(entry);
//     }
//     spin_unlock(&accelerator_lock);

//     // 遍历就绪池中的任务和所有空闲核心，计算并填充新的benefit
//     spin_lock(&yat_pool_lock);
//     list_for_each_entry(p, &yat_ready_job_pool, yat_casched.run_list) {
//         for_each_online_cpu(i) {
//             // 只为当前空闲的核心计算benefit
//             if (cpu_rq(i)->yat_casched.nr_running > 0)
//                 continue;

//             u64 recency = calculate_recency(p, i);
//             u64 crp_ratio = get_crp_ratio(recency);
//             u64 benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;

//             if (benefit > 0) {
//                 entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
//                 if (!entry) continue;
//                 entry->p = p;
//                 entry->cpu = i;
//                 entry->benefit = benefit;
                
//                 spin_lock(&accelerator_lock);
//                 // 使用任务和CPU的地址组合作为唯一的key
//                 hash_add(accelerator_table, &entry->node, (unsigned long)p + i);
//                 spin_unlock(&accelerator_lock);
//             }
//         }
//     }
//     spin_unlock(&yat_pool_lock);
// }


// 步骤2: 根据加速表进行调度决策
// static void schedule_yat_casched_tasks(void)
// {
//     struct accelerator_entry *entry, *best_entry = NULL;
//     struct task_struct *p_to_run = NULL;
//     int best_cpu = -1;
//     u64 max_benefit = 0;
//     u64 min_impact = ULLONG_MAX;
//     int bkt;
    
//     // 1. 更新加速表，缓存所有当前可行的(任务,核心)对的benefit
//     update_accelerator_table();

//     // 2. 第一轮：遍历加速表，找到最大的 benefit 值
//     spin_lock(&accelerator_lock);
//     hash_for_each(accelerator_table, bkt, entry, node) {
//         if (entry->benefit > max_benefit) {
//             max_benefit = entry->benefit;
//         }
//     }

//     // 3. 第二轮：在 benefit 最大的条目中，通过计算 impact 来打破平局
//     hash_for_each(accelerator_table, bkt, entry, node) {
//         if (entry->benefit == max_benefit) {
//             // 只有在此时，才需要计算 Impact
//             u64 current_impact = calculate_impact(entry->p, entry->cpu);
//             if (current_impact < min_impact) {
//                 min_impact = current_impact;
//                 best_entry = entry; // 记录最佳条目
//             }
//         }
//     }
//     spin_unlock(&accelerator_lock);

//     // 4. 如果找到了最佳选择，则执行调度
//     if (best_entry) {
//         p_to_run = best_entry->p;
//         best_cpu = best_entry->cpu;

//         // 从就绪池移除
//         spin_lock(&yat_pool_lock);
//         list_del_init(&p_to_run->yat_casched.run_list);
//         spin_unlock(&yat_pool_lock);

//         // 加入目标CPU的本地运行队列
//         struct rq *target_rq = cpu_rq(best_cpu);
//         struct yat_casched_rq *target_yat_rq = &target_rq->yat_casched;
//         struct rq_flags flags;
        
//         // 此处需要获取目标rq的锁来安全地修改其运行队列
//         rq_lock(target_rq, &flags);
//         list_add_tail(&p_to_run->yat_casched.run_list, &target_yat_rq->tasks);
//         target_yat_rq->nr_running++;
//         rq_unlock(target_rq, &flags);
//         // printk(KERN_INFO "[yat] schedule: PID=%d -> CPU=%d, benefit=%llu, impact=%llu\n", p_to_run->pid, best_cpu, max_benefit, min_impact);
//     }
// }

/*
 * 负载均衡 - 现在不是我们的主调度入口
 */
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    // 在新的模型(v3.0)中，调度决策由 select_task_rq 做出，
    // balance 不再是主调度入口。
    // 可以保留为空，或用于未来的后台优化任务。
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
    /* CPU下线处理 */
}

struct task_struct *pick_task_yat_casched(struct rq *rq)
{
    return pick_next_task_yat_casched(rq);
}

void switched_to_yat_casched(struct rq *rq, struct task_struct *p)
{
    if (p->yat_casched.wcet == 0) {
        p->yat_casched.wcet = get_wcet(p);
    }
}

void prio_changed_yat_casched(struct rq *rq, struct task_struct *p, int oldprio)
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
            // 假设 benefit 单位为 1/10000
            seq_printf(m, "%3d | %5d | %3d | %8llu.%04llu\n",
                count,
                entry->p->pid,
                entry->cpu,
                entry->benefit / 10000,           // 整数部分
                entry->benefit % 10000            // 小数部分
            );
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
    int i, count = 0;
    struct history_record *rec;

    seq_printf(m, "Yat_Casched History Table (L1 Caches):\nCPU | PID | Timestamp | ExecTime\n");
    for_each_possible_cpu(i) {
        spin_lock(&L1_caches[i].lock);
        // --- 修复：使用正确的成员名 time_list_node ---
        list_for_each_entry(rec, &L1_caches[i].time_list, time_list_node) {
            seq_printf(m, "%3d | %5d | %10llu | %8llu\n", i, rec->task_id, rec->timestamp, rec->exec_time);
            count++;
        }
        spin_unlock(&L1_caches[i].lock);
    }

    seq_printf(m, "\nYat_Casched History Table (L2 Caches):\nL2$ | PID | Timestamp | ExecTime\n");
    for (i = 0; i < NR_CPUS/CPU_NUM_PER_SET; i++) {
        spin_lock(&L2_caches[i].lock);
        // --- 修复：使用正确的成员名 time_list_node ---
        list_for_each_entry(rec, &L2_caches[i].time_list, time_list_node) {
            seq_printf(m, "%3d | %5d | %10llu | %8llu\n", i, rec->task_id, rec->timestamp, rec->exec_time);
            count++;
        }
        spin_unlock(&L2_caches[i].lock);
    }

    seq_printf(m, "\nYat_Casched History Table (L3 Cache):\nL3$ | PID | Timestamp | ExecTime\n");
    spin_lock(&L3_cache.lock);
    // --- 修复：使用正确的成员名 time_list_node ---
    list_for_each_entry(rec, &L3_cache.time_list, time_list_node) {
        seq_printf(m, "%3d | %5d | %10llu | %8llu\n", 0, rec->task_id, rec->timestamp, rec->exec_time);
        count++;
    }
    spin_unlock(&L3_cache.lock);

    seq_printf(m, "\nTotal Records: %d\n", count);
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
// static int yat_ready_pool_show(struct seq_file *m, void *v)
// {
//     struct task_struct *p;
//     int count = 0;
//     seq_printf(m, "Yat_Casched Ready Pool:\nIdx | PID | Prio | WCET\n");
//     spin_lock(&yat_pool_lock);
//     list_for_each_entry(p, &yat_ready_job_pool, yat_casched.run_list) {
//         seq_printf(m, "%3d | %5d | %4d | %6llu\n", count, p->pid, p->prio, p->yat_casched.wcet);
//         count++;
//     }
//     spin_unlock(&yat_pool_lock);
//     seq_printf(m, "Total: %d\n", count);
//     return 0;
// }

// static int yat_ready_pool_open(struct inode *inode, struct file *file)
// {
//     return single_open(file, yat_ready_pool_show, NULL);
// }

// static const struct file_operations yat_ready_pool_fops = {
//     .owner = THIS_MODULE,
//     .open = yat_ready_pool_open,
//     .read = seq_read,
//     .llseek = seq_lseek,
//     .release = single_release,
// };

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

    // yat_ready_pool_file = debugfs_create_file("ready_pool", 0444, yat_debugfs_dir, NULL, &yat_ready_pool_fops);
    // if (IS_ERR_OR_NULL(yat_ready_pool_file)) {
    //     printk(KERN_ERR "[yat] debugfs create ready_pool file failed: %ld\n", PTR_ERR(yat_ready_pool_file));
    //     yat_ready_pool_file = NULL;
    // } else {
    //     printk(KERN_INFO "[yat] debugfs ready_pool file created: %p\n", yat_ready_pool_file);
    // }

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