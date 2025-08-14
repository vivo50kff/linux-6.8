#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <fcntl.h>

// 系统调用定义
#define __NR_sched_set_wcet 462
#define SCHED_YAT_CASCHED 8

// 测试配置
#define NUM_CRITICAL_PROCESSES 3     // 关键进程数量
#define NUM_NORMAL_PROCESSES 15       // 普通进程数量
#define CRITICAL_PRIORITY 101         // 关键进程优先级（高优先级，数值小）
#define NORMAL_PRIORITY 120          // 普通进程优先级（低优先级，数值大）
#define CRITICAL_WCET_MS 100         // 关键进程WCET（毫秒）
#define NORMAL_WCET_MS 3000           // 普通进程WCET（毫秒）

#define LOG_FILE "/tmp/schedule_log.txt"
#define MAX_LOG_ENTRIES 1000

// 日志条目结构
typedef struct {
    long timestamp_us;      // 微秒时间戳
    int process_id;
    char type[16];          // "关键" 或 "普通"
    char event[16];         // "开始" 或 "完成"
    int priority;
    pid_t pid;
    long duration_ms;       // 对于完成事件，记录执行时长
} log_entry_t;

// 写入时间戳日志
void write_log(const char* type, int process_id, const char* event, int priority, long duration_ms) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    log_entry_t entry;
    entry.timestamp_us = tv.tv_sec * 1000000 + tv.tv_usec;
    entry.process_id = process_id;
    strncpy(entry.type, type, sizeof(entry.type) - 1);
    entry.type[sizeof(entry.type) - 1] = '\0';
    strncpy(entry.event, event, sizeof(entry.event) - 1);
    entry.event[sizeof(entry.event) - 1] = '\0';
    entry.priority = priority;
    entry.pid = getpid();
    entry.duration_ms = duration_ms;
    
    // 以追加模式写入文件
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "%ld|%d|%s|%s|%d|%d|%ld\n", 
                entry.timestamp_us, entry.process_id, entry.type, 
                entry.event, entry.priority, entry.pid, entry.duration_ms);
        fclose(f);
    }
}

// 工作负载函数：只记录开始和结束时间戳
void do_intensive_work(int process_id, const char* type, int priority, int target_duration_ms) {
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // 记录开始时间戳
    write_log(type, process_id, "开始", priority, 0);
    
    volatile long counter = 0;
    long target_us = target_duration_ms * 1000;
    
    while (1) {
        // CPU密集型工作
        for (int i = 0; i < 50000000; i++) {
            counter += i * i;
            counter = counter % 1000000;
        }
        
        gettimeofday(&end, NULL);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + 
                         (end.tv_usec - start.tv_usec);
        
        if (elapsed_us >= target_us) {
            break;
        }
    }
    
    gettimeofday(&end, NULL);
    long actual_duration = (end.tv_sec - start.tv_sec) * 1000 + 
                          (end.tv_usec - start.tv_usec) / 1000;
    
    // 记录完成时间戳
    write_log(type, process_id, "完成", priority, actual_duration);
}

int set_parent_scheduler(int priority, int wcet_ms) {
    // 设置父进程的WCET (转换为纳秒)
    long wcet_ns = (long)wcet_ms * 1000000;
    if (syscall(__NR_sched_set_wcet, getpid(), wcet_ns) != 0) {
        fprintf(stderr, "[父进程] 设置WCET=%dms失败: %s\n", wcet_ms, strerror(errno));
        return -1;
    }
    
    // 设置父进程的调度策略
    struct sched_param param;
    param.sched_priority = priority;
    if (sched_setscheduler(getpid(), SCHED_YAT_CASCHED, &param) == 0) {
        return 0;
    } else {
        fprintf(stderr, "[父进程] 设置调度策略失败: %s\n", strerror(errno));
        return -1;
    }
}

// 比较函数，用于排序
int compare_log_entries(const void *a, const void *b) {
    const log_entry_t *entry_a = (const log_entry_t *)a;
    const log_entry_t *entry_b = (const log_entry_t *)b;
    
    if (entry_a->timestamp_us < entry_b->timestamp_us) return -1;
    if (entry_a->timestamp_us > entry_b->timestamp_us) return 1;
    return 0;
}

// 读取并排序显示日志
void display_sorted_results() {
    FILE *f = fopen(LOG_FILE, "r");
    if (!f) {
        printf("无法读取日志文件\n");
        return;
    }
    
    log_entry_t entries[MAX_LOG_ENTRIES];
    int count = 0;
    char line[256];
    
    // 读取所有日志条目
    while (fgets(line, sizeof(line), f) && count < MAX_LOG_ENTRIES) {
        sscanf(line, "%ld|%d|%15[^|]|%15[^|]|%d|%d|%ld",
               &entries[count].timestamp_us,
               &entries[count].process_id,
               entries[count].type,
               entries[count].event,
               &entries[count].priority,
               &entries[count].pid,
               &entries[count].duration_ms);
        count++;
    }
    fclose(f);
    
    if (count == 0) {
        printf("没有找到日志条目\n");
        return;
    }
    
    // 按时间戳排序
    qsort(entries, count, sizeof(log_entry_t), compare_log_entries);
    
    // 计算相对时间（以第一个事件为起点）
    long base_time = entries[0].timestamp_us;
    
    printf("\n=== 按时间顺序的执行记录 ===\n");
    printf("时间戳(ms) | 进程类型 | 进程ID | 事件 | 优先级 | PID   | 执行时长(ms)\n");
    printf("-----------|----------|--------|------|--------|-------|------------\n");
    
    for (int i = 0; i < count; i++) {
        long relative_time_ms = (entries[i].timestamp_us - base_time) / 1000;
        
        if (strcmp(entries[i].event, "完成") == 0) {
            printf("%8ld   | %8s | %6d | %4s | %6d | %5d | %8ld\n",
                   relative_time_ms,
                   entries[i].type,
                   entries[i].process_id,
                   entries[i].event,
                   entries[i].priority,
                   entries[i].pid,
                   entries[i].duration_ms);
        } else {
            printf("%8ld   | %8s | %6d | %4s | %6d | %5d | %8s\n",
                   relative_time_ms,
                   entries[i].type,
                   entries[i].process_id,
                   entries[i].event,
                   entries[i].priority,
                   entries[i].pid,
                   "-");
        }
    }
    
    // 分析结果
    printf("\n=== 调度分析 ===\n");
    
    // 统计关键进程和普通进程的执行情况
    int critical_started = 0, critical_finished = 0;
    int normal_started = 0, normal_finished = 0;
    long first_critical_start = -1, last_normal_start = -1;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].type, "关键") == 0) {
            if (strcmp(entries[i].event, "开始") == 0) {
                critical_started++;
                if (first_critical_start == -1) {
                    first_critical_start = (entries[i].timestamp_us - base_time) / 1000;
                }
            } else {
                critical_finished++;
            }
        } else if (strcmp(entries[i].type, "普通") == 0) {
            if (strcmp(entries[i].event, "开始") == 0) {
                normal_started++;
                last_normal_start = (entries[i].timestamp_us - base_time) / 1000;
            } else {
                normal_finished++;
            }
        }
    }
    
    printf("关键进程: %d个启动, %d个完成\n", critical_started, critical_finished);
    printf("普通进程: %d个启动, %d个完成\n", normal_started, normal_finished);
    
    if (first_critical_start != -1 && last_normal_start != -1) {
        if (first_critical_start < last_normal_start) {
            printf("✓ 调度效果: 关键进程在%ldms启动，早于最后一个普通进程(%ldms)\n", 
                   first_critical_start, last_normal_start);
        } else {
            printf("✗ 调度异常: 关键进程启动(%ldms)晚于普通进程(%ldms)\n", 
                   first_critical_start, last_normal_start);
        }
    }
    
    // 检查是否存在抢占行为
    printf("\n抢占行为分析:\n");
    int overlapping_executions = 0;
    for (int i = 0; i < count - 1; i++) {
        if (strcmp(entries[i].event, "开始") == 0) {
            // 查找这个进程的完成事件
            for (int j = i + 1; j < count; j++) {
                if (entries[j].pid == entries[i].pid && strcmp(entries[j].event, "完成") == 0) {
                    // 检查在这个时间段内是否有其他进程开始
                    for (int k = i + 1; k < j; k++) {
                        if (strcmp(entries[k].event, "开始") == 0 && entries[k].pid != entries[i].pid) {
                            overlapping_executions++;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
    
    if (overlapping_executions > 0) {
        printf("检测到%d次可能的抢占行为\n", overlapping_executions);
    } else {
        printf("未检测到明显的抢占行为（可能是串行执行）\n");
    }
}

long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_usec - start->tv_usec) / 1000;
}

int main() {
    printf("=== YAT_CASCHED 优先级调度时序测试 ===\n");
    printf("关键进程: %d个 (优先级=%d, WCET=%dms)\n", NUM_CRITICAL_PROCESSES, CRITICAL_PRIORITY, CRITICAL_WCET_MS);
    printf("普通进程: %d个 (优先级=%d, WCET=%dms)\n", NUM_NORMAL_PROCESSES, NORMAL_PRIORITY, NORMAL_WCET_MS);
    printf("测试策略: 记录精确时间戳，最后排序分析\n\n");
    
    // 清理旧的日志文件
    unlink(LOG_FILE);
    
    struct timeval test_start, test_end;
    pid_t all_children[NUM_CRITICAL_PROCESSES + NUM_NORMAL_PROCESSES];
    int child_count = 0;
    
    gettimeofday(&test_start, NULL);
    
    printf("第1阶段: 启动%d个普通进程...\n", NUM_NORMAL_PROCESSES);
    set_parent_scheduler(NORMAL_PRIORITY, NORMAL_WCET_MS);
    
    for (int i = 0; i < NUM_NORMAL_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：静默执行，只记录时间戳
            do_intensive_work(i+1, "普通", NORMAL_PRIORITY, NORMAL_WCET_MS);
            exit(0);
        } else if (pid > 0) {
            all_children[child_count++] = pid;
        } else {
            perror("创建普通进程失败");
            exit(1);
        }
    }
    
    // 稍等片刻让普通进程开始执行
    usleep(100000);  // 100ms
    
    printf("第2阶段: 启动%d个关键进程...\n", NUM_CRITICAL_PROCESSES);
    set_parent_scheduler(CRITICAL_PRIORITY, CRITICAL_WCET_MS);
    
    for (int i = 0; i < NUM_CRITICAL_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：静默执行，只记录时间戳
            do_intensive_work(i+1, "关键", CRITICAL_PRIORITY, CRITICAL_WCET_MS);
            exit(0);
        } else if (pid > 0) {
            all_children[child_count++] = pid;
            usleep(50000);  // 50ms间隔
        } else {
            perror("创建关键进程失败");
            exit(1);
        }
    }
    
    printf("第3阶段: 等待所有进程完成...\n");
    
    // 等待所有子进程完成
    for (int i = 0; i < child_count; i++) {
        int status;
        waitpid(all_children[i], &status, 0);
    }
    
    gettimeofday(&test_end, NULL);
    long total_time = timeval_diff_ms(&test_start, &test_end);
    
    printf("所有进程已完成，总耗时: %ldms\n", total_time);
    
    // 显示排序后的结果
    display_sorted_results();
    
    return 0;
}