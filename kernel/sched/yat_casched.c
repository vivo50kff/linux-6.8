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

#define CPU_NUM_PER_SET 2  // L2缓存集群的核心数
/* debugfs 导出接口声明，避免隐式声明和 static 冲突 */
static void yat_debugfs_init(void);
static void yat_debugfs_cleanup(void);

/* --- 核心数据结构定义 (v2.2 - 最终版) --- */

// 历史记录: 使用双向链表，按时间戳顺序记录历史
struct history_record {
    struct list_head list;
    int task_id;
    u64 timestamp;
    u64 exec_time;
};

// 历史表: 每个缓存一个，包含一个链表头和锁
struct cache_history_table {
    struct list_head time_list;
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
static LIST_HEAD(yat_ready_job_pool);
static DEFINE_SPINLOCK(yat_pool_lock);

// 全局调度锁，保护 idle_cores 数组和调度决策过程
static DEFINE_SPINLOCK(global_schedule_lock);

// 空闲核心状态数组: 1 表示空闲, 0 表示繁忙
// 初始化时假设所有核心都空闲
static int idle_cores[NR_CPUS] = {[0 ... NR_CPUS-1] = 1};


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
    int i;
    for_each_possible_cpu(i) {
        INIT_LIST_HEAD(&L1_caches[i].time_list);
        spin_lock_init(&L1_caches[i].lock);
    }
    for (i = 0; i < NR_CPUS / CPU_NUM_PER_SET; i++) {
        INIT_LIST_HEAD(&L2_caches[i].time_list);
        spin_lock_init(&L2_caches[i].lock);
    }
    INIT_LIST_HEAD(&L3_cache.time_list);
    spin_lock_init(&L3_cache.lock);
}

// 添加历史记录
static void add_history_record(int cpu, struct task_struct *p, u64 exec_time) {
    struct history_record *rec_l1, *rec_l2, *rec_l3;
    struct history_record *tmp, *pos;
    struct cache_history_table *table_l1, *table_l2, *table_l3;
    int l2_cluster_id = cpu / CPU_NUM_PER_SET; // 简化假设
    u64 now = ktime_get();

    // 先删除L1历史表中同一PID的旧记录
    table_l1 = &L1_caches[cpu];
    spin_lock(&table_l1->lock);
    list_for_each_entry_safe(pos, tmp, &table_l1->time_list, list) {
        if (pos->task_id == p->pid) {
            list_del(&pos->list);
            kfree(pos);
            break;
        }
    }
    spin_unlock(&table_l1->lock);

    // 先删除L2历史表中同一PID的旧记录
    table_l2 = &L2_caches[l2_cluster_id];
    spin_lock(&table_l2->lock);
    list_for_each_entry_safe(pos, tmp, &table_l2->time_list, list) {
        if (pos->task_id == p->pid) {
            list_del(&pos->list);
            kfree(pos);
            break;
        }
    }
    spin_unlock(&table_l2->lock);

    // 先删除L3历史表中同一PID的旧记录
    table_l3 = &L3_cache;
    spin_lock(&table_l3->lock);
    list_for_each_entry_safe(pos, tmp, &table_l3->time_list, list) {
        if (pos->task_id == p->pid) {
            list_del(&pos->list);
            kfree(pos);
            break;
        }
    }
    spin_unlock(&table_l3->lock);
    
    // 为 L1, L2, L3 分别创建记录
    rec_l1 = kmalloc(sizeof(*rec_l1), GFP_ATOMIC);
    rec_l2 = kmalloc(sizeof(*rec_l2), GFP_ATOMIC);
    rec_l3 = kmalloc(sizeof(*rec_l3), GFP_ATOMIC);

    if (!rec_l1 || !rec_l2 || !rec_l3) {
        kfree(rec_l1);
        kfree(rec_l2);
        kfree(rec_l3);
        return;
    }

    // 填充记录 (L1)
    rec_l1->task_id = p->pid;
    rec_l1->timestamp = now;
    rec_l1->exec_time = exec_time;
    
    // 填充记录 (L2)
    *rec_l2 = *rec_l1;

    // 填充记录 (L3)
    *rec_l3 = *rec_l1;

    // 更新任务的 per-cpu 最近执行时间戳
    p->yat_casched.per_cpu_recency[cpu] = now;

    // 添加到 L1 历史表
    table_l1 = &L1_caches[cpu];
    spin_lock(&table_l1->lock);
    list_add_tail(&rec_l1->list, &table_l1->time_list);
    spin_unlock(&table_l1->lock);

    // 添加到 L2 历史表
    table_l2 = &L2_caches[l2_cluster_id];
    spin_lock(&table_l2->lock);
    list_add_tail(&rec_l2->list, &table_l2->time_list);
    spin_unlock(&table_l2->lock);

    // 添加到 L3 历史表
    table_l3 = &L3_cache;
    spin_lock(&table_l3->lock);
    list_add_tail(&rec_l3->list, &table_l3->time_list);
    spin_unlock(&table_l3->lock);
}

static u64 calculate_and_prune_recency(struct cache_history_table *table, struct task_struct *p)
{
    u64 recency = 0;
    struct history_record *rec, *tmp;
    int task_id = p->pid;
    bool found = false;

    spin_lock(&table->lock);
    // 从链表尾部向前安全遍历（因为要删除节点）
    list_for_each_entry_safe_reverse(rec, tmp, &table->time_list, list) {
        if (rec->task_id == task_id) {
            // 找到了上一次的记录，停止累加，并删除该记录
            found = true;
            // list_del(&rec->list);
            // kfree(rec);
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
static u64 calculate_impact(struct task_struct *p, int cpu_id) {
    // 简化实现：计算该决策对所有其他待调度任务的 benefit 损失之和
    u64 total_impact = 0;
    struct task_struct *other_p;

    spin_lock(&yat_pool_lock);
    list_for_each_entry(other_p, &yat_ready_job_pool, yat_casched.run_list) {
        if (other_p == p) continue;

        // 原本 other_p 在 cpu_id 上的 benefit
        u64 old_recency = calculate_recency(other_p, cpu_id);
        u64 old_crp = get_crp_ratio(old_recency);
        u64 old_benefit = ((1000 - old_crp) * other_p->yat_casched.wcet) / 1000;

        // p 占用 cpu_id 后，other_p 的 recency 会增加 p 的执行时间
        u64 new_recency = old_recency + p->yat_casched.wcet;
        u64 new_crp = get_crp_ratio(new_recency);
        u64 new_benefit = ((1000 - new_crp) * other_p->yat_casched.wcet) / 1000;
        
        if (old_benefit > new_benefit) {
            total_impact += (old_benefit - new_benefit);
        }
    }
    spin_unlock(&yat_pool_lock);

    return total_impact;
}


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
        spin_lock_init(&global_schedule_lock);
    }
    // printk(KERN_INFO "======init yat_casched rq for cpu %d======\n", main_rq->cpu);

    INIT_LIST_HEAD(&rq->tasks);
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
    u64 now = ktime_get();
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
static bool has_idle_cores(void)
{
    int i;
    bool has_idle = false;
    // 这个函数必须在 global_schedule_lock 保护下调用
    for_each_online_cpu(i) {
        if (idle_cores[i]) {
            has_idle = true;
            break;
        }
    }
    return has_idle;
}

// 检查任务是否为全局池的队头
static bool is_head_task(struct task_struct *p)
{
    struct task_struct *head;
    bool is_head = false;
    spin_lock(&yat_pool_lock);
    if (!list_empty(&yat_ready_job_pool)) {
        head = list_first_entry(&yat_ready_job_pool, struct task_struct, yat_casched.run_list);
        if (head == p) {
            is_head = true;
        }
    }
    spin_unlock(&yat_pool_lock);
    return is_head;
}

// 为任务寻找最佳CPU（v3.1 - 完整版）
// 通过计算当前每个空闲核心对于任务的benefit选择最佳核心，
// 如果出现两个相同benefit，则采用最小影响法，计算对其他的impact，选择impact最小的核心
static int find_best_cpu_for_task(struct task_struct *p)
{
    int i;
    int best_cpu = -1;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;
    int *candidate_cpus;
    int num_candidates = 0;
    u64 recency, crp_ratio, benefit,impact;

    candidate_cpus = kmalloc_array(NR_CPUS, sizeof(int), GFP_ATOMIC);
    if (!candidate_cpus)
        return -ENOMEM; // 内存分配失败

    // 这个函数必须在 global_schedule_lock 保护下调用
    
    // 第一轮：遍历所有空闲核心，找到最大的 benefit 值，并记录所有达到该值的 CPU
    for_each_online_cpu(i) {
        if (idle_cores[i]) {
            recency = calculate_recency(p, i);
            crp_ratio = get_crp_ratio(recency);
            benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;

            //  新增：插入加速表
            if (benefit >= 0 ) { // 允许插入0或负值的任务(暂时)
                struct accelerator_entry *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
                if (entry) {
                    entry->p = p;
                    entry->cpu = i;
                    entry->benefit = benefit;

                    spin_lock(&accelerator_lock);
                    hash_add(accelerator_table, &entry->node, (unsigned long)p + i);
                    spin_unlock(&accelerator_lock);
                }
            }

            if (benefit > max_benefit) {
                // 发现了新的更高 benefit，重置候选列表
                max_benefit = benefit;
                best_cpu = i; // 记录当前最佳 CPU
                num_candidates = 0;
                candidate_cpus[num_candidates++] = i;
            }
             else if (benefit == max_benefit) {
                // 发现了 benefit 相同的 CPU，加入候选列表
                candidate_cpus[num_candidates++] = i;
            }
        }
    }
    // 相同则随机分配
    // 第二轮：根据候选 CPU 的数量做出决策
    if (num_candidates == 0) {
        // 没有任何 CPU 有 benefit > 0，或者没有空闲 CPU。
        // 尝试返回第一个可用的空闲 CPU 作为后备。
        for_each_online_cpu(i) {
            if (idle_cores[i]) {
                best_cpu = i;
                goto out;
            }
        }
        best_cpu = -1; // 确实没有空闲核心
        goto out;
    }

    if (num_candidates == 1) {
        // 只有一个最佳选择
        best_cpu = candidate_cpus[0];
    } else {
        // 有多个 benefit 相同的 CPU，需要通过 impact 来打破平局
        best_cpu = candidate_cpus[0]; // 默认选择第一个
        for (i = 0; i < num_candidates; i++) {
            int cpu_id = candidate_cpus[i];
            impact = calculate_impact(p, cpu_id);
            if (impact < min_impact) {
                min_impact = impact;
                best_cpu = cpu_id;
            }
        }
    }

out:
    kfree(candidate_cpus);
    return best_cpu;
}


/*
 * select_task_rq - 新的全局调度决策中心 (v3.0)
 * 1. 任务进入后，首先加入全局等待池。
 * 2. 进入自旋等待循环，直到满足“是队头任务”且“有空闲核心”的条件。
 * 3. 满足条件后，获取全局调度锁，并再次检查条件（双重检查）。
 * 4. 寻找最佳CPU，将任务从全局池移到目标CPU的本地运行队列。
 * 5. 更新核心状态并返回目标CPU。
 */
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    int best_cpu = -1;

    // 步骤1: 将任务加入全局池 (如果尚未加入)
    spin_lock(&yat_pool_lock);
    if (list_empty(&p->yat_casched.run_list)) {
        list_add_tail(&p->yat_casched.run_list, &yat_ready_job_pool);
        // printk(KERN_INFO "[yat] select_task_rq: PID=%d enqueued into global pool\n", p->pid);
    }
    spin_unlock(&yat_pool_lock);

    // 步骤2: 自旋等待，直到成为队头且有空闲核心
    while (true) {
        // 检查条件，如果不满足，则短暂让出CPU再试，避免纯粹的忙等
        if (!is_head_task(p) || !has_idle_cores()) {
            cpu_relax(); // or schedule_timeout_uninterruptible(1);
            continue;
        }

        // 步骤3: 尝试获取全局调度锁
        spin_lock(&global_schedule_lock);

        // 步骤4: 双重检查，防止在获取锁的过程中状态变化
        if (is_head_task(p) && has_idle_cores()) {
            best_cpu = find_best_cpu_for_task(p);
            if (best_cpu != -1) {
                // 标记核心为繁忙
                idle_cores[best_cpu] = 0;
                break; // 成功找到CPU，跳出循环
            }
        }
        
        // 如果条件不满足或未找到CPU，释放锁并继续自旋
        spin_unlock(&global_schedule_lock);
    }

    // 步骤5: 执行调度
    // 从全局池中移除
    spin_lock(&yat_pool_lock);
    list_del_init(&p->yat_casched.run_list);
    spin_unlock(&yat_pool_lock);
    
    spin_unlock(&global_schedule_lock); // 释放全局锁
    // 将任务正式放到目标CPU的本地运行队列
    // 注意：这里不再需要获取 target_rq 的锁，因为这是在 select_task_rq 上下文中，
    // 内核框架会处理后续的入队操作。我们只需返回目标CPU即可。
    // printk(KERN_INFO "[yat] select_task_rq: PID=%d selected for CPU=%d\n", p->pid, best_cpu);
    return best_cpu;
}

/*
 * 将任务加入本地运行队列 (v3.0)
 * 这个函数现在由内核在 select_task_rq 之后调用。
 * 它的职责是把任务真正放入目标CPU的本地队列。
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    // 加入本地运行队列
    list_add_tail(&p->yat_casched.run_list, &yat_rq->tasks);
    yat_rq->nr_running++;
    
    // printk(KERN_INFO "[yat] enqueue_task: PID=%d enqueued on CPU=%d, nr_running=%d\n", p->pid, rq->cpu, yat_rq->nr_running);
}

/*
 * 将任务从本地运行队列移除 (v3.0)
 * 任务执行完毕或被抢占时调用。
 * 关键职责：释放其占用的核心，将其标记为空闲，并触发一次调度检查。
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;

    // 从本地队列移除
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        yat_rq->nr_running--;
        // printk(KERN_INFO "[yat] dequeue_task: PID=%d dequeued from CPU=%d, nr_running=%d\n", p->pid, rq->cpu, yat_rq->nr_running);
    }

    // 如果此核心变为空闲，则更新全局状态
    if (yat_rq->nr_running == 0) {
        spin_lock(&global_schedule_lock);
        idle_cores[rq->cpu] = 1; // 标记为空闲
        // printk(KERN_INFO "[yat] dequeue_task: CPU=%d is now idle. Kicking scheduler.\n", rq->cpu);
        spin_unlock(&global_schedule_lock);
        
        // 唤醒一个在 select_task_rq 中等待的任务
        // 注意：这里不需要显式唤醒，因为等待的任务在自旋。
        // 状态的改变会让他们在下一次循环中通过检查。
    }
}

/*
 * pick_next_task - 从本地运行队列取任务
 * 因为我们保证每个核心最多一个任务，所以逻辑很简单
 */
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;

    // printk(KERN_INFO "[yat] pick_next_task_yat_casched called on CPU %d, nr_running=%d\n", rq->cpu, yat_rq->nr_running);

    if (yat_rq->nr_running == 0) {
        // printk(KERN_INFO "[yat] pick_next_task_yat_casched: no runnable task on CPU %d\n", rq->cpu);
        return NULL;
    }

    struct task_struct *next = list_first_entry_or_null(&yat_rq->tasks, struct task_struct, yat_casched.run_list);
    // if (next)
    //     // printk(KERN_INFO "[yat] pick_next_task_yat_casched: picked PID=%d on CPU %d\n", next->pid, rq->cpu);
    // else
    //     // printk(KERN_INFO "[yat] pick_next_task_yat_casched: list_first_entry_or_null returned NULL on CPU %d\n", rq->cpu);

    return next;
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
    p->se.exec_start = ktime_get();
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
    u64 now = ktime_get();
    u64 delta_exec = now - p->se.exec_start;

    /* --- 新增逻辑：更新历史表 --- */
    add_history_record(rq->cpu, p, delta_exec);

    update_curr_yat_casched(rq);

    // 任务放回时，如果它不再运行（例如，它已完成），
    // 我们需要确保它已从本地运行队列中正确移除。
    // dequeue_task_yat_casched 已经处理了大部分逻辑。
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
     * 在我们的模型中，任务唤醒时不应该抢占当前正在运行的任务。
     * 唤醒的任务会通过 select_task_rq_yat_casched 进入全局等待队列，
     * 等待调度器在有核心空闲时进行调度。
     * 因此，此函数不执行任何操作。
     */
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
//         resched_curr(target_rq);
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

void switched_to_yat_casched(struct rq *rq, struct task_struct *task)
{
    /* 任务切换到Yat_Casched调度类时的处理 */
    // 初始化任务的WCET
    if (task->yat_casched.wcet == 0) {
        task->yat_casched.wcet = get_wcet(task);
        // printk(KERN_INFO "[yat] switched_to: Initialized WCET for PID=%d\n", task->pid);
    }
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
        list_for_each_entry(rec, &L1_caches[i].time_list, list) {
            seq_printf(m, "%3d | %5d | %10llu | %8llu\n", i, rec->task_id, rec->timestamp, rec->exec_time);
            count++;
        }
        spin_unlock(&L1_caches[i].lock);
    }

    seq_printf(m, "\nYat_Casched History Table (L2 Caches):\nL2$ | PID | Timestamp | ExecTime\n");
    for (i = 0; i < NR_CPUS/CPU_NUM_PER_SET; i++) {
        spin_lock(&L2_caches[i].lock);
        list_for_each_entry(rec, &L2_caches[i].time_list, list) {
            seq_printf(m, "%3d | %5d | %10llu | %8llu\n", i, rec->task_id, rec->timestamp, rec->exec_time);
            count++;
        }
        spin_unlock(&L2_caches[i].lock);
    }

    seq_printf(m, "\nYat_Casched History Table (L3 Cache):\nL3$ | PID | Timestamp | ExecTime\n");
    spin_lock(&L3_cache.lock);
    list_for_each_entry(rec, &L3_cache.time_list, list) {
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