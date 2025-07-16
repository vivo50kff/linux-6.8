#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#define MAX_PROCESSES 10

// 测试 Yat_Casched 调度器的核心功能
int main() {
    printf("=== Yat_Casched 调度器核心功能测试 ===\n");
    printf("测试时间: ");
    system("date");
    printf("\n");

    // 1. 测试调度器初始化状态
    printf("🔍 1. 检查调度器初始化状态\n");
    printf("内核版本: ");
    system("uname -r");
    
    printf("调度器启动信息: ");
    system("dmesg | grep -E 'yat_casched|init.*rq' | tail -3");
    printf("\n");

    // 2. 测试哈希表和历史表 - 通过创建进程来触发
    printf("🧪 2. 测试哈希表和历史表操作\n");
    printf("创建多个进程来测试调度器数据结构...\n");
    
    pid_t pids[MAX_PROCESSES];
    int i;
    
    // 创建多个子进程来测试调度器
    for (i = 0; i < MAX_PROCESSES; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // 子进程：执行一些计算来触发调度
            printf("  进程 %d 开始执行 (PID: %d)\n", i, getpid());
            
            // 模拟不同类型的负载
            if (i % 3 == 0) {
                // CPU 密集型
                volatile long sum = 0;
                for (long j = 0; j < 1000000; j++) {
                    sum += j;
                }
            } else if (i % 3 == 1) {
                // I/O 密集型
                usleep(100000); // 100ms
            } else {
                // 混合型
                volatile long sum = 0;
                for (long j = 0; j < 500000; j++) {
                    sum += j;
                    if (j % 100000 == 0) usleep(10000);
                }
            }
            
            printf("  进程 %d 完成 (PID: %d)\n", i, getpid());
            exit(0);
        } else if (pids[i] < 0) {
            perror("fork failed");
        }
    }
    
    // 观察进程调度情况
    sleep(1);
    printf("\n当前进程状态:\n");
    system("ps aux | head -1");
    system("ps aux | grep -v grep | grep -E '(test_yat|bash|sh)' | head -10");
    
    // 等待所有子进程完成
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (pids[i] > 0) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    }
    
    printf("\n");

    // 3. 测试缓存感知功能
    printf("🎯 3. 测试缓存感知调度\n");
    printf("创建内存访问密集型进程...\n");
    
    for (i = 0; i < 3; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            printf("  缓存测试进程 %d 开始 (PID: %d)\n", i, getpid());
            
            // 分配和访问内存来测试缓存感知
            size_t size = 1024 * 1024; // 1MB
            char *buffer = malloc(size);
            if (buffer) {
                // 随机访问内存
                for (int j = 0; j < 1000; j++) {
                    buffer[rand() % size] = j;
                }
                free(buffer);
            }
            
            printf("  缓存测试进程 %d 完成 (PID: %d)\n", i, getpid());
            exit(0);
        }
    }
    
    sleep(2);
    
    // 等待缓存测试进程
    while (wait(NULL) > 0);
    
    printf("\n");

    // 4. 显示调度统计信息
    printf("📊 4. 调度器统计信息\n");
    
    printf("系统负载: ");
    system("cat /proc/loadavg");
    
    printf("调度统计: ");
    system("cat /proc/schedstat 2>/dev/null | head -3 || echo '需要 CONFIG_SCHEDSTATS 支持'");
    
    printf("内存使用: ");
    system("free -h | head -2");
    
    printf("运行时间: ");
    system("uptime");
    
    printf("\n");

    // 5. 检查调度器错误日志
    printf("⚠️  5. 检查调度器日志\n");
    printf("最近的调度器相关日志:\n");
    system("dmesg | grep -E 'yat_casched|scheduler|sched' | tail -5");
    
    printf("\n=== 测试完成 ===\n");
    printf("✅ Yat_Casched 调度器核心功能测试完成\n");
    printf("🔍 观察要点:\n");
    printf("  - 调度器是否正确初始化\n");
    printf("  - 进程创建和调度是否正常\n");
    printf("  - 缓存感知功能是否工作\n");
    printf("  - 无错误日志输出\n");
    
    return 0;
}
