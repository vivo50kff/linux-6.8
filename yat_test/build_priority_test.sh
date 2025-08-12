#!/bin/bash
# YAT_CASCHED 优先级调度测试构建脚本

set -e

echo "=== YAT_CASCHED 优先级调度测试构建 ==="

# 路径定义
YAT_TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
INITRAMFS_DIR="$YAT_TEST_DIR/priority_test_env/initramfs"
BIN_DIR="$INITRAMFS_DIR/bin"

# 清理旧环境
rm -rf "$YAT_TEST_DIR/priority_test_env"

# 创建目录结构
mkdir -p "$BIN_DIR" "$INITRAMFS_DIR"/{sbin,etc,proc,sys,dev,tmp}

# 编译优先级测试程序
echo "编译优先级调度测试程序..."
gcc -static -O2 -o "$BIN_DIR/priority_test" "$YAT_TEST_DIR/priority_test.c"
chmod +x "$BIN_DIR/priority_test"

# 生成简单的init脚本
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc none /proc 2>/dev/null || true
mount -t sysfs none /sys 2>/dev/null || true

echo "=== YAT_CASCHED 优先级调度测试 ==="
echo "系统信息:"
echo "CPU数量: $(nproc)"
echo "内存: $(free -m | grep Mem | awk '{print $2}')MB"
echo ""

echo "开始优先级调度测试..."
/bin/priority_test

echo ""
echo "测试完成，进入shell (可用命令有限)"
exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

# 添加busybox (如果可用)
if command -v busybox >/dev/null 2>&1; then
    cp $(which busybox) "$BIN_DIR/"
    cd "$BIN_DIR"
    for cmd in sh ls cat echo mount ps; do
        ln -sf busybox $cmd
    done
fi

# 生成cpio镜像
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip > "$YAT_TEST_DIR/priority_test.cpio.gz"

echo "=== 构建完成 ==="
echo "测试镜像: $YAT_TEST_DIR/priority_test.cpio.gz"
echo "下一步: /home/yatsched/linux-6.8/yat_test/start_priority_test.sh"