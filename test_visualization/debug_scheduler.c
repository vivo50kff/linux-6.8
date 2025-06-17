#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>

#define SCHED_YAT_CASCHED 8

void test_scheduler_behavior() {
    struct sched_param param = {.sched_priority = 0};
    int original_policy = sched_getscheduler(0);
    
    printf("=== Scheduler Behavior Test ===\n");
    printf("Original scheduler: %d\n", original_policy);
    
    // 尝试设置为Yat_Casched
    printf("Attempting to set Yat_Casched scheduler...\n");
    if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
        printf("Failed to set Yat_Casched: %s\n", strerror(errno));
        return;
    }
    
    int current_policy = sched_getscheduler(0);
    printf("Current scheduler: %d\n", current_policy);
    
    if (current_policy == SCHED_YAT_CASCHED) {
        printf("Successfully using Yat_Casched!\n");
        
        // 做一些简单的工作并观察行为
        printf("Starting simple workload...\n");
        struct timeval start, end;
        gettimeofday(&start, NULL);
        
        volatile long sum = 0;
        for (int i = 0; i < 1000000; i++) {
            sum += i;
            if (i % 100000 == 0) {
                printf("Progress: %d/1000000, CPU: %d\n", i, sched_getcpu());
                usleep(1000); // 1ms sleep
            }
        }
        
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + 
                        (end.tv_usec - start.tv_usec) / 1000000.0;
        
        printf("Workload completed in %.3f seconds\n", elapsed);
        printf("Final sum: %ld\n", sum);
        
        // 恢复原始调度器
        param.sched_priority = 0;
        sched_setscheduler(0, SCHED_OTHER, &param);
        printf("Restored to CFS scheduler\n");
    } else {
        printf("Failed to activate Yat_Casched (policy=%d)\n", current_policy);
    }
}

int main() {
    test_scheduler_behavior();
    return 0;
}
