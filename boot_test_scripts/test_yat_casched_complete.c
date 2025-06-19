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

// 如果系统头文件中没有定义这些宏，手动定义
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif

#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif

// 定义Yat_Casched调度策略ID
#define SCHED_YAT_CASCHED 8

// 测试结果结构
struct test_result {
    int success_count;
    int total_count;
    int cpu_switches;
    int policy_switches;
};

// 全局测试结果
struct test_result global_result = {0, 0, 0, 0};

// 获取当前运行的CPU
int get_current_cpu() {
    return sched_getcpu();
}

// 设置调度策略为Yat_Casched
int set_yat_casched_policy(pid_t pid) {
    struct sched_param param;
    param.sched_priority = 0; // Yat_Casched是非实时调度器，优先级为0
    
    int ret = sched_setscheduler(pid, SCHED_YAT_CASCHED, &param);
    if (ret == -1) {
        printf("Failed to set Yat_Casched policy: %s (errno=%d)\n", strerror(errno), errno);
        return -1;
    }
    
    printf("✓ Successfully set Yat_Casched policy for PID %d\n", pid);
    return 0;
}

// 获取当前调度策略
const char* get_policy_name(int policy) {
    switch(policy) {
        case SCHED_OTHER: return "SCHED_OTHER";
        case SCHED_FIFO: return "SCHED_FIFO";
        case SCHED_RR: return "SCHED_RR";
        case SCHED_BATCH: return "SCHED_BATCH";
        case SCHED_IDLE: return "SCHED_IDLE";
        case SCHED_YAT_CASCHED: return "SCHED_YAT_CASCHED";
        default: return "UNKNOWN";
    }
}

void print_current_policy(pid_t pid) {
    int policy = sched_getscheduler(pid);
    if (policy == -1) {
        printf("Failed to get scheduler policy: %s\n", strerror(errno));
        return;
    }
    
    struct sched_param param;
    if (sched_getparam(pid, &param) == 0) {
        printf("PID %d: policy=%s (%d), priority=%d\n", 
               pid, get_policy_name(policy), policy, param.sched_priority);
    } else {
        printf("PID %d: policy=%s (%d)\n", pid, get_policy_name(policy), policy);
    }
}

// 读取 /proc/sched_debug 查找 yat_casched 相关信息
void check_kernel_scheduler() {
    printf("\n=== 内核调度器检查 ===\n");
    
    FILE *fp = fopen("/proc/sched_debug", "r");
    if (!fp) {
        printf("❌ 无法打开 /proc/sched_debug\n");
        return;
    }
    
    char line[1024];
    int found_yat = 0;
    int line_count = 0;
    
    printf("检查 /proc/sched_debug 中的 yat_casched 信息...\n");
    
    while (fgets(line, sizeof(line), fp) && line_count < 100) {
        line_count++;
        if (strstr(line, "yat_casched") || strstr(line, "YAT_CASCHED")) {
            printf("✓ 找到: %s", line);
            found_yat = 1;
        }
    }
    
    if (!found_yat) {
        printf("! 在 /proc/sched_debug 中未找到 yat_casched 相关信息\n");
        printf("显示前几行调度器信息：\n");
        rewind(fp);
        for (int i = 0; i < 10 && fgets(line, sizeof(line), fp); i++) {
            printf("  %s", line);
        }
    }
    
    fclose(fp);
}

// 系统调用测试
void test_syscall_interface() {
    printf("\n=== 系统调用接口测试 ===\n");
    
    pid_t pid = getpid();
    printf("测试进程 PID: %d\n", pid);
    
    // 测试1: 获取当前调度策略
    printf("\n1. 当前调度策略:\n");
    print_current_policy(pid);
    
    // 测试2: 尝试设置 Yat_Casched 策略
    printf("\n2. 尝试设置 Yat_Casched 策略:\n");
    if (set_yat_casched_policy(pid) == 0) {
        printf("✓ 策略设置成功！\n");
        print_current_policy(pid);
        global_result.success_count++;
    } else {
        printf("❌ 策略设置失败\n");
    }
    global_result.total_count++;
    
    // 测试3: 验证策略是否生效
    printf("\n3. 验证策略:\n");
    int current_policy = sched_getscheduler(pid);
    if (current_policy == SCHED_YAT_CASCHED) {
        printf("✓ 当前策略确实是 SCHED_YAT_CASCHED (%d)\n", current_policy);
        global_result.success_count++;
    } else {
        printf("❌ 当前策略不是 SCHED_YAT_CASCHED，而是 %s (%d)\n", 
               get_policy_name(current_policy), current_policy);
    }
    global_result.total_count++;
}

// 子进程测试
int child_process_test(int child_id) {
    printf("Child %d: PID=%d started\n", child_id, getpid());
    
    // 尝试设置调度策略
    if (set_yat_casched_policy(getpid()) == 0) {
        printf("Child %d: ✓ 成功设置 Yat_Casched 策略\n", child_id);
        
        // 测试 CPU 亲和性
        int last_cpu = -1;
        int cpu_switches = 0;
        
        for (int i = 0; i < 50; i++) {
            int current_cpu = get_current_cpu();
            
            if (last_cpu != -1 && last_cpu != current_cpu) {
                cpu_switches++;
                printf("Child %d: CPU switch %d->%d (iter %d)\n", 
                       child_id, last_cpu, current_cpu, i);
            }
            
            last_cpu = current_cpu;
            
            // 执行一些工作
            volatile int sum = 0;
            for (int j = 0; j < 50000; j++) {
                sum += j;
            }
            
            usleep(2000); // 2ms
        }
        
        printf("Child %d: 完成，CPU切换次数: %d\n", child_id, cpu_switches);
        return cpu_switches;
    } else {
        printf("Child %d: ❌ 无法设置 Yat_Casched 策略\n", child_id);
        return -1;
    }
}

// 多进程测试
void test_multiprocess() {
    printf("\n=== 多进程测试 ===\n");
    
    const int num_children = 3;
    pid_t children[num_children];
    
    for (int i = 0; i < num_children; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程
            int switches = child_process_test(i + 1);
            exit(switches >= 0 ? 0 : 1);
        } else if (pid > 0) {
            // 父进程
            children[i] = pid;
            printf("启动子进程 %d (PID: %d)\n", i + 1, pid);
        } else {
            printf("❌ 创建子进程失败: %s\n", strerror(errno));
            return;
        }
    }
    
    // 等待所有子进程完成
    int success_children = 0;
    for (int i = 0; i < num_children; i++) {
        int status;
        waitpid(children[i], &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            success_children++;
            printf("✓ 子进程 %d 成功完成\n", i + 1);
        } else {
            printf("❌ 子进程 %d 失败\n", i + 1);
        }
    }
    
    printf("多进程测试结果: %d/%d 成功\n", success_children, num_children);
    global_result.success_count += success_children;
    global_result.total_count += num_children;
}

// 线程测试
void* thread_test_func(void* arg) {
    int thread_id = *(int*)arg;
    pid_t tid = syscall(SYS_gettid);
    
    printf("Thread %d: TID=%d started\n", thread_id, tid);
    
    if (set_yat_casched_policy(tid) == 0) {
        printf("Thread %d: ✓ 成功设置 Yat_Casched 策略\n", thread_id);
        
        // 简单的工作负载测试
        int last_cpu = -1;
        int cpu_switches = 0;
        
        for (int i = 0; i < 30; i++) {
            int current_cpu = get_current_cpu();
            
            if (last_cpu != -1 && last_cpu != current_cpu) {
                cpu_switches++;
            }
            last_cpu = current_cpu;
            
            // 模拟工作
            volatile int sum = 0;
            for (int j = 0; j < 30000; j++) {
                sum += j;
            }
            
            usleep(1000); // 1ms
        }
        
        printf("Thread %d: CPU切换次数: %d\n", thread_id, cpu_switches);
        return (void*)(long)cpu_switches;
    } else {
        printf("Thread %d: ❌ 无法设置 Yat_Casched 策略\n", thread_id);
        return (void*)-1;
    }
}

// 多线程测试
void test_multithread() {
    printf("\n=== 多线程测试 ===\n");
    
    const int num_threads = 4;
    pthread_t threads[num_threads];
    int thread_ids[num_threads];
    
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i + 1;
        if (pthread_create(&threads[i], NULL, thread_test_func, &thread_ids[i]) != 0) {
            printf("❌ 创建线程 %d 失败\n", i + 1);
            continue;
        }
    }
    
    // 等待所有线程完成
    int success_threads = 0;
    for (int i = 0; i < num_threads; i++) {
        void* result;
        pthread_join(threads[i], &result);
        
        if ((long)result >= 0) {
            success_threads++;
            printf("✓ 线程 %d 成功完成\n", i + 1);
        } else {
            printf("❌ 线程 %d 失败\n", i + 1);
        }
    }
    
    printf("多线程测试结果: %d/%d 成功\n", success_threads, num_threads);
    global_result.success_count += success_threads;
    global_result.total_count += num_threads;
}

// 性能测试
void test_performance() {
    printf("\n=== 性能测试 ===\n");
    
    if (set_yat_casched_policy(getpid()) != 0) {
        printf("❌ 无法设置调度策略，跳过性能测试\n");
        return;
    }
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // 执行一些计算密集型任务
    volatile long long sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i * i;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long long ns = (end.tv_sec - start.tv_sec) * 1000000000LL + 
                   (end.tv_nsec - start.tv_nsec);
    
    printf("计算任务完成时间: %lld ns (%.2f ms)\n", ns, ns / 1000000.0);
    printf("结果检查: sum = %lld\n", sum);
    
    global_result.success_count++;
    global_result.total_count++;
}

// 打印最终测试报告
void print_test_report() {
    printf("\n");
    printf("================================================\n");
    printf("           Yat_Casched 测试报告\n");
    printf("================================================\n");
    printf("总测试项目: %d\n", global_result.total_count);
    printf("成功项目: %d\n", global_result.success_count);
    printf("失败项目: %d\n", global_result.total_count - global_result.success_count);
    printf("成功率: %.1f%%\n", 
           global_result.total_count > 0 ? 
           (100.0 * global_result.success_count / global_result.total_count) : 0.0);
    printf("================================================\n");
    
    if (global_result.success_count > 0) {
        printf("🎉 Yat_Casched 调度器工作正常！\n");
    } else {
        printf("❌ Yat_Casched 调度器可能未正常工作\n");
        printf("   这可能是因为:\n");
        printf("   1. 调度器未正确集成到内核中\n");
        printf("   2. 调度策略ID不匹配\n");
        printf("   3. 权限不足\n");
    }
    
    printf("\n建议的后续测试:\n");
    printf("- 查看 /proc/sched_debug 获取详细调度信息\n");
    printf("- 使用 'ps -eo pid,class,comm' 查看进程调度类\n");
    printf("- 检查内核日志中的调度器相关信息\n");
}

int main() {
    printf("===================================================\n");
    printf("        Yat_Casched 调度器完整测试程序\n");
    printf("===================================================\n");
    printf("版本: 1.1\n");
    printf("日期: 2025-06-16\n");
    printf("目标: 验证 Yat_Casched 调度器的功能\n");
    printf("\n");
    
    // 显示系统信息
    printf("=== 系统信息 ===\n");
    printf("PID: %d\n", getpid());
    printf("CPU 数量: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("页面大小: %ld bytes\n", sysconf(_SC_PAGESIZE));
    
    // 执行各项测试
    check_kernel_scheduler();
    test_syscall_interface();
    test_multiprocess();
    test_multithread();
    test_performance();
    
    // 打印测试报告
    print_test_report();
    
    return global_result.success_count > 0 ? 0 : 1;
}
