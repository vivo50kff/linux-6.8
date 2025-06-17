#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
import os

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False
sns.set_style("whitegrid")

def load_performance_data():
    """加载性能测试数据，如果没有则生成示例数据"""
    try:
        df = pd.read_csv('performance_results.csv')
        return df
    except FileNotFoundError:
        print("Warning: performance_results.csv not found.")
        print("Generating sample performance data for demonstration...")
        
        # 生成示例数据
        sample_data = {
            'Scheduler': ['CFS', 'CFS', 'CFS', 'CFS', 
                         'Yat_Casched', 'Yat_Casched', 'Yat_Casched', 'Yat_Casched'],
            'CPU_Switches': [2847, 2831, 2863, 2852,
                           741, 739, 743, 745],
            'Cache_Hit_Rate': [87.3, 87.1, 87.5, 87.2,
                              92.8, 92.6, 93.0, 92.7],
            'Execution_Time': [10.52, 10.48, 10.56, 10.50,
                              9.99, 9.97, 10.01, 10.02],
            'Task_Migrations': [156, 152, 160, 154,
                               41, 39, 43, 42]
        }
        
        df = pd.DataFrame(sample_data)
        return df

def plot_cpu_switches_comparison(df):
    """绘制CPU切换次数对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每个调度器的平均CPU切换次数
    switches_by_scheduler = df.groupby('Scheduler')['CPU_Switches'].agg(['mean', 'std']).reset_index()
    
    # 创建柱状图
    bars = plt.bar(switches_by_scheduler['Scheduler'], switches_by_scheduler['mean'], 
                   yerr=switches_by_scheduler['std'], capsize=5, 
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8)
    
    plt.title('CPU Switches Comparison\n(Lower is Better)', fontsize=14, fontweight='bold')
    plt.ylabel('Average CPU Switches', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, switches_by_scheduler['mean']):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5, 
                f'{mean_val:.1f}', ha='center', va='bottom', fontweight='bold')
    
    # 计算改进百分比
    cfs_switches = switches_by_scheduler[switches_by_scheduler['Scheduler'] == 'CFS']['mean'].iloc[0]
    yat_switches = switches_by_scheduler[switches_by_scheduler['Scheduler'] == 'Yat_Casched']['mean'].iloc[0]
    improvement = (cfs_switches - yat_switches) / cfs_switches * 100
    
    plt.text(0.5, max(switches_by_scheduler['mean']) * 0.8, 
             f'Reduction: {improvement:.1f}%', 
             ha='center', fontsize=12, fontweight='bold',
             bbox=dict(boxstyle="round,pad=0.3", facecolor="lightblue"))
    
    plt.tight_layout()
    plt.savefig('cpu_switches_comparison.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("Generated: cpu_switches_comparison.png")

def plot_execution_time_comparison(df):
    """绘制执行时间对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每个调度器的平均执行时间
    time_by_scheduler = df.groupby('Scheduler')['Execution_Time'].agg(['mean', 'std']).reset_index()
    
    # 创建柱状图
    bars = plt.bar(time_by_scheduler['Scheduler'], time_by_scheduler['mean'], 
                   yerr=time_by_scheduler['std'], capsize=5, 
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8)
    
    plt.title('Execution Time Comparison\n(Lower is Better)', fontsize=14, fontweight='bold')
    plt.ylabel('Average Execution Time (seconds)', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, time_by_scheduler['mean']):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.01, 
                f'{mean_val:.3f}s', ha='center', va='bottom', fontweight='bold')
    
    # 计算性能提升百分比
    cfs_time = time_by_scheduler[time_by_scheduler['Scheduler'] == 'CFS']['mean'].iloc[0]
    yat_time = time_by_scheduler[time_by_scheduler['Scheduler'] == 'Yat_Casched']['mean'].iloc[0]
    improvement = (cfs_time - yat_time) / cfs_time * 100
    
    plt.text(0.5, max(time_by_scheduler['mean']) * 0.8, 
             f'Performance Improvement: {improvement:.1f}%', 
             ha='center', fontsize=12, fontweight='bold',
             bbox=dict(boxstyle="round,pad=0.3", facecolor="lightgreen"))
    
    plt.tight_layout()
    plt.savefig('execution_time_comparison.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("Generated: execution_time_comparison.png")

def plot_cpu_affinity_comparison(df):
    """绘制CPU亲和性对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每个调度器的平均CPU亲和性分数
    affinity_by_scheduler = df.groupby('Scheduler')['CPU_Affinity_Score'].agg(['mean', 'std']).reset_index()
    
    # 创建柱状图
    bars = plt.bar(affinity_by_scheduler['Scheduler'], affinity_by_scheduler['mean'], 
                   yerr=affinity_by_scheduler['std'], capsize=5, 
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8)
    
    plt.title('CPU Affinity Score Comparison\n(Higher is Better)', fontsize=14, fontweight='bold')
    plt.ylabel('Average CPU Affinity Score', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    plt.ylim(0, 1)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, affinity_by_scheduler['mean']):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.02, 
                f'{mean_val:.3f}', ha='center', va='bottom', fontweight='bold')
    
    # 计算改进百分比
    cfs_affinity = affinity_by_scheduler[affinity_by_scheduler['Scheduler'] == 'CFS']['mean'].iloc[0]
    yat_affinity = affinity_by_scheduler[affinity_by_scheduler['Scheduler'] == 'Yat_Casched']['mean'].iloc[0]
    improvement = (yat_affinity - cfs_affinity) / cfs_affinity * 100
    
    plt.text(0.5, 0.8, 
             f'Affinity Improvement: {improvement:.1f}%', 
             ha='center', fontsize=12, fontweight='bold',
             bbox=dict(boxstyle="round,pad=0.3", facecolor="lightyellow"))
    
    plt.tight_layout()
    plt.savefig('cpu_affinity_comparison.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("Generated: cpu_affinity_comparison.png")

def plot_detailed_thread_performance(df):
    """绘制详细的线程性能对比图"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    
    # 1. 每个线程的CPU切换次数
    cfs_data = df[df['Scheduler'] == 'CFS']
    yat_data = df[df['Scheduler'] == 'Yat_Casched']
    
    axes[0, 0].bar(cfs_data['Thread_ID'] - 0.2, cfs_data['CPU_Switches'], 
                   width=0.4, label='CFS', color='#ff7f0e', alpha=0.8)
    axes[0, 0].bar(yat_data['Thread_ID'] + 0.2, yat_data['CPU_Switches'], 
                   width=0.4, label='Yat_Casched', color='#2ca02c', alpha=0.8)
    axes[0, 0].set_title('CPU Switches by Thread')
    axes[0, 0].set_xlabel('Thread ID')
    axes[0, 0].set_ylabel('CPU Switches')
    axes[0, 0].legend()
    axes[0, 0].grid(True, alpha=0.3)
    
    # 2. 每个线程的执行时间
    axes[0, 1].bar(cfs_data['Thread_ID'] - 0.2, cfs_data['Execution_Time'], 
                   width=0.4, label='CFS', color='#ff7f0e', alpha=0.8)
    axes[0, 1].bar(yat_data['Thread_ID'] + 0.2, yat_data['Execution_Time'], 
                   width=0.4, label='Yat_Casched', color='#2ca02c', alpha=0.8)
    axes[0, 1].set_title('Execution Time by Thread')
    axes[0, 1].set_xlabel('Thread ID')
    axes[0, 1].set_ylabel('Execution Time (s)')
    axes[0, 1].legend()
    axes[0, 1].grid(True, alpha=0.3)
    
    # 3. 每个线程的CPU亲和性分数
    axes[1, 0].bar(cfs_data['Thread_ID'] - 0.2, cfs_data['CPU_Affinity_Score'], 
                   width=0.4, label='CFS', color='#ff7f0e', alpha=0.8)
    axes[1, 0].bar(yat_data['Thread_ID'] + 0.2, yat_data['CPU_Affinity_Score'], 
                   width=0.4, label='Yat_Casched', color='#2ca02c', alpha=0.8)
    axes[1, 0].set_title('CPU Affinity Score by Thread')
    axes[1, 0].set_xlabel('Thread ID')
    axes[1, 0].set_ylabel('Affinity Score')
    axes[1, 0].legend()
    axes[1, 0].grid(True, alpha=0.3)
    
    # 4. 性能改进总结
    metrics = ['CPU Switches', 'Execution Time', 'CPU Affinity']
    cfs_avg = [cfs_data['CPU_Switches'].mean(), cfs_data['Execution_Time'].mean(), cfs_data['CPU_Affinity_Score'].mean()]
    yat_avg = [yat_data['CPU_Switches'].mean(), yat_data['Execution_Time'].mean(), yat_data['CPU_Affinity_Score'].mean()]
    
    # 计算改进百分比
    improvements = []
    for i in range(len(metrics)):
        if i == 2:  # CPU Affinity (higher is better)
            imp = (yat_avg[i] - cfs_avg[i]) / cfs_avg[i] * 100
        else:  # CPU Switches and Execution Time (lower is better)
            imp = (cfs_avg[i] - yat_avg[i]) / cfs_avg[i] * 100
        improvements.append(imp)
    
    colors = ['green' if x > 0 else 'red' for x in improvements]
    bars = axes[1, 1].bar(metrics, improvements, color=colors, alpha=0.7)
    axes[1, 1].set_title('Performance Improvements (%)')
    axes[1, 1].set_ylabel('Improvement (%)')
    axes[1, 1].axhline(y=0, color='black', linestyle='-', alpha=0.3)
    axes[1, 1].grid(True, alpha=0.3)
    
    # 添加数值标签
    for bar, improvement in zip(bars, improvements):
        height = bar.get_height()
        axes[1, 1].text(bar.get_x() + bar.get_width()/2, height + (1 if height >= 0 else -3), 
                        f'{improvement:.1f}%', ha='center', va='bottom' if height >= 0 else 'top', 
                        fontweight='bold')
    
    plt.tight_layout()
    plt.savefig('detailed_thread_performance.png', dpi=300, bbox_inches='tight')
    plt.close()
    print("Generated: detailed_thread_performance.png")

def generate_summary_report(df):
    """生成性能测试总结报告"""
    cfs_data = df[df['Scheduler'] == 'CFS']
    yat_data = df[df['Scheduler'] == 'Yat_Casched']
    
    report = []
    report.append("# Yat_Casched Performance Test Results\n")
    report.append("## Test Configuration")
    report.append(f"- Number of threads: {len(cfs_data)}")
    report.append(f"- Test type: Cache-intensive workload")
    report.append(f"- Schedulers compared: CFS vs Yat_Casched\n")
    
    report.append("## Performance Metrics Summary\n")
    
    # CPU Switches
    cfs_switches = cfs_data['CPU_Switches'].mean()
    yat_switches = yat_data['CPU_Switches'].mean()
    switch_reduction = (cfs_switches - yat_switches) / cfs_switches * 100
    report.append(f"### CPU Switches")
    report.append(f"- CFS: {cfs_switches:.1f} (avg)")
    report.append(f"- Yat_Casched: {yat_switches:.1f} (avg)")
    report.append(f"- **Reduction: {switch_reduction:.1f}%**\n")
    
    # Execution Time
    cfs_time = cfs_data['Execution_Time'].mean()
    yat_time = yat_data['Execution_Time'].mean()
    time_improvement = (cfs_time - yat_time) / cfs_time * 100
    report.append(f"### Execution Time")
    report.append(f"- CFS: {cfs_time:.3f}s (avg)")
    report.append(f"- Yat_Casched: {yat_time:.3f}s (avg)")
    report.append(f"- **Performance improvement: {time_improvement:.1f}%**\n")
    
    # CPU Affinity
    cfs_affinity = cfs_data['CPU_Affinity_Score'].mean()
    yat_affinity = yat_data['CPU_Affinity_Score'].mean()
    affinity_improvement = (yat_affinity - cfs_affinity) / cfs_affinity * 100
    report.append(f"### CPU Affinity Score")
    report.append(f"- CFS: {cfs_affinity:.3f} (avg)")
    report.append(f"- Yat_Casched: {yat_affinity:.3f} (avg)")
    report.append(f"- **Improvement: {affinity_improvement:.1f}%**\n")
    
    # 保存报告
    with open('performance_summary.md', 'w') as f:
        f.write('\n'.join(report))
    
    print("Generated: performance_summary.md")

def main():
    """主函数"""
    print("Generating performance visualization...")
    
    # 检查是否在正确的目录
    if not os.path.exists('performance_results.csv'):
        print("Error: performance_results.csv not found.")
        print("Please run the performance test first: ./performance_test")
        return
    
    # 加载数据
    df = load_performance_data()
    if df is None:
        print("Failed to load performance data.")
        return
    
    print(f"Loaded {len(df)} test records")
    
    # 生成各种图表
    plot_cpu_switches_comparison(df)
    plot_execution_time_comparison(df)
    plot_cpu_affinity_comparison(df)
    plot_detailed_thread_performance(df)
    
    # 生成总结报告
    generate_summary_report(df)
    
    print("\nVisualization complete! Generated files:")
    print("- cpu_switches_comparison.png")
    print("- execution_time_comparison.png") 
    print("- cpu_affinity_comparison.png")
    print("- detailed_thread_performance.png")
    print("- performance_summary.md")

if __name__ == "__main__":
    main()
