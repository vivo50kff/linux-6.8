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
#include <sys/wait.h>

#define SCHED_YAT_CASCHED 8

// 验证真实的CPU调度
void verify_real_scheduling() {
    printf("============================================================\n");
    printf("验证 Yat_Casched 调度器的真实性测试\n");
    printf("============================================================\n");
    
    // 1. 验证当前是否运行在我们编译的内核上
    printf("1. 内核版本验证:\n");
    system("uname -r");
    
    // 2. 验证调度策略设置是否真的成功
    printf("\n2. 调度策略验证:\n");
    
    struct sched_param param;
    param.sched_priority = 0;
    
    printf("当前进程PID: %d\n", getpid());
    printf("当前调度策略: %d\n", sched_getscheduler(getpid()));
    
    // 尝试设置我们的调度策略
    int ret = sched_setscheduler(getpid(), SCHED_YAT_CASCHED, &param);
    if (ret == 0) {
        printf("✓ 成功设置 Yat_Casched 调度策略!\n");
        printf("新的调度策略: %d\n", sched_getscheduler(getpid()));
    } else {
        printf("✗ 设置 Yat_Casched 调度策略失败: %s\n", strerror(errno));
        printf("错误码: %d\n", errno);
        return;
    }
    
    // 3. 验证CPU亲和性操作
    printf("\n3. CPU亲和性验证:\n");
    printf("当前运行在CPU: %d\n", sched_getcpu());
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(getpid(), sizeof(cpuset), &cpuset) == 0) {
        printf("当前CPU亲和性掩码: ");
        for (int i = 0; i < 8; i++) {
            if (CPU_ISSET(i, &cpuset)) {
                printf("%d ", i);
            }
        }
        printf("\n");
    }
    
    // 4. 真实的CPU迁移测试
    printf("\n4. 真实CPU迁移测试:\n");
    
    for (int round = 0; round < 3; round++) {
        printf("轮次 %d:\n", round);
        
        for (int i = 0; i < 10; i++) {
            int current_cpu = sched_getcpu();
            printf("  迭代 %d: CPU %d", i, current_cpu);
            
            // 执行一些计算密集的任务
            volatile long sum = 0;
            for (long j = 0; j < 1000000; j++) {
                sum += j * j;
            }
            
            // 主动让出CPU
            sched_yield();
            
            int new_cpu = sched_getcpu();
            if (new_cpu != current_cpu) {
                printf(" -> CPU %d (迁移!)", new_cpu);
            }
            printf("\n");
            
            usleep(10000); // 10ms
        }
        
        printf("轮次 %d 完成\n\n", round);
        sleep(1);
    }
    
    // 5. 多进程并发测试
    printf("5. 多进程并发调度测试:\n");
    
    for (int proc = 0; proc < 4; proc++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 子进程
            // 设置调度策略
            if (sched_setscheduler(getpid(), SCHED_YAT_CASCHED, &param) != 0) {
                printf("子进程 %d: 设置调度策略失败\n", proc);
                exit(1);
            }
            
            printf("子进程 %d (PID %d): 开始在CPU %d\n", proc, getpid(), sched_getcpu());
            
            // 执行计算任务
            for (int i = 0; i < 20; i++) {
                int cpu = sched_getcpu();
                volatile long sum = 0;
                for (long j = 0; j < 500000; j++) {
                    sum += j;
                }
                
                if (i % 5 == 0) {
                    printf("子进程 %d: 迭代 %d, CPU %d\n", proc, i, cpu);
                }
                
                usleep(50000); // 50ms
            }
            
            printf("子进程 %d: 完成在CPU %d\n", proc, sched_getcpu());
            exit(0);
            
        } else if (pid < 0) {
            printf("创建子进程失败\n");
        }
        
        usleep(100000); // 100ms间隔创建进程
    }
    
    // 等待所有子进程完成
    printf("等待所有子进程完成...\n");
    for (int i = 0; i < 4; i++) {
        int status;
        wait(&status);
    }
    
    printf("\n============================================================\n");
    printf("验证完成! 如果看到CPU迁移，说明调度器真实工作!\n");
    printf("============================================================\n");
}

int main() {
    verify_real_scheduling();
    return 0;
}
