# Yat_Casched 调度器开发文档

## 概述
Yat_Casched是一个缓存感知的调度器，专门为改善多核系统中的缓存局部性而设计。

## 核心特性

### 1. 缓存感知调度
- **缓存历史表**：为每个缓存维护一个历史表，记录最后在该缓存上执行的任务
- **缓存亲和性**：优先将任务调度到其之前执行过的缓存上，提高缓存命中率
- **衰减机制**：随时间衰减缓存亲和性，避免负载不均衡

### 2. 数据结构设计

#### sched_yat_casched_entity
```c
struct sched_yat_casched_entity {
    struct list_head run_list;      /* 运行队列链表节点 */
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 slice;                      /* 时间片 */
    u64 per_cpu_recency[NR_CPUS];   /* 每CPU最近度 */
    u64 wcet;                       /* 最坏情况执行时间 */
    int last_cpu;                   /* 上次运行的CPU */
};
```

#### cache_history
```c
struct cache_history {
    struct task_struct *last_task;  /* 该缓存上最后执行的任务 */
    u64 last_time;                  /* 最后执行时间 */
};
```

#### yat_casched_rq
```c
struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    unsigned int nr_running;        /* 运行任务数量 */
    struct task_struct *agent;     /* 代理任务 */
    u64 cache_decay_jiffies;       /* 缓存衰减时间 */
    spinlock_t history_lock;       /* 历史表锁 */
    struct cache_history *cache_histories; /* 缓存历史表数组 */
    int nr_caches;                 /* 缓存数量 */
};
```

### 3. 关键算法

#### 缓存感知CPU选择
1. 遍历所有可用CPU
2. 计算每个CPU的缓存感知权重
3. 选择权重最高的CPU

#### 权重计算公式
```
weight = base_weight + cache_affinity_bonus + temporal_bonus
```
- base_weight: 基础权重 (1000)
- cache_affinity_bonus: 缓存亲和性奖励 (500)
- temporal_bonus: 基于时间的奖励 (随时间衰减)

#### 缓存历史更新
- 在任务切换时更新对应缓存的历史记录
- 使用spinlock保护历史表的并发访问

### 4. 负载均衡
- 当本地队列为空时，从其他CPU拉取任务
- 迁移时考虑缓存亲和性，优先迁移缓存亲和性较低的任务

## 实现细节

### 初始化
```c
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    // 初始化任务队列
    INIT_LIST_HEAD(&rq->tasks);
    
    // 分配缓存历史表
    rq->nr_caches = get_nr_caches();
    rq->cache_histories = kzalloc(rq->nr_caches * sizeof(struct cache_history), GFP_KERNEL);
}
```

### 任务调度
1. **入队**: 将任务加入运行队列
2. **选择**: 基于vruntime选择下一个任务
3. **切换**: 更新缓存历史，设置下一个任务

### 缓存拓扑检测
- 简化实现：假设每4个CPU共享一个L2缓存
- 生产环境中应该通过硬件信息动态检测

## 性能优化

### 1. 锁优化
- 历史表使用细粒度锁
- 避免长时间持有锁

### 2. 缓存友好
- 数据结构对齐到缓存行
- 减少false sharing

### 3. 时间复杂度
- CPU选择: O(n) 其中n是可用CPU数
- 任务选择: O(1) 链表操作

## 配置参数
- cache_decay_jiffies: 缓存衰减时间 (默认100ms)
- 可通过/proc/sys/kernel/sched_yat_casched/进行调整

## 测试验证
- 使用缓存敏感的benchmark测试
- 监控缓存命中率和任务迁移频率
- 验证负载均衡效果
