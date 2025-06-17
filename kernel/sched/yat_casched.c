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
 * 缓存感知的CPU选择算法
 * 优先选择任务上次运行的CPU，如果不可用则选择负载最轻的CPU
 */
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    struct yat_casched_rq *yat_rq;
    int last_cpu = p->yat_casched.last_cpu;
    int best_cpu = task_cpu;
    int cpu;
    unsigned long min_load = ULONG_MAX;
    
    /* 如果是第一次运行，记录为无效CPU */
    if (last_cpu == -1) {
        p->yat_casched.last_cpu = task_cpu;
        return task_cpu;
    }
    
    /* 检查上次运行的CPU是否可用 */
    if (cpu_online(last_cpu) && cpumask_test_cpu(last_cpu, &p->cpus_mask)) {
        return last_cpu;  /* 优先选择上次的CPU */
    }
    
    /* 缓存已冷或上次CPU不可用，选择负载最轻的CPU */
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
        p->yat_casched.last_cpu = 0;  /* 使用0而不是-1 */
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
        /* 更新CPU历史表 - 添加边界检查 */
        if (rq->cpu >= 0 && rq->cpu < NR_CPUS) {
            spin_lock(&yat_rq->history_lock);
            yat_rq->cpu_history[rq->cpu] = p;
            spin_unlock(&yat_rq->history_lock);
            
            /* 记录当前CPU为任务的最后运行CPU */
            p->yat_casched.last_cpu = rq->cpu;
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
