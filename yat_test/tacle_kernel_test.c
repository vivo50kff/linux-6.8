#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <math.h>

// FIXME: 请根据内核中的定义，修改为正确的系统调用号
#define __NR_sched_set_wcet 462
#define WCET_NS (10 * 1000 * 1000) // 默认WCET: 10ms

#define NUM_TASKS 12
#define NUM_COPIES 20 //每个任务启动100个副本
#define SCHED_YAT_CASCHED 8  // YAT_CASCHED调度策略
#define PERIOD_MS 400
#define RUN_TIMES 10

// 选择taclebench kernel目录下的12个任务名（假设已编译为可执行文件并放在/bin下）
const char *task_names[NUM_TASKS] = {
    "binarysearch",
    "bitcount",
    "bitonic",
    "bsort",
    "complex_updates",
    "cosf",
    "countnegative",
    "cubic",
    "deg2rad",
    "fac",
    "fft",
    "filterbank"
};

// 存储每个任务的执行时间数据（用于生成箱线图）
typedef struct {
    double exec_times[NUM_COPIES * RUN_TIMES];  // 存储所有轮次的执行时间
    int count;                                  // 有效数据数量
    double min_time, max_time, avg_time;
    double median_time, std_dev;
    double q25, q75;  // 四分位数
} task_stats_t;

task_stats_t task_stats[NUM_TASKS];

int set_yat_scheduler(pid_t pid, int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    if (sched_setscheduler(pid, SCHED_YAT_CASCHED, &param) == 0) {
        return 0;
    } else {
        fprintf(stderr, "[YAT调度器] 设置失败: %s\n", strerror(errno));
        return -1;
    }
}

// 原有的时间计算函数（保持不变）
long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 +
           (end->tv_usec - start->tv_usec) / 1000;
}

// 新增：高精度时间计算函数（用于详细统计）
double timeval_diff_ms_precise(struct timeval *start, struct timeval *end) {
    return (double)(end->tv_sec - start->tv_sec) * 1000.0 + 
           (double)(end->tv_usec - start->tv_usec) / 1000.0;
}

// 计算统计数据
void calculate_task_statistics(int task_idx) {
    task_stats_t *stats = &task_stats[task_idx];
    
    if (stats->count == 0) return;
    
    // 排序用于计算中位数和四分位数
    double *sorted_times = malloc(stats->count * sizeof(double));
    memcpy(sorted_times, stats->exec_times, stats->count * sizeof(double));
    
    // 简单冒泡排序
    for (int i = 0; i < stats->count - 1; i++) {
        for (int j = 0; j < stats->count - i - 1; j++) {
            if (sorted_times[j] > sorted_times[j + 1]) {
                double temp = sorted_times[j];
                sorted_times[j] = sorted_times[j + 1];
                sorted_times[j + 1] = temp;
            }
        }
    }
    
    // 计算统计值
    stats->min_time = sorted_times[0];
    stats->max_time = sorted_times[stats->count - 1];
    
    // 平均值
    double sum = 0.0;
    for (int i = 0; i < stats->count; i++) {
        sum += stats->exec_times[i];
    }
    stats->avg_time = sum / stats->count;
    
    // 中位数
    if (stats->count % 2 == 0) {
        stats->median_time = (sorted_times[stats->count/2 - 1] + sorted_times[stats->count/2]) / 2.0;
    } else {
        stats->median_time = sorted_times[stats->count/2];
    }
    
    // 四分位数
    int q1_idx = stats->count / 4;
    int q3_idx = 3 * stats->count / 4;
    stats->q25 = sorted_times[q1_idx];
    stats->q75 = sorted_times[q3_idx];
    
    // 标准差
    double variance_sum = 0.0;
    for (int i = 0; i < stats->count; i++) {
        double diff = stats->exec_times[i] - stats->avg_time;
        variance_sum += diff * diff;
    }
    stats->std_dev = sqrt(variance_sum / stats->count);
    
    free(sorted_times);
}

// 输出Python可用的数据格式
void output_python_data() {
    printf("\n=== Python数据格式 (用于生成箱线图) ===\n");
    printf("# TacleBench YAT-CASched 测试数据\n");
    printf("yat_data = {\n");
    
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("    '%s': [", task_names[i]);
        for (int j = 0; j < task_stats[i].count; j++) {
            printf("%.3f", task_stats[i].exec_times[j]);
            if (j < task_stats[i].count - 1) printf(", ");
            // 每行显示10个数据
            if ((j + 1) % 10 == 0 && j < task_stats[i].count - 1) {
                printf("\n        ");
            }
        }
        printf("]");
        if (i < NUM_TASKS - 1) printf(",");
        printf("\n");
    }
    printf("}\n");
}

// 输出统计摘要（箱线图关键数据）
void output_statistics_summary() {
    printf("\n=== 详细统计摘要 (箱线图数据) ===\n");
    printf("%-15s %8s %8s %8s %8s %8s %8s %8s\n", 
           "任务名", "最小值", "Q1", "中位数", "Q3", "最大值", "平均值", "标准差");
    printf("========================================================================================\n");
    
    double total_median = 0.0, total_avg = 0.0, total_std = 0.0;
    double total_q1 = 0.0, total_q3 = 0.0;
    
    for (int i = 0; i < NUM_TASKS; i++) {
        calculate_task_statistics(i);
        printf("%-15s %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f\n",
               task_names[i],
               task_stats[i].min_time,
               task_stats[i].q25,
               task_stats[i].median_time,
               task_stats[i].q75,
               task_stats[i].max_time,
               task_stats[i].avg_time,
               task_stats[i].std_dev);
        
        total_median += task_stats[i].median_time;
        total_avg += task_stats[i].avg_time;
        total_std += task_stats[i].std_dev;
        total_q1 += task_stats[i].q25;
        total_q3 += task_stats[i].q75;
    }
    
    printf("========================================================================================\n");
    printf("%-15s %8s %8.3f %8.3f %8.3f %8s %8.3f %8.3f\n",
           "总体平均", "-", 
           total_q1/NUM_TASKS, total_median/NUM_TASKS, total_q3/NUM_TASKS, 
           "-", total_avg/NUM_TASKS, total_std/NUM_TASKS);
}

// 输出CSV格式数据
void output_csv_data() {
    printf("\n=== CSV格式数据 ===\n");
    printf("Task,Run,ExecutionTime_ms\n");
    
    for (int i = 0; i < NUM_TASKS; i++) {
        for (int j = 0; j < task_stats[i].count; j++) {
            printf("%s,%d,%.3f\n", task_names[i], j + 1, task_stats[i].exec_times[j]);
        }
    }
}

int main() {
    // 初始化统计数据
    for (int i = 0; i < NUM_TASKS; i++) {
        task_stats[i].count = 0;
    }
    
    int wcet[NUM_TASKS] = {
        3010, 4502, 4004, 6649, 1080, 1378, 
        8160, 1467, 2369, 1710, 2550, 1681
    };
    
    printf("=== TacleBench Kernel 12任务集周期测试（每任务%d副本）===\n", NUM_COPIES);
    printf("调度器: YAT-CASched\n");
    printf("测试配置: %d任务 × %d副本 × %d轮 = %d个进程\n", 
           NUM_TASKS, NUM_COPIES, RUN_TIMES, NUM_TASKS * NUM_COPIES * RUN_TIMES);
    
    struct timeval total_start, total_end;
    long total_exec_time = 0;  // 保留原有的时间统计
    
    // 父进程设置YAT_CASCHED调度策略
    if (set_yat_scheduler(0, 101) == 0) {
        printf("父进程PID=%d 成功设置为YAT_CASCHED调度策略\n", getpid());
    } else {
        printf("父进程PID=%d 设置YAT_CASCHED失败，使用默认策略\n", getpid());
    }
    
    gettimeofday(&total_start, NULL);
    
    for (int round = 0; round < RUN_TIMES; round++) {
        printf("=== 第%d轮 ===\n", round+1);
        pid_t children[NUM_TASKS * NUM_COPIES];
        struct timeval start_times[NUM_TASKS * NUM_COPIES], end_times[NUM_TASKS * NUM_COPIES];
        int task_indices[NUM_TASKS * NUM_COPIES];  // 记录每个进程对应的任务类型
        struct timeval round_start, round_end;
        
        gettimeofday(&round_start, NULL);
        int idx = 0;
        int f_pid = getpid(); // 父进程PID
        
        for (int i = 0; i < NUM_TASKS; i++) {
            // if (syscall(__NR_sched_set_wcet, f_pid, wcet[i]) != 0) {
            //     fprintf(stderr, "为PID %d 设置WCET失败: %s\n", f_pid, strerror(errno));
            // }
            
            for (int j = 0; j < NUM_COPIES; j++) {
                pid_t pid = fork();
                
                gettimeofday(&start_times[idx], NULL);
                if (pid == 0) {
                    // 子进程：设置调度策略并执行任务
                    set_yat_scheduler(0, 101);
                    
                    char path[64];
                    snprintf(path, sizeof(path), "/bin/%s", task_names[i]);
                    execl(path, task_names[i], NULL);
                    perror("execl失败");
                    exit(127);
                } else if (pid > 0) {
                    children[idx] = pid;
                    task_indices[idx] = i;  // 记录任务类型
                } else {
                    perror("fork失败");
                    exit(1);
                }
                idx++;
            }
        }
        
        // 等待所有子进程完成并收集时间数据
        for (int k = 0; k < NUM_TASKS * NUM_COPIES; k++) {
            int status;
            waitpid(children[k], &status, 0);
            gettimeofday(&end_times[k], NULL);
            
            // 原有的时间统计（保持不变）
            total_exec_time += timeval_diff_ms(&start_times[k], &end_times[k]);
            
            // 新增：详细的时间数据收集（用于箱线图）
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                double exec_time = timeval_diff_ms_precise(&start_times[k], &end_times[k]);
                int task_idx = task_indices[k];
                
                // 存储执行时间数据
                if (task_stats[task_idx].count < NUM_COPIES * RUN_TIMES) {
                    task_stats[task_idx].exec_times[task_stats[task_idx].count] = exec_time;
                    task_stats[task_idx].count++;
                }
            }
        }
        
        gettimeofday(&round_end, NULL);
        long round_elapsed = timeval_diff_ms(&round_start, &round_end);
        printf("本轮实际用时: %ld ms\n", round_elapsed);
        
        // 显示当前轮次的进度
        printf("数据收集进度: ");
        for (int i = 0; i < NUM_TASKS; i++) {
            printf("%s(%d) ", task_names[i], task_stats[i].count);
        }
        printf("\n");
    }
    
    gettimeofday(&total_end, NULL);
    long total_elapsed = timeval_diff_ms(&total_start, &total_end);
    
    // 原有的输出（保持不变）
    printf("=== 所有TacleBench Kernel任务完成 ===\n");
    printf("总运行时间: %ld ms\n", total_elapsed);
    printf("平均每轮用时: %.1f ms\n", (double)total_elapsed/RUN_TIMES);
    printf("总进程数: %d 个\n", NUM_TASKS * NUM_COPIES * RUN_TIMES);
    
    // 新增：详细的统计分析输出
    printf("\n=== 性能分析数据 ===\n");
    printf("有效数据收集情况:\n");
    int total_valid_samples = 0;
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("  %s: %d/%d 个样本\n", task_names[i], task_stats[i].count, NUM_COPIES * RUN_TIMES);
        total_valid_samples += task_stats[i].count;
    }
    printf("总有效样本数: %d/%d (%.1f%%)\n", 
           total_valid_samples, NUM_TASKS * NUM_COPIES * RUN_TIMES,
           (double)total_valid_samples / (NUM_TASKS * NUM_COPIES * RUN_TIMES) * 100);
    
    // 输出统计数据
    output_statistics_summary();
    
    // 输出可用于绘图的数据
    output_python_data();
    
    // 输出CSV数据（可选）
    // output_csv_data();
    
    printf("\n=== 数据已输出，可用于生成箱线图和性能分析 ===\n");
    printf("建议将Python数据部分复制到绘图脚本中使用。\n");
    
    return 0;
}