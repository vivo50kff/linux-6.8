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

    for (i = 0; i < NUM_TASKS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            CPU_ZERO(&mask);
            CPU_SET(i % NUM_CPUS, &mask);
            sched_setaffinity(0, sizeof(mask), &mask);
            struct sched_param param;
            param.sched_priority = 0;
            int ret = sched_setscheduler(0, SCHED_YAT_CASCHED, &param);
            if (ret == -1) {
                perror("sched_setscheduler failed");
                printf("ERROR: Unable to set SCHED_YAT_CASCHED for PID=%d\n", getpid());
                printf("Current policy: %d\n", sched_getscheduler(0));
                exit(1);
            } else {
                printf("SUCCESS: SCHED_YAT_CASCHED set for PID=%d\n", getpid());
                printf("Current policy: %d\n", sched_getscheduler(0));
            }
            for (int j = 0; j < 1000000; j++) {
                asm volatile("");
            }
            exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
        } else {
            exit(1);
        }
    }

    for (i = 0; i < NUM_TASKS; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}
