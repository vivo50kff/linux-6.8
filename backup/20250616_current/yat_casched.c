#include "sched.h"
#include <linux/hash.h>
#include <linux/spinlock.h>

// 缓存历史表 - 记录任务上次运行的CPU
#define YAT_CACHE_HISTORY_SIZE 256
struct yat_cache_entry {
    pid_t pid;
    int last_cpu;
    u64 last_time;
    struct hlist_node hnode;
};

static struct hlist_head yat_cache_history[YAT_CACHE_HISTORY_SIZE];
static DEFINE_SPINLOCK(yat_cache_lock);

// 初始化缓存历史表
static void __init init_yat_cache_history(void) {
    int i;
    for (i = 0; i < YAT_CACHE_HISTORY_SIZE; i++) {
        INIT_HLIST_HEAD(&yat_cache_history[i]);
    }
}

// 查找任务的缓存历史
static int yat_get_last_cpu(pid_t pid) {
    struct yat_cache_entry *entry;
    int hash_val = hash_32(pid, 8) % YAT_CACHE_HISTORY_SIZE;
    int last_cpu = -1;
    
    spin_lock(&yat_cache_lock);
    hlist_for_each_entry(entry, &yat_cache_history[hash_val], hnode) {
        if (entry->pid == pid) {
            last_cpu = entry->last_cpu;
            entry->last_time = jiffies;  // 更新访问时间
            break;
        }
    }
    spin_unlock(&yat_cache_lock);
    
    return last_cpu;
}

// 更新任务的缓存历史
static void yat_update_cache_history(pid_t pid, int cpu) {
    struct yat_cache_entry *entry, *new_entry = NULL;
    int hash_val = hash_32(pid, 8) % YAT_CACHE_HISTORY_SIZE;
    bool found = false;
    
    spin_lock(&yat_cache_lock);
    
    // 查找现有条目
    hlist_for_each_entry(entry, &yat_cache_history[hash_val], hnode) {
        if (entry->pid == pid) {
            entry->last_cpu = cpu;
            entry->last_time = jiffies;
            found = true;
            break;
        }
    }
    
    // 如果没找到，创建新条目
    if (!found) {
        new_entry = kmalloc(sizeof(struct yat_cache_entry), GFP_ATOMIC);
        if (new_entry) {
            new_entry->pid = pid;
            new_entry->last_cpu = cpu;
            new_entry->last_time = jiffies;
            hlist_add_head(&new_entry->hnode, &yat_cache_history[hash_val]);
        }
    }
    
    spin_unlock(&yat_cache_lock);
}

// 核心函数实现
bool yat_casched_prio(struct task_struct *p) { 
    return p->policy == SCHED_YAT_CASCHED; // 判断是否为yat_casched策略
}

void init_yat_casched_rq(struct yat_casched_rq *rq) {
    if (rq) {
        rq->agent = NULL;
        INIT_LIST_HEAD(&rq->tasks);
        rq->nr_running = 0;
    }
    // 第一次调用时初始化缓存历史表
    static bool initialized = false;
    if (!initialized) {
        init_yat_cache_history();
        initialized = true;
        printk(KERN_INFO "======init yat_casched cache history table======\n");
    }
}

// 调度类接口函数实现
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags) {
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    // 将任务添加到运行队列
    list_add_tail(&p->yat_casched.run_list, &yat_rq->tasks);
    yat_rq->nr_running++;
    
    printk(KERN_DEBUG "YAT_CASCHED: enqueue task PID=%d on CPU=%d, nr_running=%u\n", 
           p->pid, cpu_of(rq), yat_rq->nr_running);
}

void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags) {
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    
    // 从运行队列移除任务
    list_del_init(&p->yat_casched.run_list);
    yat_rq->nr_running--;
    
    printk(KERN_DEBUG "YAT_CASCHED: dequeue task PID=%d from CPU=%d, nr_running=%u\n", 
           p->pid, cpu_of(rq), yat_rq->nr_running);
}

void yield_task_yat_casched(struct rq *rq) {
    // 让出CPU，重新入队
    struct task_struct *curr = rq->curr;
    if (curr && yat_casched_prio(curr)) {
        dequeue_task_yat_casched(rq, curr, 0);
        enqueue_task_yat_casched(rq, curr, 0);
    }
}

bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p) { 
    return false; // 简单实现：不支持yield_to
}

void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags) {
    // 简单实现：不抢占当前任务
}

struct task_struct *pick_next_task_yat_casched(struct rq *rq) {
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct task_struct *next = NULL;
    
    if (yat_rq->nr_running == 0)
        return NULL;
    
    // 选择队列中的第一个任务
    if (!list_empty(&yat_rq->tasks)) {
        next = list_first_entry(&yat_rq->tasks, struct task_struct, yat_casched.run_list);
        
        printk(KERN_DEBUG "YAT_CASCHED: pick_next_task PID=%d on CPU=%d\n", 
               next->pid, cpu_of(rq));
               
        // 更新缓存历史
        yat_update_cache_history(next->pid, cpu_of(rq));
        
        printk(KERN_INFO "\n\n======select yat_casched task PID=%d on CPU=%d======\n\n", 
               next->pid, cpu_of(rq));
    }
    
    return next;
}

void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p) {
    // 当前任务被切换出去时调用
    printk(KERN_DEBUG "YAT_CASCHED: put_prev_task PID=%d on CPU=%d\n", 
           p->pid, cpu_of(rq));
}

void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first) {
    // 设置下一个要运行的任务
    printk(KERN_DEBUG "YAT_CASCHED: set_next_task PID=%d on CPU=%d\n", 
           p->pid, cpu_of(rq));
}

int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf) { 
    return 0; // 简单实现：不进行负载均衡
}

int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags) {
    int best_cpu = task_cpu;
    int last_cpu;
    
    // 查找任务的缓存历史
    last_cpu = yat_get_last_cpu(p->pid);
    
    if (last_cpu >= 0 && last_cpu < nr_cpu_ids && cpu_online(last_cpu) && 
        cpumask_test_cpu(last_cpu, p->cpus_ptr)) {
        // 如果上次运行的CPU仍然可用，优先选择它以利用缓存局部性
        best_cpu = last_cpu;
        printk(KERN_DEBUG "YAT_CASCHED: select_task_rq PID=%d, using cached CPU=%d\n", 
               p->pid, best_cpu);
    } else {
        // 如果是第一次或缓存的CPU不可用，使用当前CPU
        printk(KERN_DEBUG "YAT_CASCHED: select_task_rq PID=%d, first time or cache miss, using CPU=%d\n", 
               p->pid, best_cpu);
    }
    
    return best_cpu;
}

void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx) {
    // CPU亲和性设置
}

void rq_online_yat_casched(struct rq *rq) {
    // CPU上线时调用
}

void rq_offline_yat_casched(struct rq *rq) {
    // CPU下线时调用
}

struct task_struct *pick_task_yat_casched(struct rq *rq) { 
    return pick_next_task_yat_casched(rq);
}

void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued) {
    // 时间片处理
    // 简单实现：每次tick都让出CPU，实现时间片轮转
    if (queued > 0) {
        resched_curr(rq);
    }
}

void switched_to_yat_casched(struct rq *this_rq, struct task_struct *task) {
    // 任务切换到yat_casched策略时调用
    printk(KERN_INFO "YAT_CASCHED: task PID=%d switched to yat_casched policy\n", task->pid);
}

void prio_changed_yat_casched(struct rq *this_rq, struct task_struct *task, int oldprio) {
    // 优先级改变时调用
}

void update_curr_yat_casched(struct rq *rq) {
    // 更新当前任务的运行时间
}

// 调度类定义
DEFINE_SCHED_CLASS(yat_casched) = {
    .enqueue_task       = enqueue_task_yat_casched,
    .dequeue_task       = dequeue_task_yat_casched,
    .yield_task         = yield_task_yat_casched,
    .yield_to_task      = yield_to_task_yat_casched,
    .wakeup_preempt     = wakeup_preempt_yat_casched,
    .pick_next_task     = pick_next_task_yat_casched,
    .put_prev_task      = put_prev_task_yat_casched,
    .set_next_task      = set_next_task_yat_casched,
#ifdef CONFIG_SMP
    .balance            = balance_yat_casched,
    .select_task_rq     = select_task_rq_yat_casched,
    .set_cpus_allowed   = set_cpus_allowed_yat_casched,
    .rq_online          = rq_online_yat_casched,
    .rq_offline         = rq_offline_yat_casched,
#endif
#ifdef CONFIG_SCHED_CORE
    .pick_task          = pick_task_yat_casched,
#endif
    .task_tick          = task_tick_yat_casched,
    .switched_to        = switched_to_yat_casched,
    .prio_changed       = prio_changed_yat_casched,
    .update_curr        = update_curr_yat_casched,
#ifdef CONFIG_UCLAMP_TASK
    .uclamp_enabled     = 0,
#endif
};


