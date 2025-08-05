#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/time.h>
#include <sched.h>
#include <errno.h>

#define NUM_TASKS 12
#define SCHED_YAT_CASCHED 8  // YAT_CASCHED调度策略
#define PERIOD_MS 400
#define RUN_TIMES 10

const char *task_names[NUM_TASKS] = {
    "binarysearch",
    "bitcount",
    "bitonic",
    "bsort",
    "complex_updates",
    "cosf",
    "countnegative",
    "cubic",
    "deg2rad",
    "fac",
    "fft",
    "filterbank"
};

int set_yat_scheduler(pid_t pid, int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    if (sched_setscheduler(pid, SCHED_YAT_CASCHED, &param) == 0) {
        return 0;
    } else {
        fprintf(stderr, "[YAT调度器] 设置失败: %s\n", strerror(errno));
        return -1;
    }
}

long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 +
           (end->tv_usec - start->tv_usec) / 1000;
}

int main() {
    printf("=== 本地 TacleBench Kernel 12任务集周期测试 ===\n");
    // 父进程设置YAT_CASCHED调度策略
    if (set_yat_scheduler(0, 0) == 0) {
        printf("父进程PID=%d 成功设置为YAT_CASCHED调度策略\n", getpid());
    } else {
        printf("父进程PID=%d 设置YAT_CASCHED失败，使用默认策略\n", getpid());
    }
    struct timeval total_start, total_end;
    gettimeofday(&total_start, NULL);
    long task_total[NUM_TASKS] = {0};
    for (int round = 0; round < RUN_TIMES; round++) {
        printf("=== 第%d轮 ===\n", round+1);
        pid_t children[NUM_TASKS];
        struct timeval start_times[NUM_TASKS], end_times[NUM_TASKS];
        struct timeval round_start, round_end;
        gettimeofday(&round_start, NULL);
        for (int i = 0; i < NUM_TASKS; i++) {
            gettimeofday(&start_times[i], NULL);
            pid_t pid = fork();
            if (pid == 0) {
                printf("[Task %d] 启动: %s\n", i+1, task_names[i]);
                char path[256];
                snprintf(path, sizeof(path), "./yat_simple_test_env/initramfs/bin/%s", task_names[i]);
                execl(path, task_names[i], NULL);
                perror("execl失败");
                exit(127);
            } else if (pid > 0) {
                children[i] = pid;
            } else {
                perror("fork失败");
                exit(1);
            }
        }
        for (int i = 0; i < NUM_TASKS; i++) {
            int status;
            waitpid(children[i], &status, 0);
            gettimeofday(&end_times[i], NULL);
            long elapsed = timeval_diff_ms(&start_times[i], &end_times[i]);
            printf("[Task %d结束] 名字=%s, PID=%d, 执行时间=%ld ms\n",
                i+1, task_names[i], children[i], elapsed);
            task_total[i] += elapsed;
        }
        gettimeofday(&round_end, NULL);
        long round_elapsed = timeval_diff_ms(&round_start, &round_end);
        if (round_elapsed < PERIOD_MS) {
            usleep((PERIOD_MS - round_elapsed) * 1000);
        }
    }
    gettimeofday(&total_end, NULL);
    long total_elapsed = timeval_diff_ms(&total_start, &total_end);
    printf("=== 所有TacleBench Kernel任务完成 ===\n");
    printf("总运行时间: %ld ms\n", total_elapsed);
    printf("\n--- 每个任务的总时间和平均每轮执行时间（按任务ID排序） ---\n");
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("[Task %d] 名字=%s, 总时间=%ld ms, 平均每轮=%ld ms\n",
            i+1, task_names[i], task_total[i], task_total[i]/RUN_TIMES);
    }
    return 0;
}
