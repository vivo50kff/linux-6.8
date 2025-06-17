#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifndef SCHED_YAT_CASCHED
#define SCHED_YAT_CASCHED 7
#endif

int get_current_cpu() {
    return sched_getcpu();
}

void detailed_cache_test() {
    printf("============================================================\n");
    printf("Yat_Casched 详细缓存亲和性测试\n");
    printf("============================================================\n");
    
    pid_t pids[5];
    int i;
    
    // 创建5个相同的任务，观察它们是否被分配到相同的CPU
    for (i = 0; i < 5; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // 子进程
            struct sched_param param = {0};
            
            // 设置为我们的调度类
            if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
                printf("Task %d: ✗ Failed to set Yat_Casched policy: %s\n", 
                       i, strerror(errno));
                exit(1);
            }
            
            printf("Task %d: ✓ Set to Yat_Casched policy\n", i);
            
            // 记录CPU使用历史
            int cpu_history[10];
            int cpu_changes = 0;
            int last_cpu = -1;
            
            for (int iter = 0; iter < 10; iter++) {
                int current_cpu = get_current_cpu();
                cpu_history[iter] = current_cpu;
                
                if (last_cpu != -1 && last_cpu != current_cpu) {
                    cpu_changes++;
                    printf("Task %d: CPU切换 %d->%d (迭代 %d)\n", 
                           i, last_cpu, current_cpu, iter);
                }
                
                if (iter == 0) {
                    printf("Task %d: 初始CPU = %d\n", i, current_cpu);
                }
                
                last_cpu = current_cpu;
                
                // 执行一些计算任务
                volatile long sum = 0;
                for (long j = 0; j < 1000000; j++) {
                    sum += j;
                }
                
                usleep(100000); // 100ms
            }
            
            // 输出最终统计
            printf("Task %d 完成: CPU切换次数=%d, 最终CPU=%d\n", 
                   i, cpu_changes, cpu_history[9]);
            
            // 输出CPU使用历史
            printf("Task %d CPU历史: [", i);
            for (int h = 0; h < 10; h++) {
                printf("%d", cpu_history[h]);
                if (h < 9) printf(",");
            }
            printf("]\n");
            
            exit(0);
        } else if (pids[i] == -1) {
            printf("✗ Failed to fork task %d\n", i);
            exit(1);
        }
        
        // 父进程稍微等待，让子进程启动
        usleep(50000);
    }
    
    // 等待所有子进程完成
    for (i = 0; i < 5; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    printf("\n============================================================\n");
    printf("缓存亲和性分析:\n");
    printf("- 如果相同的任务被分配到相同的CPU，说明缓存亲和性工作正常\n");
    printf("- CPU切换次数越少，缓存亲和性效果越好\n");
    printf("============================================================\n");
}

void sequential_same_task_test() {
    printf("\n============================================================\n");
    printf("连续相同任务测试 - 验证缓存热度机制\n");
    printf("============================================================\n");
    
    // 连续创建多个完全相同的任务
    for (int round = 0; round < 3; round++) {
        printf("\n--- 第 %d 轮测试 ---\n", round + 1);
        
        for (int task = 0; task < 3; task++) {
            pid_t pid = fork();
            if (pid == 0) {
                // 子进程
                struct sched_param param = {0};
                
                if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
                    printf("同名任务 %d: ✗ 调度器设置失败\n", task);
                    exit(1);
                }
                
                int start_cpu = get_current_cpu();
                printf("同名任务 %d (轮次 %d): 开始于CPU %d\n", task, round + 1, start_cpu);
                
                // 模拟相同的工作负载
                volatile long result = 0;
                for (long i = 0; i < 2000000; i++) {
                    result += i * 2;
                }
                
                int end_cpu = get_current_cpu();
                if (start_cpu != end_cpu) {
                    printf("同名任务 %d (轮次 %d): CPU切换 %d->%d\n", 
                           task, round + 1, start_cpu, end_cpu);
                } else {
                    printf("同名任务 %d (轮次 %d): 保持在CPU %d\n", 
                           task, round + 1, start_cpu);
                }
                
                exit(0);
            } else if (pid > 0) {
                // 父进程等待子进程完成
                int status;
                waitpid(pid, &status, 0);
            } else {
                printf("✗ Fork失败\n");
            }
            
            // 短暂延迟
            usleep(200000);
        }
    }
}

int main() {
    printf("开始Yat_Casched调度器详细测试...\n\n");
    
    // 显示当前CPU信息
    printf("系统CPU信息:\n");
    printf("- 可用CPU核心数: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("- 当前进程CPU: %d\n\n", get_current_cpu());
    
    // 执行详细缓存测试
    detailed_cache_test();
    
    // 执行连续相同任务测试
    sequential_same_task_test();
    
    printf("\n============================================================\n");
    printf("测试完成！\n");
    printf("============================================================\n");
    
    return 0;
}
