/*
 * YAT_CASCHED 简化调度器测试程序
 * 创建多个任务测试YAT调度器功能
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

#define SCHED_YAT_CASCHED 8  // YAT_CASCHED调度策略
#define CPU_NUM_PER_SET 2  // L2缓存集群的核心数

// 模拟工作负载的函数
void do_work(int task_id, int work_time_ms) {
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // printf("[Task %d] 开始工作，目标时间：%d ms\n", task_id, work_time_ms);
    
    // CPU密集型工作
    volatile long long counter = 0;
    long long target = (long long)work_time_ms * 1000000;  // 根据时间调整计算量
    
    while (counter < target) {
        counter++;
        // 每隔一段时间检查是否被调度
        if (counter % 100000 == 0) {
            gettimeofday(&end, NULL);
            long elapsed = (end.tv_sec - start.tv_sec) * 1000 + 
                          (end.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= work_time_ms) {
                break;
            }
        }
    }
    
    gettimeofday(&end, NULL);
    long elapsed = (end.tv_sec - start.tv_sec) * 1000 + 
                  (end.tv_usec - start.tv_usec) / 1000;
    
    // printf("[Task %d] 完成工作，实际用时：%ld ms\n", task_id, elapsed);
}

// 设置调度策略
int set_yat_scheduler(pid_t pid, int priority) {
    struct sched_param param;
    // param.sched_priority = priority;
    param.sched_priority = 0; // YAT_CASCHED 策略不使用该优先级，应设为0
    
    // 尝试设置YAT_CASCHED调度策略
    if (sched_setscheduler(pid, SCHED_YAT_CASCHED, &param) == 0) {
        // printf("成功设置YAT_CASCHED调度策略，优先级：%d\n", priority);
        return 0;
    } else {
        // printf("设置YAT_CASCHED失败: %s，使用默认调度策略\n", strerror(errno));
        return -1;
    }
}

// 获取当前CPU
int get_current_cpu() {
    return sched_getcpu();
}

// 子任务函数
void child_task(int task_id, int priority, int work_cycles) {
    // printf("[Task %d] 开始执行工作周期\n", task_id);
    
    // 执行多个工作周期
    for (int cycle = 0; cycle < work_cycles; cycle++) {
        int current_cpu = get_current_cpu();
        // printf("[Task %d] 周期 %d，当前CPU: %d\n", task_id, cycle + 1, current_cpu);
        
        // 执行工作
        do_work(task_id, 50 + (task_id * 10));  // 不同任务有不同的工作时间
        
        // 短暂休眠，让其他任务有机会运行
        usleep(10000);  // 10ms
    }
    
    printf("[Task %d] 完成所有工作周期\n", task_id);
}

int main() {
    printf("=== YAT_CASCHED 调度器简化测试 ===\n");
    printf("当前内核版本测试，PID: %d\n", getpid());
    
    /* 设置父进程的调度策略为YAT_CASCHED */
    if (set_yat_scheduler(0, 0) == 0) {  /* 父进程使用优先级10 */
        printf("父进程PID=%d 成功设置为YAT_CASCHED调度策略\n", getpid());
    } else {
        printf("父进程PID=%d 设置YAT_CASCHED失败，使用默认策略\n", getpid());
    }
    
    // 显示CPU信息
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    printf("系统CPU数量: %d\n", num_cpus);
    
    const int num_tasks = 12;
    const int work_cycles = 3;
    pid_t children[num_tasks];
    int i=0;
    // 创建多个子进程进行测试
    printf("\n开始创建测试任务...\n");
    for ( i=0; i < num_tasks; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程：立即设置调度策略，避免用默认调度器运行
            int priority = 0;  // 所有任务都使用相同的sched_priority
            int job_priority=(i+1)%19+1;  // 内部作业优先级
            
            // 立即设置调度策略
            // if (set_yat_scheduler(0, job_priority) == 0) {
            //     printf("[Task %d] PID: %d, YAT调度器设置成功，sched_priority: %d\n", i+1, getpid(), job_priority);
            // } else {
            //     printf("[Task %d] PID: %d, 调度器设置失败，使用默认策略，sched_priority: %d\n", i+1, getpid(), job_priority);
            // }
            
            // 然后执行任务
            child_task(i + 1, job_priority, work_cycles);
            exit(0);
        } else if (pid > 0) {
            // 父进程
            children[i] = pid;
            printf("创建子任务 %d，PID: %d\n", i + 1, pid);
        } else {
            perror("fork失败");
            exit(1);
        }
    }
    
    // 父进程等待所有子进程完成
    printf("\n等待所有任务完成...\n");
    for ( i = 0; i < num_tasks; i++) {
        int status;
        waitpid(children[i], &status, 0);
        // printf("任务 %d (PID: %d) 已完成\n", i + 1, children[i]);
    }
    
    printf("\n=== 所有测试任务完成 ===\n");
    
    // 显示一些调度统计信息
    // printf("\n--- 调度器测试总结 ---\n");
    // printf("测试任务数量: %d\n", num_tasks);
    // printf("每个任务工作周期: %d\n", work_cycles);
    // printf("系统CPU数量: %d\n", num_cpus);
    // printf("调度策略: YAT_CASCHED \n");
    // printf("内部作业优先级分配:\n");

    return 0;
}
