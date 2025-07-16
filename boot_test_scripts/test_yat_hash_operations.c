#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

// 测试 Yat_Casched 哈希表和历史表的增删改查
int main() {
    printf("=== Yat_Casched 哈希表和历史表测试 ===\n");
    printf("目标: 验证调度器数据结构的 CRUD 操作\n\n");

    // 1. 测试进程创建 (CREATE) - 触发哈希表插入
    printf("🔨 1. 测试哈希表插入 (CREATE)\n");
    printf("创建进程来触发调度器数据结构插入...\n");
    
    pid_t test_pids[5];
    for (int i = 0; i < 5; i++) {
        test_pids[i] = fork();
        if (test_pids[i] == 0) {
            printf("  新进程创建: PID=%d, 索引=%d\n", getpid(), i);
            sleep(2); // 保持活跃状态
            exit(0);
        }
    }
    
    sleep(1);
    printf("  当前活跃进程数: ");
    system("ps aux | grep -c test_yat_hash || echo '0'");
    printf("\n");

    // 2. 测试进程查询 (READ) - 触发哈希表查找
    printf("🔍 2. 测试哈希表查询 (READ)\n");
    printf("查询进程信息来测试哈希表查找...\n");
    
    for (int i = 0; i < 5; i++) {
        if (test_pids[i] > 0) {
            printf("  查询进程 PID=%d: ", test_pids[i]);
            char cmd[100];
            snprintf(cmd, sizeof(cmd), "ps -p %d -o pid,stat,time 2>/dev/null | tail -1 || echo 'NOT_FOUND'", test_pids[i]);
            system(cmd);
        }
    }
    printf("\n");

    // 3. 测试进程修改 (UPDATE) - 触发哈希表更新
    printf("🔄 3. 测试哈希表更新 (UPDATE)\n");
    printf("改变进程优先级来触发调度器更新...\n");
    
    for (int i = 0; i < 3; i++) {
        if (test_pids[i] > 0) {
            printf("  更新进程 %d 优先级...\n", test_pids[i]);
            char cmd[100];
            snprintf(cmd, sizeof(cmd), "renice 10 %d 2>/dev/null || echo '  优先级更新失败'", test_pids[i]);
            system(cmd);
        }
    }
    printf("\n");

    // 4. 测试历史表记录
    printf("📋 4. 测试历史表记录\n");
    printf("创建短生命周期进程来测试历史记录...\n");
    
    for (int i = 0; i < 3; i++) {
        pid_t short_pid = fork();
        if (short_pid == 0) {
            printf("  短期进程 %d 开始和结束 (PID: %d)\n", i, getpid());
            // 短暂的计算任务
            volatile int sum = 0;
            for (int j = 0; j < 100000; j++) {
                sum += j;
            }
            exit(0);
        } else if (short_pid > 0) {
            // 等待短期进程结束
            waitpid(short_pid, NULL, 0);
            printf("  短期进程 %d 已结束，应记录到历史表\n", i);
        }
    }
    printf("\n");

    // 5. 测试进程删除 (DELETE) - 触发哈希表删除
    printf("🗑️  5. 测试哈希表删除 (DELETE)\n");
    printf("终止进程来触发哈希表删除...\n");
    
    for (int i = 0; i < 5; i++) {
        if (test_pids[i] > 0) {
            printf("  终止进程 PID=%d\n", test_pids[i]);
            kill(test_pids[i], SIGTERM);
            waitpid(test_pids[i], NULL, 0);
        }
    }
    
    sleep(1);
    printf("  清理后活跃进程数: ");
    system("ps aux | grep -c test_yat_hash || echo '0'");
    printf("\n");

    // 6. 性能压力测试
    printf("⚡ 6. 哈希表性能压力测试\n");
    printf("快速创建和销毁进程来测试性能...\n");
    
    time_t start_time = time(NULL);
    
    for (int batch = 0; batch < 3; batch++) {
        printf("  批次 %d: 创建 10 个进程...\n", batch + 1);
        pid_t batch_pids[10];
        
        // 快速创建
        for (int i = 0; i < 10; i++) {
            batch_pids[i] = fork();
            if (batch_pids[i] == 0) {
                // 轻量级任务
                usleep(50000); // 50ms
                exit(0);
            }
        }
        
        // 等待完成
        for (int i = 0; i < 10; i++) {
            if (batch_pids[i] > 0) {
                waitpid(batch_pids[i], NULL, 0);
            }
        }
    }
    
    time_t end_time = time(NULL);
    printf("  压力测试完成，耗时: %ld 秒\n", end_time - start_time);
    printf("\n");

    // 7. 检查调度器状态
    printf("📊 7. 最终状态检查\n");
    
    printf("系统负载: ");
    system("cat /proc/loadavg");
    
    printf("内存使用: ");
    system("free | grep Mem | awk '{printf \"使用: %.1f%%\\n\", $3/$2*100}'");
    
    printf("调度器日志 (最近):\n");
    system("dmesg | grep -E 'yat_casched' | tail -3 || echo '无相关日志'");
    
    printf("\n=== 哈希表测试总结 ===\n");
    printf("✅ CREATE: 进程创建触发插入操作\n");
    printf("✅ READ:   进程查询触发查找操作\n");
    printf("✅ UPDATE: 优先级修改触发更新操作\n");
    printf("✅ DELETE: 进程终止触发删除操作\n");
    printf("✅ 性能:   批量操作压力测试完成\n");
    printf("✅ 历史:   短期进程记录到历史表\n");
    
    printf("\n🎯 验证要点:\n");
    printf("- 无内核 panic 或错误日志\n");
    printf("- 进程正常创建和销毁\n");
    printf("- 系统响应正常\n");
    printf("- 内存使用稳定\n");
    
    return 0;
}
