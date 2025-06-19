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

## 🚀 快速开始

### 标准启动方式（推荐）

```bash
# 进入项目根目录
cd linux-6.8

# 使用标准模板启动（KVM加速，4核）
./boot_test_scripts/start_with_template.sh
```

### 直接 QEMU 命令启动

```bash
qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -smp 4 \
  -m 4069 \
  -name "yat-casched-test" \
  -nographic \
  -kernel /home/lwd/桌面/linux-6.8/arch/x86/boot/bzImage \
  -initrd /home/lwd/桌面/linux-6.8/boot_test_scripts/initramfs_complete.cpio.gz \
  -append "console=ttyS0,115200 rdinit=/init panic=1 loglevel=7"
```

## 📚 文件说明

### 核心脚本

#### `create_initramfs_complete.sh`
- **作用**: 创建包含所有测试程序的完整 initramfs 文件系统
- **用法**: `./create_initramfs_complete.sh`
- **输出**: `initramfs_complete.cpio.gz`
- **依赖**: 需要编译好的测试程序（test_*）

#### `start_with_template.sh`
- **作用**: 标准 QEMU 启动脚本，使用 KVM 加速
- **用法**: `./start_with_template.sh`
- **特点**: 
  - KVM 加速（如可用）
  - 4核心配置
  - 4GB 内存
  - 兼容 VSCode 终端

#### `start_with_intel_vtx.sh`
- **作用**: Intel VT-x/EPT 优化的 QEMU 启动脚本
- **用法**: `./start_with_intel_vtx.sh`
- **特点**:
  - 专为 Intel CPU 优化
  - 包含权限检查
  - 详细错误提示

### 测试程序

#### `test_yat_casched_complete`
- **作用**: Yat_Casched 调度器完整功能测试
- **功能**: 多进程、多线程、性能测试
- **用法**: 在 QEMU 环境中运行 `test_yat_casched_complete`

#### `test_cache_aware_fixed`
- **作用**: 缓存感知调度专项测试
- **功能**: 测试缓存局部性、任务分布
- **用法**: 在 QEMU 环境中运行 `test_cache_aware_fixed`

#### `verify_real_scheduling`
- **作用**: 调度器真实性验证程序
- **功能**: 验证系统调用、CPU迁移、调度策略
- **用法**: 在 QEMU 环境中运行 `verify_real_scheduling`
- **说明**: 系统启动时会自动运行

### 数据文件

#### `initramfs_complete.cpio.gz`
- **作用**: 完整的根文件系统
- **内容**: BusyBox + 所有测试程序 + init 脚本
- **大小**: 约 2MB
- **用法**: 作为 QEMU 的 `-initrd` 参数

## 🎯 使用场景

### 场景1: 快速验证调度器功能
```bash
# 一键启动并自动验证
./boot_test_scripts/start_with_template.sh
# 系统会自动运行 verify_real_scheduling
```

### 场景2: 运行特定测试
```bash
# 启动系统后，在 QEMU 环境中：
test_yat_casched_complete     # 完整功能测试
test_cache_aware_fixed        # 缓存感知测试
verify_real_scheduling        # 重新验证调度器
```

### 场景3: 重新生成测试环境
```bash
# 如果修改了测试程序，重新打包：
./boot_test_scripts/create_initramfs_complete.sh
# 然后重新启动测试
./boot_test_scripts/start_with_template.sh
```

## 🔧 故障排除

### 权限问题
```bash
chmod +x boot_test_scripts/*.sh
```

### KVM 不可用
- 检查 BIOS 是否启用虚拟化
- 检查用户是否在 kvm 组：`groups | grep kvm`
- 如需添加：`sudo usermod -a -G kvm $USER`（需重新登录）

### 内核镜像缺失
```bash
# 确保内核已编译
make -j$(nproc)
ls -la arch/x86/boot/bzImage
```

## 📋 测试结果解读

### 成功指标
- ✅ 调度策略从 0 变为 8（SCHED_YAT_CASCHED）
- ✅ 多进程测试无 kernel panic
- ✅ CPU 迁移行为符合预期
- ✅ 缓存感知效果可观察

### 退出方式
- **正常退出**: 按 `Ctrl+A`，然后按 `X`
- **强制退出**: 按 `Ctrl+C`
- **命令退出**: 在 shell 中输入 `poweroff`
