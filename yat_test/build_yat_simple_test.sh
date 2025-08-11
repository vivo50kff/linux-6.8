#!/bin/bash
# chmod +x /bin/yat_simple_test
# chmod +x /bin/CFS_test

# TACLEBENCH kernel任务集自动化测试环境构建脚本
set -e

echo "=== TACLEBENCH kernel任务集自动化测试环境构建 ==="

# 1. 路径定义
YAT_TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_SRC_DIR="$YAT_TEST_DIR/../tacle-bench/bench/kernel"
INITRAMFS_DIR="$YAT_TEST_DIR/yat_simple_test_env/initramfs"
BIN_DIR="$INITRAMFS_DIR/bin"
TASKS=(binarysearch bitcount bitonic bsort complex_updates cosf countnegative cubic deg2rad fac fft filterbank)

# 2. 清理旧环境
rm -rf "$YAT_TEST_DIR/yat_simple_test_env"

# 3. 创建目录结构
mkdir -p "$BIN_DIR" "$INITRAMFS_DIR"/{sbin,etc,proc,sys,dev,tmp,var,lib,usr/bin,usr/sbin}

# 4. 编译12个kernel任务（只编译到虚拟环境bin目录，不再新建本地bin目录）
for idx in "${!TASKS[@]}"; do
    task="${TASKS[$idx]}"
    SRC_DIR="$KERNEL_SRC_DIR/$task"
    if ls "$SRC_DIR"/*.c 1> /dev/null 2>&1; then
        gcc -static -O2 -o "$BIN_DIR/$task" "$SRC_DIR"/*.c
        chmod +x "$BIN_DIR/$task"
        echo "  已编译: $task (包含所有.c文件)"
    else
        echo "  未找到: $SRC_DIR/*.c"
    fi
done

# 5. 编译主控测试程序
gcc -static -O2 -o "$BIN_DIR/tacle_kernel_test" "$YAT_TEST_DIR/tacle_kernel_test.c"
chmod +x "$BIN_DIR/tacle_kernel_test"

# 6. 生成init脚本
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc none /proc 2>/dev/null || true
mount -t sysfs none /sys 2>/dev/null || true
echo "=== 启动TACLEBENCH kernel任务集测试 ==="
/bin/tacle_kernel_test
echo "=== 所有测试任务完成，进入shell ==="
exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

# 7. 添加busybox
if command -v busybox >/dev/null 2>&1; then
    cp $(which busybox) "$BIN_DIR/"
    cd "$BIN_DIR"
    for cmd in sh ls cat echo mount umount ps kill halt reboot; do
        ln -sf busybox $cmd
    done
fi

# 8. 生成cpio镜像
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip | sudo tee "$YAT_TEST_DIR/yat_simple_test.cpio.gz" > /dev/null
echo "=== 构建完成 ==="
echo "测试镜像: $YAT_TEST_DIR/yat_simple_test.cpio.gz"
