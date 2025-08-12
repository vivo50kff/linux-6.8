#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <limits.h>  // 添加这个头文件

#define NUM_TASKS 12
#define RUN_COUNT 100
#define SAFETY_MARGIN 1.1  // 10%安全裕度

// TacleBench内核测试任务集
const char *task_names[NUM_TASKS] = {
    "binarysearch", "bitcount", "bitonic", "bsort",
    "complex_updates", "cosf", "countnegative", "cubic",
    "deg2rad", "fac", "fft", "filterbank"
};

// 获取微秒精度时间差
long timeval_diff_us(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000000L + 
           (end->tv_usec - start->tv_usec);
}

// 计算标准差
double calculate_std_dev(long *data, int count, double mean) {
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = data[i] - mean;
        sum += diff * diff;
    }
    return sqrt(sum / count);
}

int main() {
    printf("=== TacleBench任务WCET统计分析 ===\n");
    printf("测量方法: %d次执行统计分析 + %.0f%%安全裕度\n\n", 
           RUN_COUNT, (SAFETY_MARGIN - 1) * 100);

    // 存储WCET结果用于代码生成（保持浮点精度）
    double wcet_results[NUM_TASKS];

    for (int i = 0; i < NUM_TASKS; i++) {
        long exec_times[RUN_COUNT];
        long total_time = 0;
        long max_time = 0;
        long min_time = LONG_MAX;
        
        printf("正在测量任务: %-20s", task_names[i]);
        fflush(stdout);

        // 执行多次测量
        for (int j = 0; j < RUN_COUNT; j++) {
            struct timeval start, end;
            pid_t pid;

            gettimeofday(&start, NULL);
            
            pid = fork();
            if (pid == 0) {
                // 子进程执行任务
                char path[128];
                snprintf(path, sizeof(path), 
                        "yat_test/yat_simple_test_env/initramfs/bin/%s", 
                        task_names[i]);
                execl(path, task_names[i], NULL);
                perror("execl failed");
                exit(127);
            } else if (pid > 0) {
                // 父进程等待并测量时间
                int status;
                waitpid(pid, &status, 0);
                gettimeofday(&end, NULL);

                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    long exec_time = timeval_diff_us(&start, &end);
                    exec_times[j] = exec_time;
                    total_time += exec_time;
                    
                    if (exec_time > max_time) max_time = exec_time;
                    if (exec_time < min_time) min_time = exec_time;
                } else {
                    fprintf(stderr, "\n警告: 任务 %s 运行失败\n", task_names[i]);
                    j--; // 重新运行这次测试
                }
            } else {
                perror("fork failed");
                exit(1);
            }
        }

        // 统计分析
        double avg_time_us = (double)total_time / RUN_COUNT;
        double std_dev = calculate_std_dev(exec_times, RUN_COUNT, avg_time_us);
        
        // WCET = max + 安全裕度，保持精确的浮点数
        double wcet_ms = (max_time * SAFETY_MARGIN) / 1000.0;
        wcet_results[i] = wcet_ms;
        
        printf(" -> 平均运行时间: %.3f ms\n", avg_time_us / 1000.0);
        printf("    最大时间: %.3f ms\n", max_time / 1000.0);
        printf("    最小时间: %.3f ms\n", min_time / 1000.0);
        printf("    标准差:   %.3f ms\n", std_dev / 1000.0);
        printf("    WCET:     %.3f ms\n", wcet_ms);
        printf("    变异系数: %.2f%%\n\n", (std_dev / avg_time_us) * 100);
    }

    printf("=== WCET测量完毕 ===\n\n");
    
    // 生成代码用的WCET数组（保持3位小数精度）
    printf("更新后的WCET数组（用于代码中）:\n");
    printf("double wcet[NUM_TASKS] = {\n    ");
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("%.3f", wcet_results[i]);
        if (i < NUM_TASKS - 1) {
            printf(", ");
            if ((i + 1) % 6 == 0) printf("\n    ");
        }
    }
    printf("\n};\n\n");

    // 也提供整数版本（向上取整）供需要的情况使用,避免内核运算浮点数
    printf("整数版本WCET数组（乘以精度  *1000）:\n");
    printf("int wcet_int[NUM_TASKS] = {\n    ");
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("%d", (int)(wcet_results[i] * 1000));
        if (i < NUM_TASKS - 1) {
            printf(", ");
            if ((i + 1) % 6 == 0) printf("\n    ");
        }
    }
    printf("\n};\n");
    
    return 0;
}