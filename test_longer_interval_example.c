// 修改测试程序，增加迭代间隔
for (int i = 0; i < 10; i++) {
    int current_cpu = sched_getcpu();
    printf("  迭代 %d: CPU %d", i, current_cpu);
    
    // 执行更长时间的计算
    volatile long sum = 0;
    for (long j = 0; j < 10000000; j++) {  // 增加10倍
        sum += j * j;
    }
    
    // 添加更长的休眠，超过缓存热度窗口
    usleep(15000);  // 15ms，超过10ms热度窗口
    
    sched_yield();
    
    int new_cpu = sched_getcpu();
    if (new_cpu != current_cpu) {
        printf(" -> CPU %d (迁移!)", new_cpu);
    }
    printf("\n");
}
