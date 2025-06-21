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

def ensure_img_directory():
    """确保img目录存在"""
    img_dir = 'img'
    if not os.path.exists(img_dir):
        os.makedirs(img_dir)
        print(f"Created directory: {img_dir}")
    return img_dir

def load_performance_data():
    """加载性能测试数据"""
    filename = 'performance_results.csv'
    try:
        df = pd.read_csv(filename)
        print(f"Loaded {len(df)} records from {filename}")
        return df
    except FileNotFoundError:
        print(f"Error: {filename} not found.")
        print("Please run the performance test first: ./performance_test")
        return None

def plot_cpu_switches_comparison(df, img_dir):
    """绘制CPU切换次数对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每种调度器的平均CPU切换次数
    switches_summary = df.groupby('Scheduler')['CPU_Switches'].agg(['mean', 'std']).reset_index()
    
    schedulers = switches_summary['Scheduler']
    means = switches_summary['mean']
    stds = switches_summary['std']
    
    bars = plt.bar(schedulers, means, yerr=stds, 
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8, capsize=5)
    
    plt.title('CPU Switches Comparison\n(Lower is Better)', 
              fontsize=14, fontweight='bold')
    plt.ylabel('Average CPU Switches', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    plt.grid(True, alpha=0.3)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, means):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5, 
                f'{mean_val:.1f}', ha='center', va='bottom', fontweight='bold')
    
    plt.tight_layout()
    output_path = os.path.join(img_dir, 'cpu_switches_comparison.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"Generated: {output_path}")

def plot_execution_time_comparison(df, img_dir):
    """绘制执行时间对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每种调度器的平均执行时间
    time_summary = df.groupby('Scheduler')['Execution_Time'].agg(['mean', 'std']).reset_index()
    
    schedulers = time_summary['Scheduler']
    means = time_summary['mean']
    stds = time_summary['std']
    
    bars = plt.bar(schedulers, means, yerr=stds,
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8, capsize=5)
    
    plt.title('Execution Time Comparison\n(Lower is Better)', 
              fontsize=14, fontweight='bold')
    plt.ylabel('Average Execution Time (seconds)', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    plt.grid(True, alpha=0.3)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, means):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.001, 
                f'{mean_val:.3f}s', ha='center', va='bottom', fontweight='bold')
    
    plt.tight_layout()
    output_path = os.path.join(img_dir, 'execution_time_comparison.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"Generated: {output_path}")

def plot_cpu_affinity_comparison(df, img_dir):
    """绘制CPU亲和性对比图"""
    plt.figure(figsize=(10, 6))
    
    # 计算每种调度器的平均CPU亲和性分数
    affinity_summary = df.groupby('Scheduler')['CPU_Affinity_Score'].agg(['mean', 'std']).reset_index()
    
    schedulers = affinity_summary['Scheduler']
    means = affinity_summary['mean']
    stds = affinity_summary['std']
    
    bars = plt.bar(schedulers, means, yerr=stds,
                   color=['#ff7f0e', '#2ca02c'], alpha=0.8, capsize=5)
    
    plt.title('CPU Affinity Score Comparison\n(Higher is Better)', 
              fontsize=14, fontweight='bold')
    plt.ylabel('Average CPU Affinity Score', fontsize=12)
    plt.xlabel('Scheduler Type', fontsize=12)
    plt.ylim(0, 1)
    plt.grid(True, alpha=0.3)
    
    # 添加数值标签
    for bar, mean_val in zip(bars, means):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.02, 
                f'{mean_val:.3f}', ha='center', va='bottom', fontweight='bold')
    
    plt.tight_layout()
    output_path = os.path.join(img_dir, 'cpu_affinity_comparison.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"Generated: {output_path}")

def generate_performance_report(df, img_dir):
    """生成性能测试总结报告"""
    cfs_data = df[df['Scheduler'] == 'CFS']
    yat_data = df[df['Scheduler'] == 'Yat_Casched']
    
    report = []
    report.append("# Yat_Casched Performance Test Results\n")
    report.append("## Test Configuration")
    report.append(f"- Number of threads: 8")
    report.append(f"- Test type: Cache-intensive workload")
    report.append(f"- Cache size: 2MB per thread")
    report.append(f"- Iterations: 10 per thread")
    report.append(f"- Schedulers compared: CFS vs Yat_Casched\n")
    
    if len(cfs_data) > 0 and len(yat_data) > 0:
        # CPU Switches
        cfs_switches = cfs_data['CPU_Switches'].mean()
        yat_switches = yat_data['CPU_Switches'].mean()
        switch_reduction = (cfs_switches - yat_switches) / cfs_switches * 100 if cfs_switches > 0 else 0
        report.append(f"## CPU Switches Analysis\n")
        report.append(f"- CFS: {cfs_switches:.1f} (avg)")
        report.append(f"- Yat_Casched: {yat_switches:.1f} (avg)")
        report.append(f"- **Reduction: {switch_reduction:.1f}%**\n")
        
        # Execution Time
        cfs_time = cfs_data['Execution_Time'].mean()
        yat_time = yat_data['Execution_Time'].mean()
        time_improvement = (cfs_time - yat_time) / cfs_time * 100 if cfs_time > 0 else 0
        report.append(f"## Execution Time Analysis")
        report.append(f"- CFS: {cfs_time:.3f}s (avg)")
        report.append(f"- Yat_Casched: {yat_time:.3f}s (avg)")
        report.append(f"- **Performance improvement: {time_improvement:.1f}%**\n")
        
        # CPU Affinity
        cfs_affinity = cfs_data['CPU_Affinity_Score'].mean()
        yat_affinity = yat_data['CPU_Affinity_Score'].mean()
        affinity_improvement = (yat_affinity - cfs_affinity) / cfs_affinity * 100 if cfs_affinity > 0 else 0
        report.append(f"## CPU Affinity Analysis")
        report.append(f"- CFS: {cfs_affinity:.3f} (avg)")
        report.append(f"- Yat_Casched: {yat_affinity:.3f} (avg)")
        report.append(f"- **Improvement: {affinity_improvement:.1f}%**\n")
        
        # 结论
        report.append("## Conclusion")
        if switch_reduction > 0 and time_improvement > 0:
            report.append("Yat_Casched shows clear advantages in reducing CPU migrations and improving performance.")
        elif switch_reduction > 0:
            report.append("Yat_Casched successfully reduces CPU migrations, demonstrating better cache affinity.")
        else:
            report.append("Results show room for improvement in the Yat_Casched implementation.")
    
    # 保存报告
    report_path = os.path.join(img_dir, 'performance_summary.md')
    with open(report_path, 'w') as f:
        f.write('\n'.join(report))
    
    print(f"Generated: {report_path}")

def main():
    """主函数"""
    print("Generating performance visualization...")
    
    # 确保img目录存在
    img_dir = ensure_img_directory()
    
    # 加载数据
    df = load_performance_data()
    if df is None:
        return
    
    print(f"Loaded {len(df)} test records")
    
    # 生成各种图表
    plot_cpu_switches_comparison(df, img_dir)
    plot_execution_time_comparison(df, img_dir)
    plot_cpu_affinity_comparison(df, img_dir)
    
    # 生成总结报告
    generate_performance_report(df, img_dir)
    
    print(f"\nVisualization complete! Generated files in {img_dir}/:")
    print("- cpu_switches_comparison.png")
    print("- execution_time_comparison.png") 
    print("- cpu_affinity_comparison.png")
    print("- performance_summary.md")

if __name__ == "__main__":
    main()
