/*
 * Yat_Casched v2.2 - Targeted Test Program
 *
 * 目的: 专门为 Yat_Casched v2.2 的核心调度逻辑创建测试场景，
 *      以验证 Benefit 和 Impact 机制是否按预期工作。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>


#define SCHED_YAT_CASCHED 8
#define NUM_TASKS 24
#define NUM_CPUS 8

int main() {
    cpu_set_t mask;
    pid_t pids[NUM_TASKS];
    int i;

    printf("Yat_Casched v2.2 用户态测试：创建 %d 个任务，分布到 %d 个核心。\n", NUM_TASKS, NUM_CPUS);

    for (i = 0; i < NUM_TASKS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            CPU_ZERO(&mask);
            CPU_SET(i % NUM_CPUS, &mask); // 轮流分配到8个核心
            if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
                perror("sched_setaffinity");
                exit(1);
            }
            struct sched_param param;
            param.sched_priority = 0;
            if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) != 0) {
                perror("sched_setscheduler");
                exit(1);
            }
            // 简单负载：循环或 sleep
            for (int j = 0; j < 1000000; j++) {
                asm volatile("");
            }
            exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
        } else {
            perror("fork");
            exit(1);
        }
    }

    // 等待所有任务结束
    for (i = 0; i < NUM_TASKS; i++) {
        waitpid(pids[i], NULL, 0);
    }

    printf("所有任务已完成。请用 cat /sys/kernel/debug/yat_casched/ready_pool 查看调度器中间状态。\n");
    printf("如需自动采集，可运行: cat /sys/kernel/debug/yat_casched/ready_pool > /tmp/yat_ready_pool.log\n");
    return 0;
}
