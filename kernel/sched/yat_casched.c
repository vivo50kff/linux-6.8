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

/* --- 全局数据结构 --- */

// 全局任务池，用于存放所有待调度的Yat_Casched任务
static LIST_HEAD(yat_global_task_pool);
static DEFINE_SPINLOCK(yat_pool_lock);

// 全局哈希加速表
#define ACCELERATOR_BITS 10
static DEFINE_HASHTABLE(accelerator_table, ACCELERATOR_BITS);
static DEFINE_SPINLOCK(accelerator_lock);

// 哈希表中的条目
struct accelerator_entry {
    struct task_struct *p;
    int cpu;
    u64 benefit;
    struct hlist_node node; // 哈希链表节点
};

/*
 * 模拟CRP模型函数
 * 输入: recency (ns) - 任务上次运行距今的时间
 * 输出: crp_ratio (0-1000) - 执行时间占WCET的千分比
 */
static u64 get_crp_ratio(u64 recency)
{
    // 这是一个简化的分段函数，模拟CRP曲线
    if (recency < 10000) // 10us以内，极热
        return 600; // 实际执行时间是WCET的60%
    if (recency < 100000) // 100us以内，温
        return 800; // 实际执行时间是WCET的80%
    
    return 1000; // 否则视为完全冷，执行时间=WCET
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
    int i;
    
    printk(KERN_INFO "======init yat_casched rq======\n");
    
    INIT_LIST_HEAD(&rq->tasks);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = jiffies;
    spin_lock_init(&rq->history_lock);
    
    /* 初始化CPU历史表 */
    for (i = 0; i < NR_CPUS; i++) {
        rq->cpu_history[i] = NULL;
    }
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
    
    // 寻找正确的插入位置（按prio和wcet排序）
    list_for_each(pos, &yat_global_task_pool) {
        entry = list_entry(pos, struct task_struct, yat_casched.run_list);
        if (p->prio < entry->prio || 
            (p->prio == entry->prio && p->yat_casched.wcet > entry->yat_casched.wcet)) {
            break;
        }
    }
    list_add_tail(&p->yat_casched.run_list, pos);

    spin_unlock(&yat_pool_lock);
}

/*
 * 将任务从全局任务池移除
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    spin_lock(&yat_pool_lock);
    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
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
    update_curr_yat_casched(rq);
    
    /* --- 新增逻辑：更新recency时间戳 --- */
    p->yat_casched.per_cpu_recency[rq->cpu] = ktime_get();
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

/* --- 全局调度逻辑 --- */

static void update_accelerator_table(void)
{
    struct task_struct *p;
    int i;
    struct hlist_node *tmp;
    struct accelerator_entry *entry;
    int bkt;

    spin_lock(&accelerator_lock);
    // 清空哈希表，释放内存
    hash_for_each_safe(accelerator_table, bkt, tmp, entry, node) {
        hash_del(&entry->node);
        kfree(entry);
    }

    spin_lock(&yat_pool_lock);
    list_for_each_entry(p, &yat_global_task_pool, yat_casched.run_list) {
        for_each_cpu(i, p->cpus_ptr) {
            if (!cpu_online(i) || cpu_rq(i)->yat_casched.nr_running > 0)
                continue;

            u64 recency = ktime_get() - p->yat_casched.per_cpu_recency[i];
            u64 crp_ratio = get_crp_ratio(recency);
            u64 benefit = ((1000 - crp_ratio) * p->yat_casched.wcet) / 1000;

            if (benefit > 0) {
                entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
                if (!entry) continue;
                entry->p = p;
                entry->cpu = i;
                entry->benefit = benefit;
                hash_add(accelerator_table, &entry->node, (unsigned long)p + i);
            }
        }
    }
    spin_unlock(&yat_pool_lock);
    spin_unlock(&accelerator_lock);
}

static void schedule_yat_casched_tasks(void)
{
    struct task_struct *p_to_run = NULL;
    int best_cpu = -1;
    u64 max_benefit = 0;
    struct accelerator_entry *entry;
    int bkt;

    update_accelerator_table();

    spin_lock(&accelerator_lock);
    hash_for_each(accelerator_table, bkt, entry, node) {
        if (entry->benefit > max_benefit) {
            max_benefit = entry->benefit;
            p_to_run = entry->p;
            best_cpu = entry->cpu;
        }
    }
    spin_unlock(&accelerator_lock);

    if (p_to_run) {
        // 从全局池移除
        spin_lock(&yat_pool_lock);
        list_del_init(&p_to_run->yat_casched.run_list);
        spin_unlock(&yat_pool_lock);

        // 加入目标CPU的本地运行队列
        struct rq *target_rq = cpu_rq(best_cpu);
        struct yat_casched_rq *target_yat_rq = &target_rq->yat_casched;
        list_add_tail(&p_to_run->yat_casched.run_list, &target_yat_rq->tasks);
        target_yat_rq->nr_running++;
        
        // 唤醒目标CPU
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
