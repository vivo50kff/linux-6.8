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
#define NUM_THREADS 4
#define TEST_DURATION 30  // 测试时间：30秒
#define CACHE_SIZE (1024 * 1024)  // 1MB缓存测试数据

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
    for (int iter = 0; iter < 100; iter++) {  // 减少到100次迭代
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
        
        // 缓存密集型计算（减少计算量）
        volatile long sum = 0;
        for (int i = 0; i < (CACHE_SIZE/sizeof(int))/10; i++) {  // 减少计算量
            sum += cache_data[i] * cache_data[(i + 1) % (CACHE_SIZE/sizeof(int)/10)];
            cache_data[i] = sum % 1000;
        }
        
        // 模拟一些延迟
        usleep(100); // 减少到0.1ms
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
                i, cfs_results[i].cpu_switches, cfs_results[i].execution_time, cfs_affinity);
        
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
                i, yat_results[i].cpu_switches, yat_results[i].execution_time, yat_affinity);
    }
    
    fclose(fp);
    printf("Performance results saved to test_visualization/performance_results.csv\n");
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
    
    printf("=== Yat_Casched Performance Test ===\n");
    printf("Testing with %d threads, cache-intensive workload\n", NUM_THREADS);
    
    // 测试CFS调度器
    run_performance_test(0, "CFS", cfs_results);
    
    // 短暂休息
    sleep(2);
    
    // 测试Yat_Casched调度器
    run_performance_test(1, "Yat_Casched", yat_results);
    
    // 生成报告
    print_performance_comparison(cfs_results, yat_results);
    generate_csv_report(cfs_results, yat_results);
    
    printf("\nTest completed. Check test_visualization/ folder for results.\n");
    return 0;
}
