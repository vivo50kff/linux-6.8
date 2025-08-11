#define _GNU_SOURCE
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
#define PERIOD_MS 400       // 周期时间
#define RUN_TIMES 10        // 运行轮次
#define TASK_COPIES 10      // 每个任务并行运行的副本数
#define REPEAT_COUNT 100    // 每个任务重复执行次数

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

int set_cpu_affinity(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    return sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 +
           (end->tv_usec - start->tv_usec) / 1000;
}

int main() {
    printf("=== 高负载 TacleBench Kernel %d任务集周期测试 ===\n", NUM_TASKS);
    
    // 父进程设置YAT_CASCHED调度策略，子进程将自动继承
    // if (set_yat_scheduler(0, 0) == 0) {
    //     printf("父进程PID=%d 成功设置为YAT_CASCHED调度策略\n", getpid());
    // } else {
    //     printf("父进程PID=%d 设置YAT_CASCHED失败，使用默认策略\n", getpid());
    // }
    
    
    struct timeval total_start, total_end;
    gettimeofday(&total_start, NULL);
    long task_total[NUM_TASKS] = {0};
    
    for (int round = 0; round < RUN_TIMES; round++) {
        printf("=== 第%d轮 ===\n", round+1);
        
        int total_processes = NUM_TASKS * TASK_COPIES;
        pid_t *children = malloc(total_processes * sizeof(pid_t));
        struct timeval *start_times = malloc(total_processes * sizeof(struct timeval));
        struct timeval *end_times = malloc(total_processes * sizeof(struct timeval));
        struct timeval round_start, round_end;
        
        gettimeofday(&round_start, NULL);
        
        int process_idx = 0;
        for (int i = 0; i < NUM_TASKS; i++) {
            for (int copy = 0; copy < TASK_COPIES; copy++) {
                gettimeofday(&start_times[process_idx], NULL);
                pid_t pid = fork();
                
                if (pid == 0) {
                    // 子进程：继承父进程的YAT_CASCHED调度策略
                    // 只设置CPU亲和性增加缓存竞争
                    if (set_cpu_affinity(i + copy) != 0) {
                        fprintf(stderr, "设置CPU亲和性失败\n");
                    }
                    
                    printf("[Task %d] 启动: %s (副本%d)\n", i+1, task_names[i], copy+1);
                    
                    // 创建执行脚本来重复运行任务
                    char script_path[256];
                    snprintf(script_path, sizeof(script_path), "/tmp/task_%s_copy%d_%d.sh", 
                             task_names[i], copy, getpid());
                    
                    FILE *script = fopen(script_path, "w");
                    if (script) {
                        fprintf(script, "#!/bin/bash\n");
                        char task_path[256];
                        snprintf(task_path, sizeof(task_path), 
                                "./yat_simple_test_env/initramfs/bin/%s", task_names[i]);
                        
                        fprintf(script, "for i in $(seq 1 %d); do\n", REPEAT_COUNT);
                        fprintf(script, "    %s > /dev/null 2>&1\n", task_path);
                        fprintf(script, "done\n");
                        fclose(script);
                        
                        chmod(script_path, 0755);
                        execl("/bin/bash", "bash", script_path, NULL);
                    }
                    
                    // 备用方案：直接执行
                    char path[256];
                    snprintf(path, sizeof(path), "./yat_simple_test_env/initramfs/bin/%s", task_names[i]);
                    execl(path, task_names[i], NULL);
                    perror("execl失败");
                    exit(127);
                    
                } else if (pid > 0) {
                    children[process_idx] = pid;
                    process_idx++;
                } else {
                    perror("fork失败");
                    exit(1);
                }
            }
        }
        
        // 等待所有进程完成
        process_idx = 0;
        for (int i = 0; i < NUM_TASKS; i++) {
            long task_round_total = 0;
            
            for (int copy = 0; copy < TASK_COPIES; copy++) {
                int status;
                waitpid(children[process_idx], &status, 0);
                gettimeofday(&end_times[process_idx], NULL);
                long elapsed = timeval_diff_ms(&start_times[process_idx], &end_times[process_idx]);
                task_round_total += elapsed;
                process_idx++;
            }
            
            // 取平均值作为该任务的执行时间
            long avg_elapsed = task_round_total / TASK_COPIES;
            printf("[Task %d结束] 名字=%s, 平均执行时间=%ld ms\n",
                i+1, task_names[i], avg_elapsed);
            task_total[i] += avg_elapsed;
        }
        
        gettimeofday(&round_end, NULL);
        long round_elapsed = timeval_diff_ms(&round_start, &round_end);
        
        // 周期控制
        if (round_elapsed < PERIOD_MS) {
            usleep((PERIOD_MS - round_elapsed) * 1000);
        }
        
        // 清理临时脚本
        system("rm -f /tmp/task_*_copy*.sh");
        
        free(children);
        free(start_times);
        free(end_times);
    }
    
    gettimeofday(&total_end, NULL);
    long total_elapsed = timeval_diff_ms(&total_start, &total_end);
    
    printf("=== 所有高负载TacleBench Kernel任务完成 ===\n");
    printf("总运行时间: %ld ms\n", total_elapsed);
    printf("\n--- 每个任务的总时间和平均每轮执行时间（按任务ID排序） ---\n");
    
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("[Task %d] 名字=%s, 总时间=%ld ms, 平均每轮=%ld ms\n",
            i+1, task_names[i], task_total[i], task_total[i]/RUN_TIMES);
    }
    
    return 0;
}