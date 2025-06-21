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
#define NUM_THREADS 8        // 8线程
#define TEST_ITER 8000      // 增加迭代次数8000

#define CACHE_SIZE (1024*1024) // 

typedef struct {
    int thread_id;
    int scheduler_type;  // 0: CFS, 1: Yat_Casched
    int cpu_switches;
    int cache_misses;
    double execution_time;
    int cpu_history[16];  // 记录在各CPU上的运行次数
} thread_data_t;

// 缓存密集型工作负载
void* cache_intensive_task(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    struct sched_param param = {.sched_priority = 0};
    int* cache_data = malloc(CACHE_SIZE);
    int last_cpu = -1;
    int current_cpu;
    struct timeval start_time, end_time;
    
    // 中等压力参数设置
    int compute_divisor = 10;    // 中等计算量
    int yield_interval = 15;    // 适中让步间隔  
    int yield_time = 10;        // 适中让步时间
    
    // 初始化缓存数据
    for (int i = 0; i < CACHE_SIZE/sizeof(int); i++) {
        cache_data[i] = i;
    }
    
    // 设置调度策略
    if (data->scheduler_type == 1) {
        if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
            printf("Thread %d: Failed to set Yat_Casched policy: %s\n", 
                   data->thread_id, strerror(errno));
            data->scheduler_type = 0; // 回退到CFS
        } else {
            printf("Thread %d: Using Yat_Casched scheduler\n", data->thread_id);
        }
    } else {
        sched_setscheduler(0, SCHED_OTHER, &param);
        printf("Thread %d: Using CFS scheduler\n", data->thread_id);
    }
    
    gettimeofday(&start_time, NULL);
    
    // 执行缓存密集型工作
    for (int iter = 0; iter < TEST_ITER; iter++) { 
        current_cpu = sched_getcpu();
        
        // 记录CPU切换
        if (last_cpu != -1 && last_cpu != current_cpu) {
            data->cpu_switches++;
        }
        
        // 记录CPU使用历史
        if (current_cpu >= 0 && current_cpu < 16) {
            data->cpu_history[current_cpu]++;
        }
        
        last_cpu = current_cpu;
        
        // 缓存密集型计算（根据压力级别调整）
        volatile long sum = 0;
        int compute_size = (CACHE_SIZE/sizeof(int))/compute_divisor;
        
        for (int i = 0; i < compute_size; i++) {
            sum += cache_data[i] * cache_data[(i + 1) % compute_size];
            cache_data[i] = sum % 1000;
        }
        
        // 根据压力级别决定让步频率和时间
        if (iter % yield_interval == 0) {
            usleep(yield_time);
        }
    }
    
    gettimeofday(&end_time, NULL);
    data->execution_time = (end_time.tv_sec - start_time.tv_sec) + 
                          (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    free(cache_data);
    return NULL;
}

// 运行性能测试
void run_performance_test(int scheduler_type, const char* scheduler_name, 
                         thread_data_t* results) {
    pthread_t threads[NUM_THREADS];
    
    printf("\n=== Testing %s Scheduler ===\n", scheduler_name);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        results[i].thread_id = i;
        results[i].scheduler_type = scheduler_type;
        results[i].cpu_switches = 0;
        results[i].cache_misses = 0;
        results[i].execution_time = 0.0;
        memset(results[i].cpu_history, 0, sizeof(results[i].cpu_history));
        
        if (pthread_create(&threads[i], NULL, cache_intensive_task, &results[i]) != 0) {
            printf("Failed to create thread %d\n", i);
            exit(1);
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}

// 生成CSV报告
void generate_csv_report(thread_data_t* cfs_results, thread_data_t* yat_results) {
    FILE* fp = fopen("performance_results.csv", "w");
    if (!fp) {
        perror("Failed to create CSV file");
        return;
    }
    
    fprintf(fp, "Scheduler,Thread_ID,CPU_Switches,Execution_Time,CPU_Affinity_Score\n");
    
    for (int i = 0; i < NUM_THREADS; i++) {
        // 计算CPU亲和性分数（最常用CPU的使用率）
        int max_cpu_usage = 0;
        int total_usage = 0;
        
        for (int j = 0; j < 16; j++) {
            total_usage += cfs_results[i].cpu_history[j];
            if (cfs_results[i].cpu_history[j] > max_cpu_usage) {
                max_cpu_usage = cfs_results[i].cpu_history[j];
            }
        }
        
        double cfs_affinity = total_usage > 0 ? (double)max_cpu_usage / total_usage : 0;
        
        fprintf(fp, "CFS,%d,%d,%.3f,%.3f\n", 
                i, cfs_results[i].cpu_switches, cfs_results[i].execution_time, 
                cfs_affinity);
        
        // Yat_Casched结果
        max_cpu_usage = 0;
        total_usage = 0;
        
        for (int j = 0; j < 16; j++) {
            total_usage += yat_results[i].cpu_history[j];
            if (yat_results[i].cpu_history[j] > max_cpu_usage) {
                max_cpu_usage = yat_results[i].cpu_history[j];
            }
        }
        
        double yat_affinity = total_usage > 0 ? (double)max_cpu_usage / total_usage : 0;
        
        fprintf(fp, "Yat_Casched,%d,%d,%.3f,%.3f\n", 
                i, yat_results[i].cpu_switches, yat_results[i].execution_time, 
                yat_affinity);
    }
    
    fclose(fp);
    printf("Performance results saved to performance_results.csv\n");
}

// 打印性能对比
void print_performance_comparison(thread_data_t* cfs_results, thread_data_t* yat_results) {
    int total_cfs_switches = 0, total_yat_switches = 0;
    double total_cfs_time = 0.0, total_yat_time = 0.0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_cfs_switches += cfs_results[i].cpu_switches;
        total_yat_switches += yat_results[i].cpu_switches;
        total_cfs_time += cfs_results[i].execution_time;
        total_yat_time += yat_results[i].execution_time;
    }
    
    printf("\n=== Performance Comparison ===\n");
    printf("Total CPU Switches:\n");
    printf("  CFS: %d\n", total_cfs_switches);
    printf("  Yat_Casched: %d\n", total_yat_switches);
    
    if (total_cfs_switches > 0) {
        double switch_reduction = (double)(total_cfs_switches - total_yat_switches) / total_cfs_switches * 100;
        printf("  Reduction: %.1f%%\n", switch_reduction);
    }
    
    printf("\nAverage Execution Time:\n");
    printf("  CFS: %.3f seconds\n", total_cfs_time / NUM_THREADS);
    printf("  Yat_Casched: %.3f seconds\n", total_yat_time / NUM_THREADS);
    
    if (total_cfs_time > 0) {
        double time_improvement = (total_cfs_time - total_yat_time) / total_cfs_time * 100;
        printf("  Performance improvement: %.1f%%\n", time_improvement);
    }
}

int main() {
    thread_data_t cfs_results[NUM_THREADS];
    thread_data_t yat_results[NUM_THREADS];
    int repeat = 5;
    
    printf("=== Yat_Casched Performance Test ===\n");
    printf("Testing with %d threads, cache-intensive workload\n", NUM_THREADS);
    
    double avg_cfs_switches = 0, avg_yat_switches = 0;
    double avg_cfs_time = 0, avg_yat_time = 0;
    
    for (int r = 0; r < repeat; r++) {
        printf("\n--- Round %d ---\n", r+1);
        
        // 测试CFS
        run_performance_test(0, "CFS", cfs_results);
        sleep(2);
        
        // 测试Yat_Casched
        run_performance_test(1, "Yat_Casched", yat_results);
        
        int total_cfs_switches = 0, total_yat_switches = 0;
        double total_cfs_time = 0.0, total_yat_time = 0.0;
        
        for (int i = 0; i < NUM_THREADS; i++) {
            total_cfs_switches += cfs_results[i].cpu_switches;
            total_yat_switches += yat_results[i].cpu_switches;
            total_cfs_time += cfs_results[i].execution_time;
            total_yat_time += yat_results[i].execution_time;
        }
        
        printf("Round %d Results:\n", r+1);
        printf("  CFS:    CPU Switches = %d, Avg Time = %.3f s\n", total_cfs_switches, total_cfs_time/NUM_THREADS);
        printf("  Yat_Casched: CPU Switches = %d, Avg Time = %.3f s\n", total_yat_switches, total_yat_time/NUM_THREADS);
        
        avg_cfs_switches += total_cfs_switches;
        avg_yat_switches += total_yat_switches;
        avg_cfs_time += total_cfs_time / NUM_THREADS;
        avg_yat_time += total_yat_time / NUM_THREADS;
    }
    
    avg_cfs_switches /= repeat;
    avg_yat_switches /= repeat;
    avg_cfs_time /= repeat;
    avg_yat_time /= repeat;
    
    printf("\n=== Average Performance over %d runs ===\n", repeat);
    printf("Total CPU Switches: CFS: %.1f, Yat_Casched: %.1f\n", avg_cfs_switches, avg_yat_switches);
    if (avg_cfs_switches > 0) {
        double switch_reduction = (avg_cfs_switches - avg_yat_switches) / avg_cfs_switches * 100;
        printf("  Reduction: %.1f%%\n", switch_reduction);
    }
    printf("Average Execution Time: CFS: %.3f s, Yat_Casched: %.3f s\n", avg_cfs_time, avg_yat_time);
    if (avg_cfs_time > 0) {
        double time_improvement = (avg_cfs_time - avg_yat_time) / avg_cfs_time * 100;
        printf("  Performance improvement: %.1f%%\n", time_improvement);
    }
    
    // 生成CSV报告
    generate_csv_report(cfs_results, yat_results);
    
    printf("\nTest completed. Check test_visualization/ folder for results.\n");
    return 0;
}
