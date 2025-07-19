# YAT_CASCHED 简化版本设计说明

## 简化目标

将复杂的DAG和Job结构体合并到调度实体中，消除双向关联的复杂性，简化代码逻辑，同时保留所有必要的调度信息。

## 主要变更

### 1. 结构体简化

#### 原始版本 vs 简化版本

**原始版本**:
- `struct yat_job` - 复杂的作业结构
- `struct yat_dag_task` - DAG任务结构
- `struct sched_yat_casched_entity` - 调度实体（依赖于yat_job）
- 双向关联：`entity->job` 和 `job->task`

**简化版本**:
```c
struct sched_yat_casched_entity {
    struct list_head run_list;      /* CPU本地队列 */
    struct list_head ready_list;    /* 全局就绪队列 */
    
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 wcet;                       /* 最坏情况执行时间 */
    int priority;                   /* 统一的优先级 */
    int job_id;                     /* 作业ID */
    enum yat_job_state state;       /* 作业状态 */
    u64 per_cpu_last_ts[NR_CPUS];   /* 每个CPU的最后时间戳 */
    int last_cpu;                   /* 上次运行的CPU */
    
    /* 保留CFS兼容性 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    u64 per_cpu_recency[NR_CPUS];   /* 每个CPU的recency值 */
};
```

### 2. 运行队列简化

**原始版本**:
```c
struct yat_casched_rq {
    struct yat_ready_job_pool *ready_pool;      /* 就绪作业池 */
    struct yat_global_task_pool *global_pool;   /* 全局优先队列 */
    struct list_head active_dags;               /* 活跃DAG列表 */
    // ... 其他复杂结构
};
```

**简化版本**:
```c
struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    struct list_head ready_tasks;   /* 全局就绪任务列表 */
    unsigned int nr_running;        /* 运行任务数量 */
    
    /* 简化的调度结构 */
    struct yat_accel_table *accel_table;              /* 加速表 */
    struct yat_cpu_history_table history_tables[NR_CPUS]; /* CPU历史表 */
};
```

### 3. 调度逻辑简化

#### 入队 (enqueue)
- **原始**: 创建复杂的yat_job -> 添加到全局就绪池 -> 建立双向关联
- **简化**: 直接初始化调度实体 -> 分配job_id -> 添加到全局就绪队列

#### 选择下一个任务 (pick_next)
- **原始**: 从全局就绪池选择job -> 找到关联的task -> 复杂的AJLR算法
- **简化**: 直接从全局就绪队列选择task -> 移动到运行队列 -> 简化的调度策略

#### 出队 (dequeue)
- **原始**: 从多个池中移除 -> 销毁job -> 清理关联
- **简化**: 直接从队列移除 -> 更新状态

## 保留的核心功能

### 1. AJLR算法核心
- 保留加速表 (accel_table)
- 保留历史表 (history_tables)
- 保留per_cpu_last_ts和recency计算

### 2. 调度信息
- 任务优先级管理
- WCET (最坏情况执行时间)
- 虚拟运行时间 (vruntime)
- CPU亲和性追踪

### 3. 内核集成
- 保持与CFS调度器的兼容性
- 保留所有调度类接口函数
- 保持内核日志输出

## 简化带来的优势

### 1. 消除复杂性
- **内存管理**: 减少动态分配/释放
- **指针关联**: 消除双向指针维护
- **锁竞争**: 减少多级锁的复杂性

### 2. 提高性能
- **缓存友好**: 数据结构更紧凑
- **减少间接访问**: 直接访问调度实体
- **简化路径**: 减少函数调用开销

### 3. 代码维护
- **逻辑清晰**: 单一数据结构
- **调试简单**: 减少状态不一致的可能
- **扩展容易**: 在单一结构体中添加字段

## 使用场景

这个简化版本特别适合：

1. **测试程序**: 不需要复杂DAG依赖的简单任务调度
2. **原型验证**: 快速验证AJLR算法的核心思想
3. **性能测试**: 减少开销，专注于调度算法本身
4. **教学演示**: 更容易理解和解释的代码结构

## 迁移指南

如果需要从原始复杂版本迁移到简化版本：

1. **数据结构迁移**:
   - 将`yat_job`的字段合并到`sched_yat_casched_entity`
   - 使用PID作为默认的job_id
   - 使用任务的prio作为priority

2. **函数调用更新**:
   - `p->yat_casched.job->field` → `p->yat_casched.field`
   - 移除所有DAG相关的函数调用
   - 简化ready_pool操作为直接的链表操作

3. **测试验证**:
   - 确保基本的调度功能正常
   - 验证AJLR算法的核心逻辑
   - 检查性能改进效果

## 总结

简化版本在保持YAT_CASCHED调度器核心功能的同时，大幅减少了代码复杂性和维护成本。对于大多数实际应用场景，这个简化版本能够提供足够的功能和更好的性能。
