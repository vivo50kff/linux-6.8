#!/usr/bin/env python3
# 文件名: scheduler_performance_analyzer.py

import re
import statistics
import matplotlib.pyplot as plt
# import seaborn as sns
import numpy as np
import pandas as pd
from collections import defaultdict
import os
from matplotlib.backends.backend_pdf import PdfPages

# 设置中文字体和样式
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.facecolor'] = 'white'
# sns.set_style("whitegrid")

def parse_ftrace_log(filename):
    """解析ftrace日志，提取函数执行时间"""
    function_times = defaultdict(list)
    
    try:
        with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
            line_count = 0
            matched_count = 0
            
            for line in f:
                line_count += 1
                line = line.strip()
                
                # 调试：打印前几行来理解格式
                if line_count <= 5:
                    print(f"样本行 {line_count:2d}: {line}")
                
                # 根据实际日志格式修改的匹配模式
                patterns = [
                    # 模式1: 匹配 "| + 19.074 us   |  } /* dequeue_task_fair */"
                    r'\|\s*[\+\!\#\s]*(\d+\.\d+)\s*us\s*\|\s*\}\s*/\*\s*(\w+)\s*\*/',
                    
                    # 模式2: 匹配 "|   5.919 us    |  pick_next_task_fair();"
                    r'\|\s*[\+\!\#\s]*(\d+\.\d+)\s*us\s*\|\s*(\w+)\(\);?',
                    
                    # 模式3: 匹配 "3)  yat_sim-1117  | + 14.130 us   |  task_tick_yat_casched();"
                    r'\d+\)\s+[\w<>-]+\s*\|\s*[\+\!\#\s]*(\d+\.\d+)\s*us\s*\|\s*(\w+)\(\);?',
                    
                    # 模式4: 匹配 "3)    <idle>-0    |   3.397 us    |    } /* update_curr_yat_casched */"
                    r'\d+\)\s+[\w<>-]+\s*\|\s*[\+\!\#\s]*(\d+\.\d+)\s*us\s*\|\s*\}\s*/\*\s*(\w+)\s*\*/',
                    
                    # 模式5: 更宽松的匹配，只要有时间和函数名
                    r'[\+\!\#\s]*(\d+\.\d+)\s*us.*?(\w*(?:yat_casched|fair|schedule|tick|enqueue|dequeue|pick|select|put|update_curr)\w*)',
                ]
                
                matched = False
                for pattern in patterns:
                    match = re.search(pattern, line)
                    if match:
                        try:
                            duration = float(match.group(1))
                            duration_ns = duration * 1000  # 转换为纳秒
                            func_name = match.group(2)
                            
                            # 过滤函数名，只保留调度器相关的函数（注释掉balance和wakeup）
                            if (len(func_name) > 3 and 
                                ('yat_casched' in func_name or 'fair' in func_name or 
                                 'schedule' in func_name or 
                                 # 'balance' in func_name or  # 注释掉负载均衡
                                 'tick' in func_name or 'enqueue' in func_name or
                                 'dequeue' in func_name or 'pick' in func_name or
                                 'select' in func_name or 'put' in func_name or
                                 # 'wakeup' in func_name or  # 注释掉抢占
                                 'update_curr' in func_name)):
                                
                                function_times[func_name].append(duration_ns)
                                matched_count += 1
                                matched = True
                                
                                # 调试：显示匹配的前几个结果
                                if matched_count <= 15:
                                    print(f"匹配 {matched_count:2d}: {func_name:<35} = {duration_ns:8.2f}ns")
                                break
                        except (ValueError, IndexError):
                            continue
            
            print(f"\n文件 {filename} 解析完成:")
            print(f"{'总行数:':<15} {line_count:,}")
            print(f"{'匹配行数:':<15} {matched_count:,}")
            print(f"{'找到的函数:':<15} {len(function_times)}")
            
            if function_times:
                print(f"\n{'函数名':<40} {'样本数':<8}")
                print("-" * 50)
                for func, times in sorted(function_times.items()):
                    print(f"{func:<40} {len(times):<8}")
            
    except FileNotFoundError:
        print(f"错误: 文件 {filename} 未找到")
    except Exception as e:
        print(f"解析文件 {filename} 时出错: {e}")
    
    return function_times

def group_functions_by_purpose(function_times):
    """按功能对函数进行分组"""
    groups = {
        'dequeue': [],
        'enqueue': [],
        'pick_next': [],
        'task_tick': [],
        'select_task_rq': [],
        'put_prev': [], 
        # 'balance': [],  # 注释掉负载均衡
        # 'wakeup_preempt': [],  # 注释掉抢占
        'update_curr': []
    }
    
    for func_name, times in function_times.items():
        if 'dequeue' in func_name:
            groups['dequeue'].extend(times)
        elif 'enqueue' in func_name:
            groups['enqueue'].extend(times)
        elif 'pick_next' in func_name:
            groups['pick_next'].extend(times)
        elif 'task_tick' in func_name:
            groups['task_tick'].extend(times)
        elif 'select_task_rq' in func_name:
            groups['select_task_rq'].extend(times)
        elif 'put_prev' in func_name:  # 重新添加put_prev
            groups['put_prev'].extend(times)
        # elif 'balance' in func_name:  # 注释掉负载均衡
        #     groups['balance'].extend(times)
        # elif 'wakeup_preempt' in func_name:  # 注释掉抢占
        #     groups['wakeup_preempt'].extend(times)
        elif 'update_curr' in func_name:
            groups['update_curr'].extend(times)
    
    return groups

def analyze_scheduler_performance(yat_file, cfs_file=None):
    """分析调度器性能"""
    
    print("调度器性能分析工具")
    print("=" * 60)
    print("解析YAT-CASched日志...")
    yat_times = parse_ftrace_log(yat_file)
    
    cfs_times = {}
    if cfs_file and os.path.exists(cfs_file):
        print(f"\n解析CFS日志...")
        cfs_times = parse_ftrace_log(cfs_file)
    else:
        print("注意: 没有CFS数据文件")
    
    # 按功能分组
    yat_groups = group_functions_by_purpose(yat_times)
    cfs_groups = group_functions_by_purpose(cfs_times)
    
    # 统计数据
    stats_data = []
    
    # 统计YAT数据（按功能分组）
    if any(yat_groups.values()):
        print("\n" + "=" * 100)
        print("YAT-CASched 调度器功能执行时间统计分析")
        print("=" * 100)
        print(f"{'功能':<20} {'样本数':<10} {'平均时间(ns)':<15} {'中位数(ns)':<15} {'标准差(ns)':<15} {'最小值(ns)':<15} {'最大值(ns)':<15}")
        print("-" * 100)
        
        for group_name, times in yat_groups.items():
            if times:
                yat_stats = {
                    'function': group_name,
                    'scheduler': 'YAT-CASched',
                    'samples': len(times),
                    'mean': statistics.mean(times),
                    'median': statistics.median(times),
                    'std': statistics.stdev(times) if len(times) > 1 else 0,
                    'min': min(times),
                    'max': max(times)
                }
                stats_data.append(yat_stats)
                
                print(f"{yat_stats['function']:<20} {yat_stats['samples']:<10} "
                      f"{yat_stats['mean']:<15.1f} {yat_stats['median']:<15.1f} {yat_stats['std']:<15.1f} "
                      f"{yat_stats['min']:<15.1f} {yat_stats['max']:<15.1f}")
    
    # 统计CFS数据（按功能分组）
    if any(cfs_groups.values()):
        print("\n" + "=" * 100)
        print("CFS 调度器功能执行时间统计分析")
        print("=" * 100)
        print(f"{'功能':<20} {'样本数':<10} {'平均时间(ns)':<15} {'中位数(ns)':<15} {'标准差(ns)':<15} {'最小值(ns)':<15} {'最大值(ns)':<15}")
        print("-" * 100)
        
        for group_name, times in cfs_groups.items():
            if times:
                cfs_stats = {
                    'function': group_name,
                    'scheduler': 'CFS',
                    'samples': len(times),
                    'mean': statistics.mean(times),
                    'median': statistics.median(times),
                    'std': statistics.stdev(times) if len(times) > 1 else 0,
                    'min': min(times),
                    'max': max(times)
                }
                stats_data.append(cfs_stats)
                
                print(f"{cfs_stats['function']:<20} {cfs_stats['samples']:<10} "
                      f"{cfs_stats['mean']:<15.1f} {cfs_stats['median']:<15.1f} {cfs_stats['std']:<15.1f} "
                      f"{cfs_stats['min']:<15.1f} {cfs_stats['max']:<15.1f}")
    
    return stats_data, yat_groups, cfs_groups

def create_comparison_visualization(stats_data, yat_groups, cfs_groups):
    """创建YAT vs CFS功能对比可视化图表"""
    if not stats_data:
        print("没有数据可用于生成图表")
        return

    df = pd.DataFrame(stats_data)
    yat_df = df[df['scheduler'] == 'YAT-CASched'].copy()
    cfs_df = df[df['scheduler'] == 'CFS'].copy()

    # 定义功能顺序（重新添加put_prev）
    function_order = ['dequeue', 'enqueue', 'pick_next', 'task_tick', 
                     'select_task_rq', 'put_prev', 'update_curr']
    
    # 统一功能顺序，确保对齐
    yat_df = yat_df.set_index('function').reindex(function_order, fill_value=0)
    cfs_df = cfs_df.set_index('function').reindex(function_order, fill_value=0)

    # 更友好的标签名
    labels = ['Task Dequeue', 'Task Enqueue', 'Pick Next Task', 'Task Tick', 
              'Select Task RQ', 'Put Prev Task', 'Update Current']

    # 确保img目录存在
    os.makedirs('img', exist_ok=True)
    
    # 准备数据
    yat_means = []
    cfs_means = []
    yat_mins = []
    cfs_mins = []
    yat_maxs = []
    cfs_maxs = []
    yat_samples = []
    cfs_samples = []
    yat_stds = []
    cfs_stds = []
    valid_labels = []
    valid_x = []
    
    for i, func in enumerate(function_order):
        yat_mean = yat_df.loc[func, 'mean'] if func in yat_df.index and yat_df.loc[func, 'mean'] > 0 else None
        cfs_mean = cfs_df.loc[func, 'mean'] if func in cfs_df.index and cfs_df.loc[func, 'mean'] > 0 else None
        
        if yat_mean is not None or cfs_mean is not None:
            yat_means.append(yat_mean if yat_mean is not None else 0)
            cfs_means.append(cfs_mean if cfs_mean is not None else 0)
            
            # 获取最大最小值
            yat_min = yat_df.loc[func, 'min'] if func in yat_df.index and yat_df.loc[func, 'min'] > 0 else 0
            cfs_min = cfs_df.loc[func, 'min'] if func in cfs_df.index and cfs_df.loc[func, 'min'] > 0 else 0
            yat_max = yat_df.loc[func, 'max'] if func in yat_df.index and yat_df.loc[func, 'max'] > 0 else 0
            cfs_max = cfs_df.loc[func, 'max'] if func in cfs_df.index and cfs_df.loc[func, 'max'] > 0 else 0
            
            yat_mins.append(yat_min)
            cfs_mins.append(cfs_min)
            yat_maxs.append(yat_max)
            cfs_maxs.append(cfs_max)
            
            # 获取其他数据
            yat_samples.append(yat_df.loc[func, 'samples'] if func in yat_df.index else 0)
            cfs_samples.append(cfs_df.loc[func, 'samples'] if func in cfs_df.index else 0)
            yat_stds.append(yat_df.loc[func, 'std'] if func in yat_df.index else 0)
            cfs_stds.append(cfs_df.loc[func, 'std'] if func in cfs_df.index else 0)
            
            valid_labels.append(labels[i])
            valid_x.append(len(valid_x))
    
    if not valid_x:
        print("没有有效数据可绘制")
        return
    
    valid_x = np.array(valid_x)
    
    # 图表1: 平均执行时间对比（移除误差线）
    fig1, ax1 = plt.subplots(1, 1, figsize=(12, 8))
    
    bars1 = ax1.bar(valid_x - 0.2, yat_means, width=0.4, label='YAT-CASched', 
                    color='#2E86AB', alpha=0.8)
    bars2 = ax1.bar(valid_x + 0.2, cfs_means, width=0.4, label='CFS', 
                    color='#FF6B6B', alpha=0.8)
    
    ax1.set_xticks(valid_x)
    ax1.set_xticklabels(valid_labels, rotation=45, ha='right')
    ax1.set_xlabel('Scheduler Functions')
    ax1.set_ylabel('Average Execution Time (ns)')
    ax1.set_title('Average Execution Time Comparison')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('img/1_average_execution_time.pdf', bbox_inches='tight')
    plt.savefig('img/1_average_execution_time.jpg', bbox_inches='tight', dpi=300)
    plt.close(fig1)
    
    # 图表2: 调用频率对比
    fig2, ax2 = plt.subplots(1, 1, figsize=(12, 8))
    
    ax2.bar(valid_x - 0.2, yat_samples, width=0.4, label='YAT-CASched', color='#2E86AB', alpha=0.8)
    ax2.bar(valid_x + 0.2, cfs_samples, width=0.4, label='CFS', color='#FF6B6B', alpha=0.8)
    ax2.set_xticks(valid_x)
    ax2.set_xticklabels(valid_labels, rotation=45, ha='right')
    ax2.set_xlabel('Scheduler Functions')
    ax2.set_ylabel('Call Frequency')
    ax2.set_title('Function Call Frequency Comparison')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('img/2_call_frequency.pdf', bbox_inches='tight')
    plt.savefig('img/2_call_frequency.jpg', bbox_inches='tight', dpi=300)
    plt.close(fig2)

    # 图表3: 稳定性对比（标准差）
    fig3, ax3 = plt.subplots(1, 1, figsize=(12, 8))
    
    ax3.bar(valid_x - 0.2, yat_stds, width=0.4, label='YAT-CASched', color='#2E86AB', alpha=0.8)
    ax3.bar(valid_x + 0.2, cfs_stds, width=0.4, label='CFS', color='#FF6B6B', alpha=0.8)
    ax3.set_xticks(valid_x)
    ax3.set_xticklabels(valid_labels, rotation=45, ha='right')
    ax3.set_xlabel('Scheduler Functions')
    ax3.set_ylabel('Standard Deviation (ns)')
    ax3.set_title('Execution Time Stability Comparison')
    ax3.legend()
    ax3.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('img/3_stability_comparison.pdf', bbox_inches='tight')
    plt.savefig('img/3_stability_comparison.jpg', bbox_inches='tight', dpi=300)
    plt.close(fig3)

    # 计算总执行时间
    yat_total_time = 0
    cfs_total_time = 0
    
    for i, func in enumerate(function_order):
        if func in yat_df.index:
            yat_samples_val = yat_df.loc[func, 'samples']
            yat_mean_val = yat_df.loc[func, 'mean']
            if yat_samples_val > 0 and yat_mean_val > 0:
                yat_total_time += yat_samples_val * yat_mean_val
        
        if func in cfs_df.index:
            cfs_samples_val = cfs_df.loc[func, 'samples']
            cfs_mean_val = cfs_df.loc[func, 'mean']
            if cfs_samples_val > 0 and cfs_mean_val > 0:
                cfs_total_time += cfs_samples_val * cfs_mean_val

    # 图表4: 功能对比统计表格（包含总执行时间）
    fig4, ax4 = plt.subplots(1, 1, figsize=(16, 10))
    ax4.axis('off')
    
    comparison_data = [['Function', 'YAT Calls', 'CFS Calls', 'YAT Avg(ns)', 'CFS Avg(ns)', 'YAT Min(ns)', 'CFS Min(ns)', 'YAT Max(ns)', 'CFS Max(ns)', 'YAT Total(ns)', 'CFS Total(ns)']]
    
    for i, func in enumerate(function_order):
        if func in yat_df.index or func in cfs_df.index:
            yat_samples_val = yat_df.loc[func, 'samples'] if func in yat_df.index else 0
            cfs_samples_val = cfs_df.loc[func, 'samples'] if func in cfs_df.index else 0
            yat_mean_val = yat_df.loc[func, 'mean'] if func in yat_df.index else 0
            cfs_mean_val = cfs_df.loc[func, 'mean'] if func in cfs_df.index else 0
            yat_min_val = yat_df.loc[func, 'min'] if func in yat_df.index else 0
            cfs_min_val = cfs_df.loc[func, 'min'] if func in cfs_df.index else 0
            yat_max_val = yat_df.loc[func, 'max'] if func in yat_df.index else 0
            cfs_max_val = cfs_df.loc[func, 'max'] if func in cfs_df.index else 0
            
            # 计算单个函数的总执行时间
            yat_func_total = yat_samples_val * yat_mean_val if yat_samples_val > 0 and yat_mean_val > 0 else 0
            cfs_func_total = cfs_samples_val * cfs_mean_val if cfs_samples_val > 0 and cfs_mean_val > 0 else 0
            
            if yat_samples_val > 0 or cfs_samples_val > 0:
                comparison_data.append([
                    labels[i],
                    f"{yat_samples_val:,}" if yat_samples_val > 0 else "-",
                    f"{cfs_samples_val:,}" if cfs_samples_val > 0 else "-",
                    f"{yat_mean_val:.1f}" if yat_mean_val > 0 else "-",
                    f"{cfs_mean_val:.1f}" if cfs_mean_val > 0 else "-",
                    f"{yat_min_val:.1f}" if yat_min_val > 0 else "-",
                    f"{cfs_min_val:.1f}" if cfs_min_val > 0 else "-",
                    f"{yat_max_val:.1f}" if yat_max_val > 0 else "-",
                    f"{cfs_max_val:.1f}" if cfs_max_val > 0 else "-",
                    f"{yat_func_total:.0f}" if yat_func_total > 0 else "-",
                    f"{cfs_func_total:.0f}" if cfs_func_total > 0 else "-"
                ])
    
    # 添加总计行
    comparison_data.append([
        "TOTAL",
        "-", "-", "-", "-", "-", "-", "-", "-",
        f"{yat_total_time:.0f}" if yat_total_time > 0 else "-",
        f"{cfs_total_time:.0f}" if cfs_total_time > 0 else "-"
    ])
    
    if len(comparison_data) > 1:
        table = ax4.table(cellText=comparison_data[1:],
                         colLabels=comparison_data[0],
                         cellLoc='center',
                         loc='center')
        table.auto_set_font_size(False)
        table.set_fontsize(9)
        table.scale(1.2, 1.8)
        
        # 设置表格样式
        for i in range(len(comparison_data)):
            for j in range(len(comparison_data[0])):
                cell = table[(i, j)]
                if i == 0:  # 表头
                    cell.set_facecolor('#4CAF50')
                    cell.set_text_props(weight='bold', color='white')
                elif i == len(comparison_data) - 1:  # 总计行
                    cell.set_facecolor('#FFE0B2')
                    cell.set_text_props(weight='bold')
                else:
                    cell.set_facecolor('#F5F5F5')
                cell.set_edgecolor('black')
    
    ax4.set_title('Scheduler Function Comparison Statistics with Total Execution Time', fontsize=16, fontweight='bold', pad=20)
    
    plt.tight_layout()
    plt.savefig('img/4_comparison_table.pdf', bbox_inches='tight')
    plt.savefig('img/4_comparison_table.jpg', bbox_inches='tight', dpi=300)
    plt.close(fig4)
    
    print(f"\n性能分析图表已分别保存为:")
    print(f"- img/1_average_execution_time.pdf/.jpg (平均执行时间)")
    print(f"- img/2_call_frequency.pdf/.jpg (调用频率对比)")
    print(f"- img/3_stability_comparison.pdf/.jpg (稳定性对比)")
    print(f"- img/4_comparison_table.pdf/.jpg (统计表格)")
    
    # 返回总执行时间用于后续统计
    return yat_total_time, cfs_total_time

def main():
    """主函数"""
    # 文件路径
    yat_log_file = "yat_scheduler_trace.log"
    cfs_log_file = "cfs_scheduler_trace.log"  # 可选
    
    try:
        # 分析性能
        stats_data, yat_times, cfs_times = analyze_scheduler_performance(yat_log_file, cfs_log_file)
        
        if not stats_data:
            print("错误: 未找到有效的性能数据")
            print("请检查日志文件是否存在有效的ftrace数据")
            return
        
        # 生成对比可视化
        yat_total_time, cfs_total_time = create_comparison_visualization(stats_data, yat_times, cfs_times)
        
        # 生成数据统计
        yat_data = [item for item in stats_data if item['scheduler'] == 'YAT-CASched']
        cfs_data = [item for item in stats_data if item['scheduler'] == 'CFS']
        
        print("\n" + "=" * 80)
        print("数据统计报告")
        print("=" * 80)
        
        if yat_data:
            print(f"\nYAT-CASched统计:")
            print(f"{'- 函数数量:':<20} {len(yat_data)}")
            print(f"{'- 总调用次数:':<20} {sum(item['samples'] for item in yat_data):,}")
            print(f"{'- 平均执行时间:':<20} {statistics.mean(item['mean'] for item in yat_data):.2f} ns")
            print(f"{'- 总执行时间:':<20} {yat_total_time:.0f} ns ({yat_total_time/1000000:.2f} ms)")
        
        if cfs_data:
            print(f"\nCFS统计:")
            print(f"{'- 函数数量:':<20} {len(cfs_data)}")
            print(f"{'- 总调用次数:':<20} {sum(item['samples'] for item in cfs_data):,}")
            print(f"{'- 平均执行时间:':<20} {statistics.mean(item['mean'] for item in cfs_data):.2f} ns")
            print(f"{'- 总执行时间:':<20} {cfs_total_time:.0f} ns ({cfs_total_time/1000000:.2f} ms)")
        
        # 对比总结
        if yat_total_time > 0 and cfs_total_time > 0:
            print(f"\n性能对比总结:")
            if yat_total_time < cfs_total_time:
                improvement = (cfs_total_time - yat_total_time) / cfs_total_time * 100
                print(f"{'- YAT相比CFS改进:':<20} {improvement:.1f}% (总执行时间减少)")
            else:
                degradation = (yat_total_time - cfs_total_time) / cfs_total_time * 100
                print(f"{'- YAT相比CFS变化:':<20} +{degradation:.1f}% (总执行时间增加)")
        
    except FileNotFoundError as e:
        print(f"错误: 找不到日志文件 - {e}")
        print("请确保日志文件存在于当前目录中")
    except Exception as e:
        print(f"分析过程中出现错误: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()