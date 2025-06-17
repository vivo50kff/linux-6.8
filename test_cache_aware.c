#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <fcntl.h>

// 定义Yat_Casched调度策略ID
#define SCHED_YAT_CASCHED 8

// 测试任务结构
struct test_task_info {
    int task_id;
    int initial_cpu;
    int final_cpu;
    int cpu_switches;
    int iterations;
    pid_t pid;
    char task_name[32];
};

// 全局变量用于结果收集
struct test_task_info task_results[20];
int task_count = 0;

// 获取当前运行的CPU
int get_current_cpu() {
    return sched_getcpu();
}

// 设置调度策略为Yat_Casched
int set_yat_casched_policy(pid_t pid) {
    struct sched_param param;
    param.sched_priority = 0;
    
    int ret = sched_setscheduler(pid, SCHED_YAT_CASCHED, &param);
    if (ret == -1) {
        printf("Task %d: Failed to set Yat_Casched policy: %s\n", 
               pid, strerror(errno));
        return -1;
    }
    
    printf("Task %d: ✓ Set to Yat_Casched policy\n", pid);
    return 0;
}

// 计算密集型任务 - 模拟缓存使用
void intensive_computation(int task_id, int duration_ms) {
    volatile long sum = 0;
    int current_cpu = get_current_cpu();
    int last_cpu = current_cpu;
    int cpu_switches = 0;
    int iterations = 0;
    
    // 记录初始CPU
    printf("Task%d: Started on CPU %d\n", task_id, current_cpu);
    
    clock_t start = clock();
    clock_t end = start + (duration_ms * CLOCKS_PER_SEC / 1000);
    
    // 执行计算密集型工作
    while (clock() < end) {
        // 模拟缓存敏感的计算
        for (int i = 0; i < 10000; i++) {
            sum += i * i + task_id;
        }
        
        // 检查CPU切换
        current_cpu = get_current_cpu();
        if (current_cpu != last_cpu) {
            cpu_switches++;
            printf("Task%d: CPU switch %d->%d (switch #%d)\n", 
                   task_id, last_cpu, current_cpu, cpu_switches);
            last_cpu = current_cpu;
        }
        
        iterations++;
        
        // 偶尔让出CPU，让调度器有机会重新调度
        if (iterations % 100 == 0) {
            usleep(100); // 0.1ms
        }
    }
    
    // 记录结果
    if (task_count < 20) {
        task_results[task_count].task_id = task_id;
        task_results[task_count].initial_cpu = get_current_cpu();
        task_results[task_count].final_cpu = current_cpu;
        task_results[task_count].cpu_switches = cpu_switches;
        task_results[task_count].iterations = iterations;
        task_results[task_count].pid = getpid();
        snprintf(task_results[task_count].task_name, 32, "Task%d", task_id);
        task_count++;
    }
    
    printf("Task%d: Completed on CPU %d, switches=%d, sum=%ld\n", 
           task_id, current_cpu, cpu_switches, sum);
}

// 创建重复任务来测试缓存感知性
void test_cache_aware_scheduling() {
    printf("\n=== 缓存感知性调度测试 ===\n");
    printf("目标：验证相同任务能否被调度到相同CPU以提高缓存局部性\n\n");
    
    // 第一轮：创建多个相同的Task1
    printf("第一轮：创建5个Task1进程\n");
    for (int i = 0; i < 5; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            if (set_yat_casched_policy(getpid()) == 0) {
                intensive_computation(1, 2000); // 2秒计算
            }
            exit(0);
        } else if (pid > 0) {
            printf("Created Task1 process %d (instance %d)\n", pid, i+1);
            usleep(100000); // 100ms间隔
        }
    }
    
    // 等待第一轮完成
    for (int i = 0; i < 5; i++) {
        wait(NULL);
    }
    
    printf("\n第二轮：创建3个Task2进程\n");
    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            if (set_yat_casched_policy(getpid()) == 0) {
                intensive_computation(2, 1500); // 1.5秒计算
            }
            exit(0);
        } else if (pid > 0) {
            printf("Created Task2 process %d (instance %d)\n", pid, i+1);
            usleep(150000); // 150ms间隔
        }
    }
    
    // 等待第二轮完成
    for (int i = 0; i < 3; i++) {
        wait(NULL);
    }
    
    printf("\n第三轮：再次创建5个Task1进程（测试缓存重用）\n");
    for (int i = 0; i < 5; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            if (set_yat_casched_policy(getpid()) == 0) {
                intensive_computation(1, 1000); // 1秒计算
            }
            exit(0);
        } else if (pid > 0) {
            printf("Created Task1 process %d (instance %d, round 2)\n", pid, i+1);
            usleep(80000); // 80ms间隔
        }
    }
    
    // 等待第三轮完成
    for (int i = 0; i < 5; i++) {
        wait(NULL);
    }
}

// 并发任务测试 - 多个任务同时运行
void test_concurrent_tasks() {
    printf("\n=== 并发任务调度测试 ===\n");
    printf("目标：测试多个任务并发时的CPU分配策略\n\n");
    
    pid_t pids[8];
    int task_types[] = {1, 1, 2, 1, 3, 2, 1, 3}; // 混合任务类型
    
    // 同时启动8个任务
    for (int i = 0; i < 8; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // 子进程
            if (set_yat_casched_policy(getpid()) == 0) {
                intensive_computation(task_types[i], 3000); // 3秒计算
            }
            exit(0);
        } else if (pids[i] > 0) {
            printf("Started concurrent Task%d process %d\n", task_types[i], pids[i]);
        }
        usleep(50000); // 50ms间隔启动
    }
    
    // 等待所有任务完成
    for (int i = 0; i < 8; i++) {
        wait(NULL);
    }
}

// 分析测试结果
void analyze_results() {
    printf("\n" "=" * 60 "\n");
    printf("                缓存感知性调度分析报告\n");
    printf("=" * 60 "\n");
    
    printf("CPU核心数: %d\n", get_nprocs());
    printf("总任务数: %d\n\n", task_count);
    
    // 按任务类型统计
    int task1_count = 0, task2_count = 0, task3_count = 0;
    int task1_cpu0 = 0, task1_cpu1 = 0;
    int task2_cpu0 = 0, task2_cpu1 = 0;
    int task3_cpu0 = 0, task3_cpu1 = 0;
    int total_switches = 0;
    
    printf("详细任务执行情况:\n");
    printf("%-8s %-8s %-8s %-8s %-10s %-10s\n", 
           "任务", "PID", "初始CPU", "最终CPU", "CPU切换", "迭代次数");
    printf("-" * 60 "\n");
    
    for (int i = 0; i < task_count; i++) {
        printf("%-8s %-8d %-8d %-8d %-10d %-10d\n",
               task_results[i].task_name,
               task_results[i].pid,
               task_results[i].initial_cpu,
               task_results[i].final_cpu,
               task_results[i].cpu_switches,
               task_results[i].iterations);
        
        total_switches += task_results[i].cpu_switches;
        
        // 统计任务类型分布
        if (task_results[i].task_id == 1) {
            task1_count++;
            if (task_results[i].final_cpu == 0) task1_cpu0++;
            else task1_cpu1++;
        } else if (task_results[i].task_id == 2) {
            task2_count++;
            if (task_results[i].final_cpu == 0) task2_cpu0++;
            else task2_cpu1++;
        } else if (task_results[i].task_id == 3) {
            task3_count++;
            if (task_results[i].final_cpu == 0) task3_cpu0++;
            else task3_cpu1++;
        }
    }
    
    printf("\n缓存感知性分析:\n");
    if (task1_count > 0) {
        printf("Task1: %d个任务, CPU0=%d, CPU1=%d ", 
               task1_count, task1_cpu0, task1_cpu1);
        float task1_locality = (float)max(task1_cpu0, task1_cpu1) / task1_count * 100;
        printf("(局部性: %.1f%%)\n", task1_locality);
    }
    
    if (task2_count > 0) {
        printf("Task2: %d个任务, CPU0=%d, CPU1=%d ", 
               task2_count, task2_cpu0, task2_cpu1);
        float task2_locality = (float)max(task2_cpu0, task2_cpu1) / task2_count * 100;
        printf("(局部性: %.1f%%)\n", task2_locality);
    }
    
    if (task3_count > 0) {
        printf("Task3: %d个任务, CPU0=%d, CPU1=%d ", 
               task3_count, task3_cpu0, task3_cpu1);
        float task3_locality = (float)max(task3_cpu0, task3_cpu1) / task3_count * 100;
        printf("(局部性: %.1f%%)\n", task3_locality);
    }
    
    printf("\n性能指标:\n");
    printf("总CPU切换次数: %d\n", total_switches);
    printf("平均每任务切换: %.2f\n", (float)total_switches / task_count);
    
    // 缓存感知性评估
    printf("\n缓存感知性评估:\n");
    if (total_switches < task_count * 0.5) {
        printf("✓ 优秀：大部分任务保持在同一CPU，缓存局部性良好\n");
    } else if (total_switches < task_count * 1.0) {
        printf("○ 良好：适度的CPU切换，缓存感知性有效\n");
    } else {
        printf("△ 需改进：频繁CPU切换，缓存局部性较差\n");
    }
    
    printf("=" * 60 "\n");
}

int max(int a, int b) {
    return a > b ? a : b;
}

int main() {
    printf("=" * 60 "\n");
    printf("        Yat_Casched 缓存感知性专项测试程序\n");
    printf("=" * 60 "\n");
    printf("版本: 2.0\n");
    printf("日期: 2025-06-16\n");
    printf("目标: 验证缓存感知调度器的CPU亲和性和局部性\n\n");
    
    printf("=== 系统信息 ===\n");
    printf("PID: %d\n", getpid());
    printf("CPU 数量: %d\n", get_nprocs());
    printf("当前CPU: %d\n", get_current_cpu());
    
    // 设置主进程为Yat_Casched调度策略
    printf("\n=== 设置调度策略 ===\n");
    if (set_yat_casched_policy(getpid()) != 0) {
        printf("❌ 无法设置主进程调度策略\n");
        return 1;
    }
    
    // 执行测试
    test_cache_aware_scheduling();
    test_concurrent_tasks();
    
    // 分析结果
    analyze_results();
    
    printf("\n🎯 缓存感知性测试完成！\n");
    printf("建议：分析上述结果，检查相同任务是否倾向于在同一CPU上运行\n");
    
    return 0;
}
