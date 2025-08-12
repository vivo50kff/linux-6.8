#!/bin/bash

set -e

WORK_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_IMAGE="$WORK_DIR/../arch/arm64/boot/Image"
INITRAMFS="$WORK_DIR/yat_simple_test.cpio.gz"
LOG_FILE="$WORK_DIR/yat_simple_qemu_arm64.log"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "错误: 未找到ARM64内核镜像: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误: 未找到initramfs: $INITRAMFS"
    exit 1
fi

echo "内核镜像: $KERNEL_IMAGE"
echo "Initramfs: $INITRAMFS"
echo "日志文件: $LOG_FILE"

QEMU_ARGS=(
    -M virt
    -cpu cortex-a57
    -smp 4
    -m 2048M
    -kernel "$KERNEL_IMAGE"
    -initrd "$INITRAMFS"
    -append "console=ttyAMA0 root=/dev/ram0 rdinit=/init loglevel=7"
    -serial stdio
    -display none
    -no-reboot
)

echo "启动QEMU..."
qemu-system-aarch64 "${QEMU_ARGS[@]}" 2>&1 | tee "$LOG_FILE"
