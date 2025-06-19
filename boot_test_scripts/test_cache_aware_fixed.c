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
#include <sys/sysinfo.h>

// 定义Yat_Casched调度策略ID
#define SCHED_YAT_CASCHED 8

// 简单的max宏定义
#define max(a,b) ((a) > (b) ? (a) : (b))

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

// 模拟计算密集型任务（访问内存以测试缓存效果）
void compute_intensive_task(int task_id, const char* task_name, int duration_ms) {
    int initial_cpu = get_current_cpu();
    int current_cpu = initial_cpu;
    int last_cpu = initial_cpu;
    int cpu_switches = 0;
    int iterations = 0;
    
    printf("Task %d (%s): Starting on CPU %d\n", task_id, task_name, initial_cpu);
    
    // 分配一些内存用于测试缓存局部性
    const int array_size = 1024 * 1024; // 1MB
    volatile int *data = malloc(array_size * sizeof(int));
    if (!data) {
        printf("Task %d: Memory allocation failed\n", task_id);
        return;
    }
    
    // 初始化数据
    for (int i = 0; i < array_size; i++) {
        data[i] = i;
    }
    
    clock_t start_time = clock();
    clock_t end_time = start_time + (duration_ms * CLOCKS_PER_SEC) / 1000;
    
    while (clock() < end_time) {
        // 执行内存访问密集的操作
        volatile long sum = 0;
        for (int i = 0; i < array_size / 10; i++) {
            sum += data[i % array_size];
        }
        
        // 检查CPU切换
        current_cpu = get_current_cpu();
        if (current_cpu != last_cpu) {
            cpu_switches++;
            printf("Task %d (%s): CPU switch %d->%d at iteration %d\n", 
                   task_id, task_name, last_cpu, current_cpu, iterations);
            last_cpu = current_cpu;
        }
        
        iterations++;
        
        // 短暂休眠让出CPU
        usleep(1000); // 1ms
    }
    
    int final_cpu = get_current_cpu();
    
    printf("Task %d (%s): Completed - Initial CPU: %d, Final CPU: %d, Switches: %d, Iterations: %d\n", 
           task_id, task_name, initial_cpu, final_cpu, cpu_switches, iterations);
    
    // 保存结果
    if (task_count < 20) {
        task_results[task_count].task_id = task_id;
        task_results[task_count].initial_cpu = initial_cpu;
        task_results[task_count].final_cpu = final_cpu;
        task_results[task_count].cpu_switches = cpu_switches;
        task_results[task_count].iterations = iterations;
        task_results[task_count].pid = getpid();
        strncpy(task_results[task_count].task_name, task_name, 31);
        task_results[task_count].task_name[31] = '\0';
        task_count++;
    }
    
    free((void*)data);
}

// 创建子进程执行任务
int create_task_process(int task_id, const char* task_name, int duration_ms) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // 子进程
        if (set_yat_casched_policy(getpid()) == 0) {
            compute_intensive_task(task_id, task_name, duration_ms);
        }
        exit(0);
    } else if (pid > 0) {
        // 父进程
        return pid;
    } else {
        printf("Fork failed for task %d\n", task_id);
        return -1;
    }
}

// 等待所有子进程完成
void wait_for_all_tasks(pid_t* pids, int count) {
    for (int i = 0; i < count; i++) {
        if (pids[i] > 0) {
            int status;
            waitpid(pids[i], &status, 0);
            printf("Task process %d completed\n", pids[i]);
        }
    }
}

// 分析测试结果
void analyze_results() {
    printf("\n============================================================\n");
    printf("Yat_Casched 缓存感知性测试结果分析\n");
    printf("============================================================\n");
    
    printf("CPU核心数: %d\n", get_nprocs());
    printf("总任务数: %d\n", task_count);
    
    if (task_count == 0) {
        printf("没有收集到任务数据\n");
        return;
    }
    
    // 分析每个任务的运行情况
    printf("\n任务详细信息:\n");
    printf("------------------------------------------------------------\n");
    printf("TaskID   TaskName      PID    InitCPU FinalCPU Switches Iterations\n");
    printf("------------------------------------------------------------\n");
    
    int total_switches = 0;
    int total_iterations = 0;
    
    for (int i = 0; i < task_count; i++) {
        printf("%-8d %-12s %-7d %-7d %-8d %-8d %-10d\n",
               task_results[i].task_id,
               task_results[i].task_name,
               task_results[i].pid,
               task_results[i].initial_cpu,
               task_results[i].final_cpu,
               task_results[i].cpu_switches,
               task_results[i].iterations);
        
        total_switches += task_results[i].cpu_switches;
        total_iterations += task_results[i].iterations;
    }
    
    // 分析缓存亲和性（同类任务是否倾向于在同一CPU运行）
    printf("\n缓存亲和性分析:\n");
    printf("------------------------------------------------------------\n");
    
    // 统计task1和task2的CPU分布
    int task1_count = 0, task2_count = 0;
    int task1_cpu0 = 0, task1_cpu1 = 0, task1_others = 0;
    int task2_cpu0 = 0, task2_cpu1 = 0, task2_others = 0;
    
    for (int i = 0; i < task_count; i++) {
        if (strstr(task_results[i].task_name, "task1")) {
            task1_count++;
            if (task_results[i].final_cpu == 0) task1_cpu0++;
            else if (task_results[i].final_cpu == 1) task1_cpu1++;
            else task1_others++;
        } else if (strstr(task_results[i].task_name, "task2")) {
            task2_count++;
            if (task_results[i].final_cpu == 0) task2_cpu0++;
            else if (task_results[i].final_cpu == 1) task2_cpu1++;
            else task2_others++;
        }
    }
    
    if (task1_count > 0) {
        float task1_locality = (float)max(task1_cpu0, task1_cpu1) / task1_count * 100;
        printf("Task1类任务: 总数=%d, CPU0=%d, CPU1=%d, 其他=%d, 局部性=%.1f%%\n",
               task1_count, task1_cpu0, task1_cpu1, task1_others, task1_locality);
    }
    
    if (task2_count > 0) {
        float task2_locality = (float)max(task2_cpu0, task2_cpu1) / task2_count * 100;
        printf("Task2类任务: 总数=%d, CPU0=%d, CPU1=%d, 其他=%d, 局部性=%.1f%%\n",
               task2_count, task2_cpu0, task2_cpu1, task2_others, task2_locality);
    }
    
    // 整体统计
    printf("\n整体统计:\n");
    printf("------------------------------------------------------------\n");
    printf("平均CPU切换次数: %.2f\n", (float)total_switches / task_count);
    printf("平均迭代次数: %.2f\n", (float)total_iterations / task_count);
    
    // 缓存感知效果评估
    float avg_switches = (float)total_switches / task_count;
    printf("\n缓存感知效果评估:\n");
    if (avg_switches < 2.0) {
        printf("✓ 优秀 - CPU切换次数很少，缓存局部性很好\n");
    } else if (avg_switches < 5.0) {
        printf("✓ 良好 - CPU切换次数较少，缓存局部性较好\n");
    } else {
        printf("⚠ 一般 - CPU切换次数较多，缓存局部性有待改善\n");
    }
    
    printf("============================================================\n");
}

int main() {
    printf("============================================================\n");
    printf("Yat_Casched 缓存感知调度器专项测试\n");
    printf("============================================================\n");
    
    printf("开始缓存感知性测试...\n");
    
    pid_t task_pids[16];
    int pid_count = 0;
    
    // 第一轮：创建4个task1类型的任务
    printf("\n第一轮：创建task1类型任务...\n");
    for (int i = 0; i < 4; i++) {
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "task1_%d", i);
        pid_t pid = create_task_process(i, task_name, 2000); // 2秒
        if (pid > 0) {
            task_pids[pid_count++] = pid;
        }
        usleep(200000); // 200ms间隔
    }
    
    // 等待第一轮完成
    wait_for_all_tasks(task_pids, pid_count);
    printf("第一轮完成\n");
    
    sleep(1); // 稍作休息
    
    // 第二轮：创建4个task2类型的任务
    printf("\n第二轮：创建task2类型任务...\n");
    pid_count = 0;
    for (int i = 0; i < 4; i++) {
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "task2_%d", i);
        pid_t pid = create_task_process(i + 10, task_name, 2000); // 2秒
        if (pid > 0) {
            task_pids[pid_count++] = pid;
        }
        usleep(200000); // 200ms间隔
    }
    
    // 等待第二轮完成
    wait_for_all_tasks(task_pids, pid_count);
    printf("第二轮完成\n");
    
    sleep(1); // 稍作休息
    
    // 第三轮：混合创建task1和task2
    printf("\n第三轮：混合创建任务...\n");
    pid_count = 0;
    for (int i = 0; i < 4; i++) {
        // 创建task1
        char task_name1[32];
        snprintf(task_name1, sizeof(task_name1), "task1_%d", i + 10);
        pid_t pid1 = create_task_process(i + 20, task_name1, 1500);
        if (pid1 > 0) {
            task_pids[pid_count++] = pid1;
        }
        
        usleep(100000); // 100ms
        
        // 创建task2
        char task_name2[32];
        snprintf(task_name2, sizeof(task_name2), "task2_%d", i + 10);
        pid_t pid2 = create_task_process(i + 30, task_name2, 1500);
        if (pid2 > 0) {
            task_pids[pid_count++] = pid2;
        }
        
        usleep(100000); // 100ms
    }
    
    // 等待第三轮完成
    wait_for_all_tasks(task_pids, pid_count);
    printf("第三轮完成\n");
    
    // 分析结果
    analyze_results();
    
    printf("\n测试完成！\n");
    return 0;
}
