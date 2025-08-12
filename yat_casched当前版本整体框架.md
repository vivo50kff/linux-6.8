# Yat_Casched 决赛版全局缓存感知调度器 - 数据结构实现文档

## 修改概述
本次修改将Yat_Casched从初赛版的本地化调度升级为决赛版的全局缓存感知调度器，采用"全局优先队列+加速表"两层结构，实现LCIF思想的ajlr调度算法。

## 新增数据结构

### 1. 加速表 (Acceleration Table)
```c
struct yat_accel_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    pid_t task_pid;                /* 任务PID */
    int cpu_id;                    /* CPU编号 */
    u64 benefit;                   /* 加速值（benefit） */
    u64 wcet;                      /* 任务的WCET */
    u64 crp_ratio;                 /* CRP比率（千分比表示，避免浮点） */
    u64 last_update_time;          /* 最后更新时间 */
};

struct yat_accel_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 全局锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
};
```

**设计要点:**
- 使用二维哈希表实现(task_pid, cpu_id) → benefit的映射
- 哈希函数: `hash_32((u32)task_pid ^ (u32)cpu_id, YAT_ACCEL_HASH_BITS)`
- 支持动态增删查改操作
- benefit计算公式: `benefit = (1 - crp_ratio) * WCET`
- 使用u64类型存储所有时间和数值，确保精度

### 2. 全局优先队列 (Global Task Pool)
```c
struct yat_global_task_node {
    struct list_head list;         /* 链表节点 */
    struct task_struct *task;      /* 任务指针 */
    int priority;                  /* 优先级 */
    u64 wcet;                      /* WCET */
    u64 enqueue_time;              /* 入队时间 */
};

struct yat_global_task_pool {
    struct list_head head;         /* 队列头 */
    spinlock_t lock;               /* 队列锁 */
    unsigned int nr_tasks;         /* 队列中任务数 */
};
```

**设计要点:**
- 使用有序链表实现优先队列
- 排序规则: 优先级高者在前，优先级相同则WCET大者在前
- 支持入队、出队、查看队头、移除指定任务等操作
- 全局唯一，所有CPU共享

### 3. CPU历史表 (CPU History Table)
```c
struct yat_history_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    u64 timestamp;                 /* 时间戳作为键 */
    struct task_struct *task;      /* 任务指针 */
    u64 insert_time;               /* 插入时间 */
};

struct yat_cpu_history_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 该CPU历史表的锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
};
```

**设计要点:**
- 每个CPU维护独立的时间戳→任务映射关系
- 哈希函数: `hash_64(timestamp, YAT_HISTORY_HASH_BITS)`
- 支持精确的时间戳查找和过期清理
- 为后续实现impact、缓存污染分析提供基础

### 4. 增强的任务实体
```c
struct sched_yat_casched_entity {
    struct list_head run_list;      /* 运行队列链表节点 */
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    int last_cpu;                   /* 上次运行的CPU */
    u64 per_cpu_recency[NR_CPUS];   /* 每个CPU的recency值 */
    u64 wcet;                       /* 任务的WCET */
    int priority;                   /* 任务优先级 */
};
```

## 接口函数

### 加速表接口
- `yat_accel_table_init()` - 初始化加速表
- `yat_accel_table_destroy()` - 销毁加速表
- `yat_accel_table_insert()` - 插入新条目
- `yat_accel_table_update()` - 更新现有条目
- `yat_accel_table_lookup()` - 查找benefit值
- `yat_accel_table_delete()` - 删除指定条目
- `yat_accel_table_cleanup()` - 清理过期条目

### 历史表接口
- `init_cpu_history_tables()` - 初始化所有CPU的历史表
- `destroy_cpu_history_tables()` - 销毁所有CPU的历史表
- `add_history_record()` - 添加历史记录
- `get_history_task()` - 查询指定时间戳的任务
- `prune_history_table()` - 清理过期历史记录

### 全局优先队列接口
- `yat_global_pool_init()` - 初始化队列
- `yat_global_pool_destroy()` - 销毁队列
- `yat_global_pool_enqueue()` - 按优先级入队
- `yat_global_pool_dequeue()` - 出队（取队头）
- `yat_global_pool_peek()` - 查看队头（不移除）
- `yat_global_pool_empty()` - 检查是否为空
- `yat_global_pool_remove()` - 移除指定任务

## 数据类型设计说明

### 时间相关
- `u64 timestamp` - 64位无符号整数，存储jiffies时间戳
- `u64 exec_time` - 64位无符号整数，存储执行时间（纳秒）
- `u64 wcet` - 64位无符号整数，存储WCET值

### 标识符
- `pid_t task_pid` - 任务PID，与内核标准一致
- `int cpu_id` - CPU编号，使用int类型

### 数值计算
- `u64 benefit` - 64位无符号整数，存储加速值
- `u64 crp_ratio` - 64位无符号整数，千分比表示（避免浮点运算）
- `int priority` - 任务优先级，使用int类型

## 哈希表配置

### 哈希位数配置
```c
#define YAT_CASCHED_HASH_BITS  6   /* 2^6 = 64个桶，原有任务哈希表 */
#define YAT_ACCEL_HASH_BITS    8   /* 2^8 = 256个桶，加速表 */
#define YAT_HISTORY_HASH_BITS  7   /* 2^7 = 128个桶，历史表 */
```

### 哈希函数设计
- **加速表**: `hash_32((u32)task_pid ^ (u32)cpu_id, YAT_ACCEL_HASH_BITS)`
  - 将task_pid和cpu_id进行异或操作后取哈希
  - 确保(task,cpu)组合的均匀分布
- **历史表**: `hash_64(timestamp, YAT_HISTORY_HASH_BITS)`
  - 直接对64位时间戳取哈希
  - 利用时间戳的随机性实现均匀分布

## 内存管理

### 动态分配策略
- 所有哈希表桶使用`kcalloc()`分配
- 哈希表条目使用`kmalloc(GFP_ATOMIC)`分配
- 全局结构使用`kmalloc(GFP_KERNEL)`分配

### 生命周期管理
- **全局结构**: 系统启动时分配，系统关闭时释放
- **哈希表条目**: 按需分配，定期清理过期条目
- **CPU本地结构**: CPU上线时分配，CPU下线时释放

## 并发控制

### 锁设计
- **加速表**: 全局锁，保护整个加速表
- **历史表**: 每个CPU独立锁，提高并发性能
- **全局队列**: 全局锁，保护队列操作
- **原有CPU历史**: 每个CPU独立锁，保持兼容性

### 锁顺序
为避免死锁，统一锁获取顺序：
1. 全局队列锁
2. 加速表锁  
3. CPU历史表锁（按CPU编号升序）

## 性能考虑

### 时间复杂度
- **加速表查找**: O(1) 平均，O(n) 最坏
- **历史表查找**: O(1) 平均，O(n) 最坏
- **全局队列操作**: O(n) 插入，O(1) 出队

### 空间复杂度
- **加速表**: O(活跃(task,cpu)组合数)
- **历史表**: O(活跃时间戳数 × CPU数)
- **全局队列**: O(待调度任务数)

### 优化策略
- 定期清理过期条目，防止内存泄漏
- 使用合适的哈希桶数量，平衡内存和性能
- 细粒度锁，减少锁竞争

## 扩展性

### 支持的扩展功能
1. **Impact分析**: 利用历史表分析任务间的缓存冲突
2. **动态WCET**: 根据历史执行时间动态调整WCET
3. **智能清理**: 根据系统负载动态调整清理频率
4. **负载预测**: 基于历史数据预测未来负载

### 接口预留
所有接口都为后续功能扩展预留了参数空间，支持：
- 更复杂的CRP模型
- 多级缓存层次结构
- NUMA感知调度
- 实时任务支持

## 兼容性

### 向后兼容
- 保留原有的CPU历史表结构和接口
- 保持原有的调度类接口不变
- 新增功能通过配置选项控制

### 错误处理
- 所有动态分配都有失败处理
- 锁操作都有超时和重试机制
- 哈希表操作都有边界检查

---

**修改日期**: 2025年7月16日  
**版本**: 决赛版 v1.0  
**负责人**: GitHub Copilot
