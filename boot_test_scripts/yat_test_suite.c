/*
 * Yat_Casched Test Suite
 *
 * 这是一个交互式的测试程序，用于在QEMU环境中验证Yat_Casched调度器的功能。
 * 它提供了多个测试用例，可以通过菜单选择执行。
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

// 确保与内核中的定义一致
#define SCHED_YAT_CASCHED 8 // 这里是与内核一致的定义

// --- 辅助函数 ---

// 打印头部信息
void print_header(const char* title) {
    printf("\n==================================================\n");
    printf("  %s\n", title);
    printf("==================================================\n");
}

// 消耗CPU的函数
void cpu_work(long iterations) {
    for (long i = 0; i < iterations; ++i) {
        volatile double x = 1.234 * 5.678;
    }
}

// 获取当前CPU核心
int get_current_cpu(void) {
    // syscall(SYS_getcpu, ...) 是最可靠的方式
    int cpu = -1;
    if (syscall(SYS_getcpu, &cpu, NULL, NULL) < 0) {
        perror("syscall(SYS_getcpu) failed");
        return -1;
    }
    return cpu;
}

// --- 测试用例 ---

// 测试1：基础调度验证
void test_basic_scheduling(void) {
    print_header("Test 1: Basic Scheduling Verification");
    int num_tasks = 5;
    pid_t pids[num_tasks];

    printf("Info: Starting %d tasks with SCHED_YAT_CASCHED policy.\n", num_tasks);
    printf("Info: Each task will do a short burst of work.\n");
    printf("Observe: Check if tasks are distributed across available CPUs.\n\n");

    for (int i = 0; i < num_tasks; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) { // 子进程
            struct sched_param param = { .sched_priority = 0 };
            if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
                perror("Error: sched_setscheduler failed");
                exit(1);
            }
            
            // 执行短时计算
            cpu_work(20000000L);

            int cpu = get_current_cpu();
            printf("  -> Task %d (PID: %d) finished on CPU %d\n", i, getpid(), cpu);
            exit(0);
        }
    }

    // 等待所有子进程结束
    for (int i = 0; i < num_tasks; ++i) {
        wait(NULL);
    }
    printf("\nTest 1 Finished.\n");
}

// 测试2：缓存亲和性测试
void test_cache_affinity(void) {

    print_header("Test 2: Cache Affinity (Same Process)");
    printf("Info: Using the same process, run, sleep, then run again.\n");
    printf("Observe: The process should be scheduled on the SAME CPU both times if the scheduler is idle.\n\n");

    struct sched_param param = { .sched_priority = 0 };
    if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
        perror("Error: sched_setscheduler failed");
        return;
    }

    // 第一次运行
    cpu_work(20000000L);
    int cpu1 = get_current_cpu();
    printf("  -> First run finished on CPU %d\n", cpu1);

    // 休眠，保证调度器有机会迁移
    sleep(1);

    // 第二次运行
    cpu_work(20000000L);
    int cpu2 = get_current_cpu();
    printf("  -> Second run finished on CPU %d\n", cpu2);

    printf("\nVerification: ");
    if (cpu1 == cpu2) {
        printf("[PASS] Both runs on CPU %d, cache affinity works.\n", cpu1);
    } else {
        printf("[FAIL] Runs on different CPUs (%d and %d), cache affinity failed.\n", cpu1, cpu2);
    }

    printf("\nTest 2 Finished.\n");
}

// 测试3：高负载下的负载均衡
void test_load_balancing(void) {
    print_header("Test 3: Load Balancing under High Load");
    int num_tasks = 12; // 创建比CPU核心数更多的任务
    pid_t pids[num_tasks];

    printf("Info: Starting %d tasks (more than available CPUs).\n", num_tasks);
    printf("Observe: Tasks should be distributed across all available cores over time.\n\n");

    for (int i = 0; i < num_tasks; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) { // 子进程
            struct sched_param param = { .sched_priority = 0 };
            if (sched_setscheduler(0, SCHED_YAT_CASCHED, &param) == -1) {
                perror("Error: sched_setscheduler failed");
                exit(1);
            }
            
            cpu_work(20000000L);

            int cpu = get_current_cpu();
            printf("  -> Task %d (PID: %d) finished on CPU %d\n", i, getpid(), cpu);
            exit(0);
        }
    }

    // 等待所有子进程结束
    for (int i = 0; i < num_tasks; ++i) {
        wait(NULL);
    }
    printf("\nTest 3 Finished.\n");
}


// --- 主函数和菜单 ---

void show_menu(void) {
    printf("\n--- Yat_Casched Interactive Test Suite ---\n");
    printf("1. Basic Scheduling Verification\n");
    printf("2. Cache Affinity Test\n");
    printf("3. Load Balancing Test\n");
    printf("------------------------------------------\n");
    printf("d. View scheduler debug info (dmesg)\n");
    printf("s. View process scheduling classes (ps)\n");
    printf("q. Quit\n");
    printf("------------------------------------------\n");
    printf("Enter your choice: ");
}

int main() {
    char choice[10];

    while (1) {
        show_menu();
        if (fgets(choice, sizeof(choice), stdin) == NULL) break;
        
        switch (choice[0]) {
            case '1':
                test_basic_scheduling();
                break;
            case '2':
                test_cache_affinity();
                break;
            case '3':
                test_load_balancing();
                break;
            case 'd':
                print_header("Kernel Log (dmesg)");
                system("dmesg | tail -n 20");
                break;
            case 's':
                print_header("Process Scheduling Classes");
                system("ps -eo pid,ppid,class,comm | grep -E 'COMMAND|test_suite|sh'");
                break;
            case 'q':
                printf("Exiting test suite.\n");
                return 0;
            default:
                printf("\nInvalid choice. Please try again.\n");
                break;
        }
    }

    return 0;
}
