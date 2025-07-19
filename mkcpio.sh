#!/bin/bash

# 脚本: mkcpio.sh
# 功能: 创建一个包含 busybox 和测试程序的最小 cpio 根文件系统

# --- 配置 ---
BUSYBOX_URL="https://busybox.net/downloads/busybox-1.36.1.tar.bz2"
BUSYBOX_TAR="busybox-1.36.1.tar.bz2"
BUSYBOX_DIR="busybox-1.36.1"
ROOTFS_DIR="_rootfs"

# --- 函数 ---

# 检查命令是否存在
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# 出错时退出
die() {
    echo "Error: $1" >&2
    exit 1
}

# --- 主流程 ---

# 1. 环境检查
echo "Step 1: Checking environment..."
command_exists wget || die "wget is not installed. Please install it."
command_exists gcc || die "gcc is not installed. Please install it."
command_exists tar || die "tar is not installed. Please install it."

# 2. 下载并解压 BusyBox
echo "Step 2: Downloading and extracting BusyBox..."
if [ ! -f "$BUSYBOX_TAR" ]; then
    wget "$BUSYBOX_URL" || die "Failed to download BusyBox."
fi
if [ ! -d "$BUSYBOX_DIR" ]; then
    tar -xjf "$BUSYBOX_TAR" || die "Failed to extract BusyBox."
fi

# 3. 配置并编译 BusyBox
echo "Step 3: Configuring and compiling BusyBox..."
cd "$BUSYBOX_DIR" || die "Failed to enter BusyBox directory."
make defconfig || die "make defconfig failed."
# 关键：静态链接，这样我们就不需要复制库文件了
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
make -j$(nproc) || die "BusyBox make failed."
make install || die "BusyBox make install failed."
cd ..

# 4. 创建根文件系统目录结构
echo "Step 4: Creating rootfs directory structure..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,proc,sys,usr/{bin,sbin},dev}
cp -a "$BUSYBOX_DIR"/_install/* "$ROOTFS_DIR"/

# 5. 创建必要的设备文件
echo "Step 5: Creating device nodes..."
sudo mknod -m 666 "$ROOTFS_DIR"/dev/null c 1 3
sudo mknod -m 666 "$ROOTFS_DIR"/dev/tty c 5 0
sudo mknod -m 600 "$ROOTFS_DIR"/dev/console c 5 1

# 6. 创建 init 脚本
echo "Step 6: Creating init script..."
cat > "$ROOTFS_DIR"/init <<EOF
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys

echo "--- Starting YAT Scheduler Test ---"
/bin/test_yat
echo "--- Test Finished ---"

echo "Dropping to shell..."
exec /bin/sh
EOF
chmod +x "$ROOTFS_DIR"/init

# 7. 编译测试程序并放入根文件系统
echo "Step 7: Compiling and adding test program..."
[ -f "test_yat.c" ] || die "test_yat.c not found."
gcc -static -o "$ROOTFS_DIR"/bin/test_yat test_yat.c || die "Failed to compile test_yat.c"

# 8. 打包 CPIO 归档
echo "Step 8: Creating CPIO archive..."
cd "$ROOTFS_DIR" || die "Failed to enter rootfs directory."
find . -print0 | cpio --null -ov --format=newc > ../rootfs.cpio || die "Failed to create cpio archive."
cd ..

# 9. 清理权限（如果需要）
sudo chown $(whoami):$(whoami) -R "$ROOTFS_DIR"

echo "---"
echo "Success! rootfs.cpio has been created."
echo "You can now run the kernel with run_qemu.sh"
echo "---"
