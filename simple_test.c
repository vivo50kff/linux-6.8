#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <string.h>

// 定义Yat_Casched调度策略ID
#define SCHED_YAT_CASCHED 7

int main() {
    printf("=== Simple Yat_Casched Test ===\n");
    
    pid_t pid = getpid();
    printf("Process PID: %d\n", pid);
    
    // 检查当前调度策略
    int current_policy = sched_getscheduler(pid);
    printf("Current scheduling policy: %d\n", current_policy);
    
    // 尝试设置Yat_Casched调度策略
    struct sched_param param;
    param.sched_priority = 0;
    
    printf("Attempting to set Yat_Casched policy...\n");
    int ret = sched_setscheduler(pid, SCHED_YAT_CASCHED, &param);
    
    if (ret == 0) {
        printf("SUCCESS: Yat_Casched policy set successfully!\n");
        
        // 验证设置是否成功
        int new_policy = sched_getscheduler(pid);
        printf("New scheduling policy: %d\n", new_policy);
        
        if (new_policy == SCHED_YAT_CASCHED) {
            printf("VERIFIED: Policy is now Yat_Casched\n");
        } else {
            printf("WARNING: Policy verification failed\n");
        }
    } else {
        printf("FAILED: Could not set Yat_Casched policy\n");
        printf("Error: %s (errno: %d)\n", strerror(errno), errno);
        
        if (errno == EINVAL) {
            printf("This suggests Yat_Casched is not available in the kernel\n");
        } else if (errno == EPERM) {
            printf("Permission denied - try running as root\n");
        }
    }
    
    return ret;
}
