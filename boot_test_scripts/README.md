# Yat_Casched 调度器测试环境使用指南

## 📁 目录结构

```
boot_test_scripts/
├── create_initramfs_complete.sh      # 创建完整功能的 initramfs 文件系统
├── start_with_template.sh            # 标准 QEMU 启动脚本（推荐）
├── start_with_intel_vtx.sh           # Intel VT-x 优化启动脚本
├── initramfs_complete.cpio.gz        # 完整测试环境文件系统
├── test_yat_casched_complete         # 完整功能测试程序
├── test_cache_aware_fixed            # 缓存感知专项测试程序
├── verify_real_scheduling            # 调度器真实性验证程序
└── README.md                         # 本使用指南
```

## 🚀 使用方法

1. 进入 boot_test_scripts 目录：
   ```bash
   cd boot_test_scripts
   ```
2. 生成 initramfs（如未生成）：
   ```bash
   ./create_initramfs_complete.sh
   ```
3. 启动 QEMU 测试环境（推荐）：
   ```bash
   ./start_with_template.sh
   ```
   或（Intel VT-x 优化）：
   ```bash
   ./start_with_intel_vtx.sh
   ```

## 🧪 测试程序说明

- `test_yat_casched_complete`：完整功能测试，自动验证调度器各项能力。
- `test_cache_aware_fixed`：缓存感知专项测试。
- `verify_real_scheduling`：调度器真实性验证。

## ⚠️ 注意事项
- 所有脚本和测试程序均采用相对路径，无需修改。
- 主目录无需保留任何测试脚本，全部集中在 boot_test_scripts 目录。
- 如遇权限问题请先 `chmod +x *.sh`。
