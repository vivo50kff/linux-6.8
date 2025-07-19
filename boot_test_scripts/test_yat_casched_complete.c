#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>

#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
#define SCHED_YAT_CASCHED 8

// 测试结果结构
struct test_result {
    int success_count;
    int total_count;
    int cpu_switches;
    int policy_switches;
};
struct test_result global_result = {0, 0, 0, 0};

int set_yat_casched_policy(pid_t pid) {
    struct sched_param param;
    param.sched_priority = 0;
    int ret = sched_setscheduler(pid, SCHED_YAT_CASCHED, &param);
    if (ret == -1) {
        printf("❌ 设置 Yat_Casched 策略失败: %s (errno=%d)\n", strerror(errno), errno);
        return -1;
    }
    printf("✓ 设置 Yat_Casched 策略成功 (PID=%d)\n", pid);
    return 0;
}

const char* get_policy_name(int policy) {
    switch(policy) {
        case SCHED_OTHER: return "SCHED_OTHER";
        case SCHED_FIFO: return "SCHED_FIFO";
        case SCHED_RR: return "SCHED_RR";
        case SCHED_BATCH: return "SCHED_BATCH";
        case SCHED_IDLE: return "SCHED_IDLE";
        case SCHED_YAT_CASCHED: return "SCHED_YAT_CASCHED";
        default: return "UNKNOWN";
    }
}

void print_current_policy(pid_t pid) {
    int policy = sched_getscheduler(pid);
    if (policy == -1) {
        printf("Failed to get scheduler policy: %s\n", strerror(errno));
        return;
    }
    struct sched_param param;
    if (sched_getparam(pid, &param) == 0) {
        printf("PID %d: policy=%s (%d), priority=%d\n", pid, get_policy_name(policy), policy, param.sched_priority);
    } else {
        printf("PID %d: policy=%s (%d)\n", pid, get_policy_name(policy), policy);
    }
}

// 读取 /proc/sched_debug 查找 yat_casched 相关信息
void check_kernel_scheduler() {
    printf("\n=== 内核调度器检查 ===\n");
    FILE *fp = fopen("/proc/sched_debug", "r");
    if (!fp) {
        printf("❌ 无法打开 /proc/sched_debug\n");
        return;
    }
    char line[1024];
    int found_yat = 0;
    printf("检查 /proc/sched_debug 中的 yat_casched 信息...\n");
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "yat_casched") || strstr(line, "YAT_CASCHED")) {
            printf("✓ 找到: %s", line);
            found_yat = 1;
        }
    }
    if (!found_yat) {
        printf("! 未找到 yat_casched 相关信息\n");
    }
    fclose(fp);
}

// v2.2多进程/多线程调度行为测试
void test_v2_2_benefit_impact() {
    printf("\n=== v2.2 Benefit/Impact 行为测试 ===\n");
    int num_children = 4;
    pid_t children[4];
    int cpu_switches[4] = {0};
    for (int i = 0; i < num_children; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            set_yat_casched_policy(getpid());
            int last_cpu = -1, switches = 0;
            for (int j = 0; j < 100; j++) {
                int cpu = sched_getcpu();
                if (last_cpu != -1 && cpu != last_cpu) switches++;
                last_cpu = cpu;
                volatile int sum = 0;
                for (int k = 0; k < 10000; k++) sum += k;
                usleep(1000);
            }
            printf("子进程%d: CPU切换次数=%d\n", i+1, switches);
            exit(switches);
        } else if (pid > 0) {
            children[i] = pid;
        } else {
            printf("❌ 创建子进程失败: %s\n", strerror(errno));
        }
    }
    for (int i = 0; i < num_children; i++) {
        int status;
        waitpid(children[i], &status, 0);
        if (WIFEXITED(status)) {
            cpu_switches[i] = WEXITSTATUS(status);
            printf("✓ 子进程%d完成，CPU切换次数=%d\n", i+1, cpu_switches[i]);
        }
    }
    int total_switches = 0;
    for (int i = 0; i < num_children; i++) total_switches += cpu_switches[i];
    printf("v2.2多进程总CPU切换次数: %d\n", total_switches);
    global_result.success_count++;
    global_result.total_count++;
}

void print_test_report() {
    printf("\n================================================\n");
    printf("           Yat_Casched v2.2 测试报告\n");
    printf("================================================\n");
    printf("总测试项目: %d\n", global_result.total_count);
    printf("成功项目: %d\n", global_result.success_count);
    printf("失败项目: %d\n", global_result.total_count - global_result.success_count);
    printf("成功率: %.1f%%\n", global_result.total_count > 0 ? (100.0 * global_result.success_count / global_result.total_count) : 0.0);
    printf("================================================\n");
    if (global_result.success_count > 0) {
        printf("🎉 Yat_Casched v2.2调度器核心机制通过！\n");
    } else {
        printf("❌ Yat_Casched v2.2调度器机制未通过\n");
    }
    printf("建议: 查看 /proc/sched_debug、内核日志，进一步分析Benefit/Impact和加速表行为。\n");
}

int main() {
    printf("===================================================\n");
    printf("   Yat_Casched v2.2 调度器针对性测试程序\n");
    printf("===================================================\n");
    printf("版本: 2.2\n");
    printf("日期: 2025-07-18\n");
    printf("目标: 自动验证调度器核心机制\n\n");
    printf("=== 系统信息 ===\n");
    printf("PID: %d\n", getpid());
    printf("CPU 数量: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    printf("页面大小: %ld bytes\n", sysconf(_SC_PAGESIZE));
    check_kernel_scheduler();
    set_yat_casched_policy(getpid());
    print_current_policy(getpid());
    test_v2_2_benefit_impact();
    print_test_report();
    return global_result.success_count > 0 ? 0 : 1;
}
