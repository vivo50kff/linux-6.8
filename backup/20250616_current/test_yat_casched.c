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

// 如果系统头文件中没有定义这些宏，手动定义
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif

#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif

// 定义Yat_Casched调度策略ID
// 需要根据实际内核中定义的值来设置
#define SCHED_YAT_CASCHED 8

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
        printf("Failed to set Yat_Casched policy: %s\n", strerror(errno));
        printf("Error code: %d\n", errno);
        return -1;
    }
    
    printf("Successfully set Yat_Casched policy for PID %d\n", pid);
    return 0;
}

// 获取当前调度策略
void print_current_policy(pid_t pid) {
    int policy = sched_getscheduler(pid);
    const char* policy_name;
    
    switch(policy) {
        case SCHED_OTHER:
            policy_name = "SCHED_OTHER";
            break;
        case SCHED_FIFO:
            policy_name = "SCHED_FIFO";
            break;
        case SCHED_RR:
            policy_name = "SCHED_RR";
            break;
        case SCHED_BATCH:
            policy_name = "SCHED_BATCH";
            break;
        case SCHED_IDLE:
            policy_name = "SCHED_IDLE";
            break;
        case SCHED_YAT_CASCHED:
            policy_name = "SCHED_YAT_CASCHED";
            break;
        default:
            policy_name = "UNKNOWN";
            break;
    }
    
    printf("PID %d current policy: %s (%d)\n", pid, policy_name, policy);
}

// 测试任务：记录在哪个CPU上运行
void* test_task(void* arg) {
    int task_id = *(int*)arg;
    int last_cpu = -1;
    int current_cpu;
    int cpu_switches = 0;
    int iterations = 100;
    
    printf("Task %d started\n", task_id);
    
    // 设置当前线程为Yat_Casched调度策略
    pid_t tid = syscall(SYS_gettid);
    if (set_yat_casched_policy(tid) == 0) {
        printf("Task %d: Successfully set to Yat_Casched policy\n", task_id);
        print_current_policy(tid);
    } else {
        printf("Task %d: Failed to set Yat_Casched policy, using default\n", task_id);
        print_current_policy(tid);
    }
    
    for (int i = 0; i < iterations; i++) {
        current_cpu = get_current_cpu();
        
        if (last_cpu != -1 && last_cpu != current_cpu) {
            cpu_switches++;
            printf("Task %d: CPU switch from %d to %d (iteration %d)\n", 
                   task_id, last_cpu, current_cpu, i);
        }
        
        if (i % 20 == 0) {
            printf("Task %d: Running on CPU %d (iteration %d)\n", 
                   task_id, current_cpu, i);
        }
        
        last_cpu = current_cpu;
        
        // 执行一些计算工作
        volatile int sum = 0;
        for (int j = 0; j < 100000; j++) {
            sum += j;
        }
        
        // 短暂休眠，让调度器有机会调度
        usleep(1000); // 1ms
    }
    
    printf("Task %d completed: %d CPU switches in %d iterations\n", 
           task_id, cpu_switches, iterations);
    
    return NULL;
}

int main() {
    printf("=== Yat_Casched Scheduler Test ===\n");
    printf("PID: %d\n", getpid());
    
    // 打印系统信息
    printf("Number of CPUs: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    
    // 测试主进程的调度策略设置
    printf("\n--- Testing main process ---\n");
    print_current_policy(getpid());
    
    if (set_yat_casched_policy(getpid()) == 0) {
        printf("Main process successfully set to Yat_Casched\n");
        print_current_policy(getpid());
    } else {
        printf("Main process failed to set Yat_Casched policy\n");
        printf("This might indicate that the scheduler is not available\n");
        printf("Continuing with thread test...\n");
    }
    
    // 创建多个线程测试
    printf("\n--- Creating test threads ---\n");
    const int num_threads = 4;
    pthread_t threads[num_threads];
    int thread_ids[num_threads];
    
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i + 1;
        int ret = pthread_create(&threads[i], NULL, test_task, &thread_ids[i]);
        if (ret != 0) {
            printf("Failed to create thread %d: %s\n", i, strerror(ret));
            exit(1);
        }
        
        // 错开线程启动时间
        usleep(10000); // 10ms
    }
    
    // 等待所有线程完成
    printf("\n--- Waiting for threads to complete ---\n");
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n=== Test completed ===\n");
    printf("If Yat_Casched is working correctly, you should see:\n");
    printf("1. Tasks successfully setting Yat_Casched policy\n");
    printf("2. Tasks tending to stay on the same CPU (fewer CPU switches)\n");
    printf("3. When tasks do switch CPUs, they should prefer their last CPU\n");
    
    return 0;
}
