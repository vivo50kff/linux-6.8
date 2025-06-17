#!/bin/bash

echo "启动QEMU进行调度器真实性验证..."

if [ ! -f "arch/x86/boot/bzImage" ]; then
    echo "错误: bzImage不存在，请先编译内核。"
    exit 1
fi

if [ ! -f "initramfs_verify.cpio.gz" ]; then
    echo "错误: initramfs_verify.cpio.gz不存在，请先运行create_verify_initramfs.sh。"
    exit 1
fi

echo "启动配置:"
echo "- 内核: arch/x86/boot/bzImage"
echo "- Initramfs: initramfs_verify.cpio.gz"
echo "- CPU核心: 4"
echo "- 内存: 512MB"
echo ""
echo "这个测试将验证:"
echo "1. 系统调用sched_setscheduler()是否真的工作"
echo "2. 进程是否真的在不同CPU核心间迁移"
echo "3. 我们的调度器是否真的被内核调用"
echo ""
echo "按 Ctrl+A 然后 X 退出QEMU"
echo ""

qemu-system-x86_64 \
    -kernel arch/x86/boot/bzImage \
    -initrd initramfs_verify.cpio.gz \
    -m 512 \
    -smp 4 \
    -nographic \
    -append "console=ttyS0 loglevel=4 rdinit=/init" \
    -no-reboot
