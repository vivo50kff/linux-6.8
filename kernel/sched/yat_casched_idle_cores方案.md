# ## 1. 方案概述
本设计采用全局就绪池和全局空闲核心数组（idle_cores[8]）实现多核Cache-Aware调度。以select_task_rq_yat_casched为主调度入口，调度流程如下：

**第一步：进程入队**
1. 新建进程通过select_task_rq_yat_casched入口，加入全局就绪池（yat_ready_job_pool）。

**第二步：调度决策**
2. 当任务成为队头，且存在"空闲核心"时，开始调度流程：
   - "空闲核心"定义：该核心的yat_casched_rq->nr_running == 0（非系统级idle）
   - 全局锁保护整个决策过程，从计算最优核心到更新空闲核心数组
   - 遍历所有空闲核心，计算每个任务-核心对的benefit/impact
   - 选出最优核心，更新idle_cores[8]数组状态

**第三步：任务分配**
3. 进程通过最优核心找到对应rq，调用enqueue_task_yat_casched加入本地队列。ed调度器全局空闲核心方案设计

## 1. 方案概述
本设计采用全局就绪池和全局空闲核心数组（idle_cores[8]）实现多核Cache-Aware调度。调度流程如下：

1. 新建进程加入全局就绪池（yat_ready_job_pool）。
2. 判断是否有“空闲核心”，即idle_cores[i]为1（不是系统级idle，而是调度类本地队列为空）。
3. 若有空闲核心，进入调度决策流程：
   - 全局锁保护，遍历所有空闲核心，计算每个任务-核心对的benefit/impact。
   - 选出最优核心，更新idle_cores数组。
4. 进程通过enqueue_task_yat_casched加入目标核心的本地队列。

## 2. 关键数据结构
- yat_ready_job_pool：全局就绪池，存放所有待调度任务。
- idle_cores[8]：全局数组，记录8个核心是否空闲（1=空闲，0=非空闲）。
- accelerator_table：加速表，缓存所有可行任务-核心对的benefit。

## 3. 主要流程

### 3.1 主调度入口（select_task_rq_yat_casched）
- 作为唯一调度入口，处理新建进程的调度决策。
- 两步流程：进程入队 → 调度决策与分配。

### 3.2 第一步：进程入队
- 新建进程加入全局就绪池（yat_ready_job_pool）。
- 按优先级有序插入队列。

### 3.3 第二步：调度决策触发条件
- 任务成为队头（优先级最高或等待时间最长）。
- 存在"空闲核心"：yat_casched_rq->nr_running == 0的核心。
- 注意：非系统级idle，而是调度类本地队列为空。

### 3.4 第三步：全局锁保护下的调度决策
- 获取全局锁，保护整个决策过程。
- 遍历所有空闲核心，计算任务-核心对的benefit/impact。
- 选出最优核心，更新idle_cores[8]数组状态。
- 释放全局锁。

### 3.5 第四步：任务分配与入队
- 通过最优核心ID找到对应的rq。
- 调用enqueue_task_yat_casched将进程加入目标核心本地队列。
- 更新目标核心的nr_running计数。

## 4. 代码片段参考

```c
// select_task_rq_yat_casched主调度入口
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    // 第一步：加入全局就绪池
    spin_lock(&yat_pool_lock);
    // 按优先级有序插入yat_ready_job_pool
    // ...
    spin_unlock(&yat_pool_lock);
    
    // 第二步：检查是否为队头且有空闲核心
    if (is_head_task(p) && has_idle_cores()) {
        // 第三步：全局锁保护下的调度决策
        spin_lock(&global_schedule_lock);
        int best_cpu = find_best_cpu_for_task(p);
        idle_cores[best_cpu] = 0; // 更新空闲核心状态
        spin_unlock(&global_schedule_lock);
        
        // 第四步：任务分配
        struct rq *target_rq = cpu_rq(best_cpu);
        enqueue_task_yat_casched(target_rq, p, flags);
        return best_cpu;
    }
    
    return task_cpu; // 暂不调度，返回原CPU
}

// 空闲核心判定
extern int idle_cores[8];
bool has_idle_cores(void) {
    for (int i = 0; i < 8; i++) {
        if (cpu_rq(i)->yat_casched.nr_running == 0) {
            idle_cores[i] = 1;
            return true;
        }
    }
    return false;
}
```

## 5. 关键设计要点

### 5.1 调度触发时机
- 队头任务：确保高优先级任务优先调度。
- 空闲核心存在：避免抢占正在运行的任务。
- 空闲核心定义：yat_casched_rq->nr_running == 0，而非系统级CPU idle。

### 5.2 全局锁保护范围
- 从benefit/impact计算开始，到idle_cores数组更新结束。
- 保证调度决策的原子性和一致性。
- 锁粒度需平衡性能与正确性。

### 5.3 数据结构同步
- idle_cores[8]数组需与实际核心状态实时同步。
- 任务分配后立即更新目标核心的nr_running。
- 任务完成后及时释放核心，恢复空闲状态。

## 6. 优缺点分析
**优点：**
- 全局调度决策，Cache-Aware，理论上全局最优。
- 空闲核心判定灵活，避免与系统级idle冲突。

**缺点：**
- 全局锁可能成为多核瓶颈。
- idle_cores一致性和原子性需重点关注。
- 代码维护复杂度提升。

## 7. 适用场景
- 适合实验性、研究型Cache-Aware调度。
- 生产环境需评估锁竞争和多核扩展性。

---
如需进一步细化，请补充idle_cores的具体更新逻辑和多核同步方案。
