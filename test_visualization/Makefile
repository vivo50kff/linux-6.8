CC = gcc
CFLAGS = -Wall -O2 -lpthread

all: performance_test

performance_test: performance_test.c
	$(CC) $(CFLAGS) -o performance_test performance_test.c

# 检查是否在正确的内核版本
check-kernel:
	@echo "当前内核版本: $$(uname -r)"
	@echo "检查是否包含 Yat_Casched 调度器..."
	@if [ ! -f /proc/schedstat ]; then \
		echo "警告: 无法访问调度统计信息"; \
	fi

# 安装依赖
install-deps:
	sudo apt update
	sudo apt install -y python3-matplotlib python3-numpy python3-pandas python3-seaborn

# 运行测试
test: performance_test check-kernel
	@echo "开始性能测试..."
	./performance_test
	python3 visualize_results.py

# 可视化
visualize:
	python3 visualize_results.py

clean:
	rm -f performance_test *.png *.csv *.md
	@echo "Generating visualizations in virtual environment..."
	./venv/bin/python visualize_results.py
	@echo "Test complete! Check the generated PNG files and performance_summary.md"

# 检查当前内核是否支持 Yat_Casched
check-kernel:
	@echo "检查当前内核版本和调度器支持..."
	@echo "当前内核: $$(uname -r)"
	@echo "内核编译时间: $$(uname -v)"
	@echo ""
	@echo "检查调度器支持:"
	@if [ -f /proc/schedstat ]; then \
		echo "✓ /proc/schedstat 存在"; \
	else \
		echo "✗ /proc/schedstat 不存在"; \
	fi
	@if grep -q "CONFIG_SCHED_DEBUG" /boot/config-$$(uname -r) 2>/dev/null; then \
		echo "✓ CONFIG_SCHED_DEBUG 已启用"; \
	else \
		echo "? CONFIG_SCHED_DEBUG 状态未知"; \
	fi
	@echo ""
	@echo "注意: 要测试 Yat_Casched 调度器，需要："
	@echo "1. 编译包含 Yat_Casched 的内核"
	@echo "2. 重启到新内核"
	@echo "3. 验证调度器已加载"
	@echo ""
	@echo "检查是否需要重启到新内核："
	@if [ -d "/boot" ]; then \
		echo "可用内核版本:"; \
		ls -la /boot/vmlinuz-* | tail -3; \
	fi

# 检查并准备测试环境
prepare-test: check-kernel
	@echo ""
	@echo "=== 测试环境准备检查 ==="
	@echo ""
	@echo "1. 内核检查完成（见上方输出）"
	@echo ""
	@echo "2. 检查测试程序编译状态:"
	@if [ -f "performance_test" ]; then \
		echo "✓ 测试程序已编译"; \
	else \
		echo "✗ 需要先编译: make performance_test"; \
	fi
	@echo ""
	@echo "3. 检查Python依赖:"
	@$(PYTHON) -c "import matplotlib, numpy, pandas, seaborn; print('✓ Python依赖已安装')" 2>/dev/null || \
		echo "✗ 需要安装依赖: make install-deps"
	@echo ""
	@echo "4. 权限检查:"
	@echo "测试调度器需要 root 权限来设置调度策略"
	@echo "建议使用: sudo make run-test"
	@echo ""
	@echo "=== 重要提醒 ==="
	@echo "如果要测试 Yat_Casched 调度器："
	@echo "• 确保已编译包含 Yat_Casched 的内核"
	@echo "• 重启系统并选择新内核启动"
	@echo "• 验证新调度器可用后再运行测试"

# 安全的测试运行（带权限检查）
run-test: performance_test
	@echo "准备运行性能测试..."
	@echo "当前用户: $$(whoami)"
	@echo "当前内核: $$(uname -r)"
	@echo ""
	@if [ "$$(id -u)" -eq 0 ]; then \
		echo "以 root 权限运行测试..."; \
		./performance_test; \
	else \
		echo "需要 root 权限来设置调度策略"; \
		echo "正在使用 sudo 运行..."; \
		sudo ./performance_test; \
	fi
	@echo ""
	@echo "生成可视化图表..."
	$(PYTHON) visualize_results.py
	@echo ""
	@echo "测试完成！请查看生成的文件："
	@echo "• performance_comparison.png - 性能对比图"
	@echo "• performance_timeline.png - 时间线分析图"
	@echo "• performance_report.md - 详细报告"

# 显示帮助信息
help:
	@echo "Yat_Casched Performance Test Makefile"
	@echo "====================================="
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Compile the performance test program"
	@echo "  test             - Run complete test (compile + test + visualize)"
	@echo "  visualize        - Generate visualization only"
	@echo "  clean            - Clean generated files"
	@echo "  install-deps     - Install Python dependencies using apt (requires sudo)"
	@echo "  install-deps-venv- Create virtual environment with dependencies"
	@echo "  test-venv        - Run tests using virtual environment"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  # First time setup (choose one):"
	@echo "  make install-deps      # System-wide installation (requires sudo)"
	@echo "  make install-deps-venv # Virtual environment (recommended)"
	@echo ""
	@echo "  # Run tests:"
	@echo "  make test              # Using system Python"
	@echo "  source venv/bin/activate && make test  # Using virtual environment"
	@echo "  # OR"
	@echo "  make test-venv         # Direct virtual environment test"

.PHONY: all test visualize clean install-deps install-deps-venv test-venv help check-kernel prepare-test run-test
