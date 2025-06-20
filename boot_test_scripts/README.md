# Yat_Casched 调度器测试环境使用指南

## 📁 目录结构

```
boot_test_scripts/
├── create_initramfs_complete.sh      # 创建完整功能的 initramfs 文件系统
├── start_with_template.sh            # 标准 QEMU 启动脚本（推荐）
├── start_with_intel_vtx.sh           # Intel VT-x 优化启动脚本
├── initramfs_complete.cpio.gz        # 完整测试环境文件系统
├── test_yat_casched_complete.c       # 完整功能测试程序源码
├── test_cache_aware_fixed.c          # 缓存感知专项测试源码
├── verify_real_scheduling.c          # 调度器真实性验证源码
├── README.md                         # 本使用指南
```

## 🚀 使用方法

1. 进入 boot_test_scripts 目录：

   ```bash
   cd boot_test_scripts
   ```

2. 编译测试程序（如有源码变动）：

   ```bash
   gcc -O2 -o test_yat_casched_complete test_yat_casched_complete.c
   gcc -O2 -o test_cache_aware_fixed test_cache_aware_fixed.c
   gcc -O2 -o verify_real_scheduling verify_real_scheduling.c
   ```

3. 生成 initramfs（如未生成或有测试程序更新）：

   ```bash
   ./create_initramfs_complete.sh
   ```

4. 启动 QEMU 测试环境（推荐）：

   ```bash
   ./start_with_template.sh
   ```

   或（Intel VT-x 优化，需要intel处理器）：

   ```bash
   ./start_with_intel_vtx.sh
   ```

## 🧪 测试程序说明

- `test_yat_casched_complete`：完整功能测试，自动验证调度器各项能力。
- `test_cache_aware_fixed`：缓存感知专项测试。
- `verify_real_scheduling`：调度器真实性验证。

## ⚠️ 注意事项


- 如遇权限问题请先 `chmod +x *.sh`。
- 这些脚本直接用QEMU虚拟机加载 bzImage 和 initramfs，无需将内核安装到主机系统。
- 只需保证 arch/x86/boot/bzImage 和 boot_test_scripts/initramfs_complete.cpio.gz 是最新编译生成版本。
- 每次修改测试程序源码后，记得重新编译并重新生成initramfs。
