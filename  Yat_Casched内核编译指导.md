# Yat_Casched 内核编译指导

## 1. 环境准备

- 推荐系统：Ubuntu 22.04+（或其他现代Linux发行版）
- 依赖工具安装：
  ```bash
  sudo apt update
  sudo apt install -y build-essential libncurses5-dev libssl-dev \
      libelf-dev bison flex bc dwarves git fakeroot
  ```

## 2. 配置内核

- 进入源码目录：
  ```bash
  cd /home/mrma91/Yat_CAsched/linux-6.8-main
  ```
- 复制当前系统配置（可选）：
  ```bash
  cp /boot/config-$(uname -r) .config
  ```
- 启用Yat_Casched调度器（如有Kconfig脚本）：
  ```bash
  scripts/config --enable CONFIG_SCHED_CLASS_YAT_CASCHED
  ```
- 进入菜单配置界面（可选）：
  ```bash
  make menuconfig
  ```
  检查/启用 `Yat_Casched scheduling class`。

## 3. 编译内核

- 清理旧文件（可选）：
  ```bash
  make mrproper
  ```
- 编译内核（bzImage）：
  ```bash
  make -j$(nproc)
  ```
- 编译模块（如有）：
  ```bash
  make modules
  ```

## 4. 编译结果检查

- 核心镜像应在：
  ```
  arch/x86/boot/bzImage
  ```
- 可用 `file` 命令检查：
  ```bash
  file arch/x86/boot/bzImage
  ```

## 5. 安装/测试

- 安装模块（如有）：
  ```bash
  sudo make modules_install
  ```
- 安装内核（可选）：
  ```bash
  sudo make install
  ```
- 更新引导（如GRUB）：
  ```bash
  sudo update-grub
  ```
- 重启并选择新内核。

## 6. QEMU 虚拟机测试（推荐）

- 可用QEMU直接测试新内核，避免影响主机系统。
- 示例启动命令（需准备initramfs）：
  ```bash
  qemu-system-x86_64 -kernel arch/x86/boot/bzImage -initrd initramfs.img -nographic -append "console=ttyS0"
  ```

---

> 编译遇到错误时，请根据提示修正依赖或配置，或将错误信息发给我进一步分析。
