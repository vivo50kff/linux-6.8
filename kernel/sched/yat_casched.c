#include <linux/debugfs.h>
#include <linux/seq_file.h>


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
    int task_id;
    u64 timestamp;
    u64 exec_time;
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
static void add_history_record(int cpu, struct task_struct *p, u64 exec_time) {
    struct history_record *rec;
    struct cpu_history_table *table = &history_tables[cpu];
    
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec) return;

    rec->task_id = p->pid;
    rec->timestamp = ktime_get();
    rec->exec_time = exec_time;

    // 更新任务的 per-cpu 最近执行时间戳
    p->yat_casched.per_cpu_recency[cpu] = rec->timestamp;

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


/* --- Recency 和 Impact 计算 (v2.1) --- */

// 计算多层缓存 Recency
static u64 calculate_recency(struct task_struct *p, int cpu_id) {
    u64 now = ktime_get();
    u64 l1_sum, l2_sum, l3_sum;

    // L1 (私有)：统计任务i两次命中核心J之间，核心J上所有任务的执行时间总和
    u64 l1_last = p->yat_casched.per_cpu_recency[cpu_id];
    if (l1_last == 0) {
        // 首次未命中，用任务创建时间戳
        l1_last = p->start_time;
    }
    l1_sum = sum_exec_time_since(cpu_id, l1_last, now);

    // L2 (共享)：统计任务i两次命中L2缓存之间，J及与J共享L2的所有核心上的任务执行时间总和
    const struct cpumask *l2_cpus = cpu_l2c_shared_mask(cpu_id);
    u64 l2_last = get_max_last_exec_time(p, l2_cpus);
    if (l2_last == 0) {
        l2_last = p->start_time;
    }
    l2_sum = sum_exec_time_multi(l2_cpus, l2_last, now);

    // L3 (全局)：统计任务i两次命中L3缓存之间，J及与J共享L3的所有核心上的任务执行时间总和
    const struct cpumask *l3_cpus = cpu_llc_shared_mask(cpu_id);
    u64 l3_last = get_max_last_exec_time(p, l3_cpus);
    if (l3_last == 0) {
        l3_last = p->start_time;
    }
    l3_sum = sum_exec_time_multi(l3_cpus, l3_last, now);

    return l1_sum + l2_sum + l3_sum;
}

// 计算将任务 p 放到 cpu_id 上对其他任务的 Impact
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
    // 初步实现：给所有任务一个固定的WCET，例如10ms
    return 10 * 1000 * 1000; // 10ms in ns
}

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / HZ)

/*
 * 初始化Yat_Casched运行队列
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    struct rq *main_rq = container_of(rq, struct rq, yat_casched);
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
    }

    spin_lock(&yat_pool_lock);
    
    // 插入就绪池，按优先级排序 (值越小，优先级越高)
    list_for_each(pos, &yat_ready_job_pool) {
        entry = list_entry(pos, struct task_struct, yat_casched.run_list);
        if (p->prio < entry->prio) {
            break;
        }
    }
    list_add_tail(&p->yat_casched.run_list, pos);

    spin_unlock(&yat_pool_lock);
    printk(KERN_INFO "[yat] enqueue_task: PID=%d\n", p->pid);
}

/*
 * 将任务从就绪池移除
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    spin_lock(&yat_pool_lock);
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        printk(KERN_INFO "[yat] dequeue_task: PID=%d\n", p->pid);
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
    
    if (yat_rq->nr_running == 0)
        return NULL;
    
    // 直接返回队列中的唯一任务
    return list_first_entry_or_null(&yat_rq->tasks, struct task_struct, yat_casched.run_list);
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
    update_curr_yat_casched(rq);
    
    delta_exec = rq_clock_task(rq) - p->se.exec_start;
    
    /* --- 新增逻辑：更新历史表 --- */
    add_history_record(rq->cpu, p, delta_exec);
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
        rq_lock(target_rq, &flags);
        list_add_tail(&p_to_run->yat_casched.run_list, &target_yat_rq->tasks);
        target_yat_rq->nr_running++;
        rq_unlock(target_rq, &flags);
        printk(KERN_INFO "[yat] schedule: PID=%d -> CPU=%d, benefit=%llu, impact=%llu\n", p_to_run->pid, best_cpu, max_benefit, min_impact);
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
    /* CPU下线处理 */
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
