# TACLeBench app类任务批量测试脚本

本目录用于批量运行 tacle-bench/bench/app 目录下所有 .host 程序，自动记录运行时间，便于调度器对比实验。

## 使用方法

1. 确保 tacle-bench 已经编译好 app 类程序：
   ```bash
   cd /home/yat/Desktop/linux-6.8/tacle-bench
   make app-host
   ```

2. 运行批量测试脚本：
   ```bash
   cd /home/yat/Desktop/linux-6.8/boot_test_scripts/taclebench_test
   chmod +x run_taclebench_app.sh
   ./run_taclebench_app.sh
   ```

3. 结果会保存在 results_日期/summary.txt 和各个 .out 文件中。

## 说明
- 只会遍历可执行的 .host 程序
- 每个程序的标准输出和错误输出会分别保存
- 可根据需要扩展脚本，支持 perf、ftrace 等分析
