#!/bin/bash

# 脚本：test_yat.c
# 功能：创建多个SCHED_YAT_CASCHED任务，并打印其运行的CPU核心

cat > test_yat.c <<EOF
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define SCHED_YAT_CASCHED 7
#define NUM_TASKS 5

void task_func(int id) {
    // 模拟一些计算密集型工作
    for (long i = 0; i < 100000000; ++i) {
        // 使用 volatile 防止编译器优化掉循环
        volatile int x = 0;
        x++;
    }
    // 使用系统调用获取当前CPU，比 sched_getcpu() 更底层
    int cpu = syscall(SYS_getcpu, NULL, NULL, NULL);
    printf("Task %d (PID: %d) finished on CPU %d\\n", id, getpid(), cpu);
    // 内核态打印，方便在dmesg中查看
    syscall(SYS_klog, 3, "Yat_Test: Task %d finished on CPU %d\\n", id, cpu);
}

int main() {
    pid_t pids[NUM_TASKS];

    printf("Starting %d tasks with SCHED_YAT_CASCHED policy...\\n", NUM_TASKS);

    for (int i = 0; i < NUM_TASKS; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // 子进程
            struct sched_param param;
            param.sched_priority = 0; // YAT_CASCHED不使用优先级

            if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
                perror("sched_setscheduler failed");
                exit(1);
            }
            
            task_func(i);
            exit(0);
        }
    }

    // 等待所有子进程结束
    for (int i = 0; i < NUM_TASKS; ++i) {
        wait(NULL);
    }

    printf("All tasks finished.\\n");
    return 0;
}
EOF

echo "test_yat.c created."
