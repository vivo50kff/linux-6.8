/*
 * CFS 调度器对照测试程序
 * 100轮测试，每轮12个任务，每个任务50个周期
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <sys/resource.h>

#define SCHED_CFS 8  // CFS调度策略
#define NUM_ROUNDS 100       // 测试轮数
#define TASKS_PER_ROUND 12   // 每轮任务数
#define CYCLES_PER_TASK 50   // 每个任务的周期数
#define STANDARD_PRIORITY 120 // 统一优先级

// 测试统计数据
struct test_stats {
    double round_times[NUM_ROUNDS];
    double total_time;
    double avg_response_time;
    double max_response_time;
    double min_response_time;
    double std_deviation;
    double avg_schedule_delay;
    int successful_rounds;
};

// 获取当前时间戳(毫秒)
static long long get_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 模拟工作负载函数
void do_work_cycle(int task_id, int cycle_id) {
    volatile long long counter = 0;
    long long target = 50000; // 固定计算量
    
    while (counter < target) {
        counter++;
        // // 每1000次循环检查一次，避免过度占用CPU
        // if (counter % 1000 == 0) {
        //     usleep(1); // 1微秒的微小延迟
        // }
    }
}

// 设置YAT调度策略
int set_yat_scheduler(pid_t pid, int priority) {
    struct sched_param param;
    param.sched_priority = priority; 

    if (sched_setscheduler(pid, SCHED_CFS, &param) == 0) {
        return 0;
    } else {
        return -1;
    }
}

// 子任务执行函数
void child_task(int task_id, int round_id) {
    long long start_time, end_time;
    
    // 设置调度策略
    if (set_yat_scheduler(0, STANDARD_PRIORITY) != 0) {
        printf("警告: 任务%d无法设置YAT调度策略\n", task_id);
    }
    
    start_time = get_timestamp_ms();
    
    // 执行指定周期数的工作
    for (int cycle = 0; cycle < CYCLES_PER_TASK; cycle++) {
        do_work_cycle(task_id, cycle);
        
        // 周期间短暂让出CPU
        usleep(100); // 100微秒
    }
    
    end_time = get_timestamp_ms();
    
    // 通过exit code返回执行时间(受限于exit code范围)
    long execution_time = end_time - start_time;
    if (execution_time > 255) execution_time = 255;
    
    exit((int)execution_time);
}

// 计算标准差
double calculate_std_deviation(double data[], int count, double mean) {
    double sum_squared_diff = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = data[i] - mean;
        sum_squared_diff += diff * diff;
    }
    return sqrt(sum_squared_diff / count);
}

// 执行单轮测试
double run_single_round(int round_id) {
    pid_t children[TASKS_PER_ROUND];
    long long round_start, round_end;
    int task_times[TASKS_PER_ROUND];
    
    printf("第%d轮测试开始...\n", round_id + 1);
    
    round_start = get_timestamp_ms();
    
    // 创建子进程
    for (int i = 0; i < TASKS_PER_ROUND; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程
            child_task(i + 1, round_id);
        } else if (pid > 0) {
            // 父进程
            children[i] = pid;
        } else {
            perror("fork失败");
            return -1.0;
        }
    }
    
    // 等待所有子进程完成
    for (int i = 0; i < TASKS_PER_ROUND; i++) {
        int status;
        waitpid(children[i], &status, 0);
        
        if (WIFEXITED(status)) {
            task_times[i] = WEXITSTATUS(status);
        } else {
            task_times[i] = 0;
        }
    }
    
    round_end = get_timestamp_ms();
    
    double round_time = (double)(round_end - round_start);
    printf("第%d轮完成，耗时: %.2f ms\n", round_id + 1, round_time);
    
    return round_time;
}

// 运行完整测试并计算统计数据
void run_full_test(struct test_stats *stats) {
    printf("=== CFS 调度器性能测试 ===\n");
    printf("测试配置: %d轮 × %d任务 × %d周期\n", 
           NUM_ROUNDS, TASKS_PER_ROUND, CYCLES_PER_TASK);
    printf("统一优先级: %d\n\n", STANDARD_PRIORITY);
    
    long long total_start = get_timestamp_ms();
    
    stats->successful_rounds = 0;
    stats->max_response_time = 0.0;
    stats->min_response_time = 999999.0;
    
    // 执行所有轮次
    for (int round = 0; round < NUM_ROUNDS; round++) {
        double round_time = run_single_round(round);
        
        if (round_time > 0) {
            stats->round_times[stats->successful_rounds] = round_time;
            
            if (round_time > stats->max_response_time) {
                stats->max_response_time = round_time;
            }
            if (round_time < stats->min_response_time) {
                stats->min_response_time = round_time;
            }
            
            stats->successful_rounds++;
        }
        
        // 轮次间暂停
        usleep(10000); // 10ms
    }
    
    long long total_end = get_timestamp_ms();
    stats->total_time = (double)(total_end - total_start);
    
    // 计算统计数据
    if (stats->successful_rounds > 0) {
        double sum = 0.0;
        for (int i = 0; i < stats->successful_rounds; i++) {
            sum += stats->round_times[i];
        }
        stats->avg_response_time = sum / stats->successful_rounds;
        stats->std_deviation = calculate_std_deviation(
            stats->round_times, stats->successful_rounds, stats->avg_response_time);
        
        // 估算平均调度延迟(简化计算)
        stats->avg_schedule_delay = stats->avg_response_time / (TASKS_PER_ROUND * CYCLES_PER_TASK);
    }
}

// 打印测试结果
void print_test_results(struct test_stats *stats) {
    printf("\n=== CFS 测试结果统计 ===\n");
    printf("成功完成轮数: %d/%d\n", stats->successful_rounds, NUM_ROUNDS);
    printf("总测试时间: %.2f ms\n", stats->total_time);
    printf("平均响应时间: %.2f ms\n", stats->avg_response_time);
    printf("最大响应时间: %.2f ms\n", stats->max_response_time);
    printf("最小响应时间: %.2f ms\n", stats->min_response_time);
    printf("响应时间标准差: %.2f ms\n", stats->std_deviation);
    printf("平均调度延迟: %.4f ms\n", stats->avg_schedule_delay);
    
    // 输出详细的轮次数据(前10轮和后10轮)
    printf("\n详细轮次数据 (前10轮):\n");
    for (int i = 0; i < 10 && i < stats->successful_rounds; i++) {
        printf("轮次%d: %.2f ms\n", i + 1, stats->round_times[i]);
    }
    
    if (stats->successful_rounds > 20) {
        printf("... (中间轮次省略) ...\n");
        printf("详细轮次数据 (后10轮):\n");
        for (int i = stats->successful_rounds - 10; i < stats->successful_rounds; i++) {
            printf("轮次%d: %.2f ms\n", i + 1, stats->round_times[i]);
        }
    }
}

int main() {
    struct test_stats stats = {0};
    
    printf("=== CFS 调度器测试程序 ===\n");
    printf("当前进程PID: %d\n", getpid());
    printf("系统CPU数量: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    
    // 设置父进程调度策略
    // if (set_yat_scheduler(0, STANDARD_PRIORITY) == 0) {
    //     printf("父进程成功设置为CFS调度策略\n");
    // } else {
    //     printf("父进程设置CFS失败，使用默认策略\n");
    // }
    
    // 运行完整测试
    run_full_test(&stats);
    
    // 打印结果
    print_test_results(&stats);
    
    return 0;
}