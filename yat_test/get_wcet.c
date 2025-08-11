#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/time.h>

#define NUM_TASKS 12
#define RUN_COUNT 100

// TacleBench内核测试任务集
const char *task_names[NUM_TASKS] = {
    "binarysearch", "bitcount", "bitonic", "bsort",
    "complex_updates", "cosf", "countnegative", "cubic",
    "deg2rad", "fac", "fft", "filterbank"
};

/**
 * @brief 计算两个timeval结构体之间的时间差（单位：微秒 us）
 * @param start 开始时间
 * @param end 结束时间
 * @return long 时间差（微秒）
 */
long timeval_diff_us(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000000L + (end->tv_usec - start->tv_usec);
}

int main() {
    printf("=== 开始为TacleBench任务集测量平均执行时间 (每个任务运行%d次) ===\n", RUN_COUNT);

    for (int i = 0; i < NUM_TASKS; i++) {
        long long total_exec_time_us = 0;
        printf("正在测量任务: %-20s", task_names[i]);
        fflush(stdout); // 确保立即输出提示信息

        for (int j = 0; j < RUN_COUNT; j++) {
            struct timeval start, end;
            pid_t pid;

            gettimeofday(&start, NULL); // 在fork之前记录开始时间

            pid = fork();

            if (pid == 0) {
                // --- 子进程 ---
                char path[128];
                snprintf(path, sizeof(path), "yat_test/yat_simple_test_env/initramfs/bin/%s", task_names[i]);

                // 使用execl执行测试任务，不带任何参数
                execl(path, task_names[i], NULL);

                // 如果execl返回，说明出错了
                perror("execl failed");
                exit(127); // 127是shell中找不到命令的约定退出码
            } else if (pid > 0) {
                // --- 父进程 ---
                int status;
                waitpid(pid, &status, 0); // 等待子进程结束
                gettimeofday(&end, NULL); // 子进程结束后记录结束时间

                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    // 仅在子进程正常成功退出时才累加时间
                    total_exec_time_us += timeval_diff_us(&start, &end);
                } else {
                    // 如果子进程异常退出，打印错误信息
                    fprintf(stderr, "\n警告: 任务 %s 的一次运行失败，状态码: %d\n", task_names[i], WEXITSTATUS(status));
                }
            } else {
                // --- fork 失败 ---
                perror("fork failed");
                exit(1);
            }
        }

        // 计算并打印平均时间
        double avg_time_ms = (double)total_exec_time_us / RUN_COUNT / 1000.0;
        printf(" -> 平均运行时间: %.3f ms\n", avg_time_ms);
    }

    printf("\n=== 所有任务测量完毕 ===\n");

    return 0;
}
