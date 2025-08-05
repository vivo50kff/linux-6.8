# TACLeBench 顶层 Makefile
# 支持编译所有基准测试

# 编译器配置
CC_HOST = gcc
CC_ARM = arm-none-eabi-gcc
CFLAGS = -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-maybe-uninitialized -Wno-implicit-fallthrough

# ARM 交叉编译选项
ARM_FLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

# 基准测试路径
BENCH_DIR = bench
APP_DIR = $(BENCH_DIR)/app
KERNEL_DIR = $(BENCH_DIR)/kernel
PARALLEL_DIR = $(BENCH_DIR)/parallel
SEQUENTIAL_DIR = $(BENCH_DIR)/sequential

# 排除复杂的多架构项目（这些需要特殊处理）
EXCLUDE_PATTERNS = */DEBIE/* */PapaBench/* */arch/* */harness/* */sw/* */lib/* */crt0/* */thread*

# 查找基准测试目录，但排除复杂项目的子目录
APP_TARGETS = $(sort $(patsubst %/,%,$(dir $(shell find $(APP_DIR) -mindepth 2 -name "*.c" | grep -v -E "$(EXCLUDE_PATTERNS)" | head -n 50))))
KERNEL_TARGETS = $(sort $(patsubst %/,%,$(dir $(shell find $(KERNEL_DIR) -mindepth 2 -name "*.c" | grep -v -E "$(EXCLUDE_PATTERNS)" | head -n 50))))
PARALLEL_TARGETS = $(sort $(patsubst %/,%,$(dir $(shell find $(PARALLEL_DIR) -mindepth 1 -maxdepth 2 -name "*.c" | grep -v -E "$(EXCLUDE_PATTERNS)" | head -n 20))))
SEQUENTIAL_TARGETS = $(sort $(patsubst %/,%,$(dir $(shell find $(SEQUENTIAL_DIR) -mindepth 2 -name "*.c" | grep -v -E "$(EXCLUDE_PATTERNS)" | head -n 50))))

ALL_TARGETS = $(APP_TARGETS) $(KERNEL_TARGETS) $(PARALLEL_TARGETS) $(SEQUENTIAL_TARGETS)

# 默认目标
all: all-host

# 编译所有基准测试（主机版本）
all-host:
	@echo "编译所有基准测试（主机版本）..."
	@for target in $(ALL_TARGETS); do \
		echo "编译 $$target (主机版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.host"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_HOST) $(CFLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

# 编译所有基准测试（ARM 版本）
all-arm:
	@echo "编译所有基准测试（ARM 版本）..."
	@for target in $(ALL_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

# 编译各类别基准测试（主机版本）
app-host:
	@echo "编译所有 app 基准测试（主机版本）..."
	@for target in $(APP_TARGETS); do \
		echo "编译 $$target (主机版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.host"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_HOST) $(CFLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

kernel-host:
	@echo "编译所有 kernel 基准测试（主机版本）..."
	@for target in $(KERNEL_TARGETS); do \
		echo "编译 $$target (主机版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.host"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_HOST) $(CFLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

parallel-host:
	@echo "编译所有 parallel 基准测试（主机版本）..."
	@for target in $(PARALLEL_TARGETS); do \
		echo "编译 $$target (主机版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.host"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_HOST) $(CFLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

sequential-host:
	@echo "编译所有 sequential 基准测试（主机版本）..."
	@for target in $(SEQUENTIAL_TARGETS); do \
		echo "编译 $$target (主机版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.host"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_HOST) $(CFLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

# 编译所有基准测试（ARM 版本）
all-arm:
	@echo "编译所有基准测试（ARM 版本）..."
	@for target in $(ALL_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

# 编译各类别基准测试（ARM 版本）
app-arm:
	@echo "编译所有 app 基准测试（ARM 版本）..."
	@for target in $(APP_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

kernel-arm:
	@echo "编译所有 kernel 基准测试（ARM 版本）..."
	@for target in $(KERNEL_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

parallel-arm:
	@echo "编译所有 parallel 基准测试（ARM 版本）..."
	@for target in $(PARALLEL_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

sequential-arm:
	@echo "编译所有 sequential 基准测试（ARM 版本）..."
	@for target in $(SEQUENTIAL_TARGETS); do \
		echo "编译 $$target (ARM 版本)"; \
		benchmark_name=$$(basename "$$target"); \
		benchmark_dir="$$target"; \
		output_file="$$benchmark_dir/$$benchmark_name.elf"; \
		mkdir -p "$$benchmark_dir"; \
		c_files=$$(find "$$benchmark_dir" -maxdepth 1 -name "*.c" 2>/dev/null); \
		if [ -n "$$c_files" ]; then \
			$(CC_ARM) $(CFLAGS) $(ARM_FLAGS) $$c_files -o "$$output_file" 2>/dev/null || \
			echo "警告: $$benchmark_name 编译失败"; \
		else \
			echo "警告: $$benchmark_dir 中没有找到 C 文件"; \
		fi; \
	done
	@echo "完成！"

# 清理生成的文件
clean:
	@echo "清理所有编译产物..."
	@find $(BENCH_DIR) -name "*.host" -delete 2>/dev/null || true
	@find $(BENCH_DIR) -name "*.elf" -delete 2>/dev/null || true
	@echo "完成！"

# 测试单个基准测试的编译
test-single:
	@echo "测试单个基准测试编译..."
	@echo "Sequential 目标: $(SEQUENTIAL_TARGETS)"
	@echo "Kernel 目标: $(KERNEL_TARGETS)"
	@echo "App 目标: $(APP_TARGETS)"
	@echo "Parallel 目标: $(PARALLEL_TARGETS)"

# 调试信息
debug:
	@echo "调试信息:"
	@echo "APP_TARGETS: $(APP_TARGETS)"
	@echo "KERNEL_TARGETS: $(KERNEL_TARGETS)"
	@echo "PARALLEL_TARGETS: $(PARALLEL_TARGETS)"
	@echo "SEQUENTIAL_TARGETS: $(SEQUENTIAL_TARGETS)"
	@echo ""
	@echo "查找到的基准测试目录:"
	@echo "=== Sequential 目录 ==="
	@find $(SEQUENTIAL_DIR) -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort || echo "无 sequential 目录"
	@echo ""
	@echo "=== Kernel 目录 ==="
	@find $(KERNEL_DIR) -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort || echo "无 kernel 目录"
	@echo ""
	@echo "=== App 目录 ==="
	@find $(APP_DIR) -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort || echo "无 app 目录"
	@echo ""
	@echo "=== Parallel 目录 ==="
	@find $(PARALLEL_DIR) -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort || echo "无 parallel 目录"
	@echo ""
	@echo "=== 包含 C 文件的目录 ==="
	@find bench -name "*.c" -type f | head -20 | while read file; do echo "$$file"; done

# 列出可用的基准测试
list-benchmarks:
	@echo "=== 可用的基准测试程序 ==="
	@echo ""
	@echo "Sequential 基准测试:"
	@find $(SEQUENTIAL_DIR) -mindepth 1 -maxdepth 2 -type d 2>/dev/null | grep -v -E "$(EXCLUDE_PATTERNS)" | while read dir; do \
		name=$$(basename "$$dir"); \
		if find "$$dir" -maxdepth 1 -name "*.c" -type f | head -1 >/dev/null 2>&1; then \
			echo "  ✓ $$name"; \
		else \
			echo "  ✗ $$name (无主C文件)"; \
		fi; \
	done || echo "  无 sequential 目录"
	@echo ""
	@echo "Kernel 基准测试:"
	@find $(KERNEL_DIR) -mindepth 1 -maxdepth 2 -type d 2>/dev/null | grep -v -E "$(EXCLUDE_PATTERNS)" | while read dir; do \
		name=$$(basename "$$dir"); \
		if find "$$dir" -maxdepth 1 -name "*.c" -type f | head -1 >/dev/null 2>&1; then \
			echo "  ✓ $$name"; \
		else \
			echo "  ✗ $$name (无主C文件)"; \
		fi; \
	done || echo "  无 kernel 目录"
	@echo ""
	@echo "App 基准测试:"
	@find $(APP_DIR) -mindepth 1 -maxdepth 2 -type d 2>/dev/null | grep -v -E "$(EXCLUDE_PATTERNS)" | while read dir; do \
		name=$$(basename "$$dir"); \
		if find "$$dir" -maxdepth 1 -name "*.c" -type f | head -1 >/dev/null 2>&1; then \
			echo "  ✓ $$name"; \
		else \
			echo "  ✗ $$name (无主C文件)"; \
		fi; \
	done || echo "  无 app 目录"
	@echo ""
	@echo "Parallel 基准测试:"
	@find $(PARALLEL_DIR) -mindepth 1 -maxdepth 2 -type d 2>/dev/null | grep -v -E "$(EXCLUDE_PATTERNS)" | while read dir; do \
		name=$$(basename "$$dir"); \
		if find "$$dir" -maxdepth 1 -name "*.c" -type f | head -1 >/dev/null 2>&1; then \
			echo "  ✓ $$name"; \
		else \
			echo "  ✗ $$name (无主C文件)"; \
		fi; \
	done || echo "  无 parallel 目录"
	@echo ""
	@echo "⚠️  注意：DEBIE 和 PapaBench 等复杂项目需要特殊编译方法"

# 特殊项目的编译指南
help-complex:
	@echo "=== 复杂项目编译指南 ==="
	@echo ""
	@echo "以下项目需要特殊编译方法："
	@echo ""
	@echo "1. DEBIE 项目："
	@echo "   cd bench/parallel/DEBIE/code"
	@echo "   # 查看具体的编译说明"
	@echo "   cat README.txt"
	@echo ""
	@echo "2. PapaBench 项目："
	@echo "   cd bench/parallel/PapaBench"
	@echo "   # 查看 Makefile 和文档"
	@echo "   ls conf/"
	@echo ""
	@echo "3. Rosace 项目："
	@echo "   cd bench/parallel/rosace"
	@echo "   # 这是多线程项目，需要特殊处理"
	@echo ""
	@echo "使用 'make list-simple' 查看简单的可编译项目"

# 列出简单的可编译项目
list-simple:
	@echo "=== 推荐的简单基准测试 ==="
	@echo ""
	@echo "优先尝试这些简单的基准测试："
	@echo ""
	@for dir in $(SEQUENTIAL_DIR) $(KERNEL_DIR) $(APP_DIR); do \
		if [ -d "$$dir" ]; then \
			category=$$(basename "$$dir"); \
			echo "$$category 类别:"; \
			find "$$dir" -mindepth 1 -maxdepth 1 -type d | head -10 | while read subdir; do \
				name=$$(basename "$$subdir"); \
				if find "$$subdir" -maxdepth 1 -name "*.c" -type f | head -1 >/dev/null 2>&1; then \
					c_count=$$(find "$$subdir" -maxdepth 1 -name "*.c" | wc -l); \
					echo "  ✓ $$name ($$c_count 个C文件)"; \
				fi; \
			done; \
			echo ""; \
		fi; \
	done

# 测试可用程序并显示输出
test-available:
	@echo "=== 测试所有可用的基准测试程序 ==="
	@echo "测试时间: $(date)"
	@echo ""
	@available_tests=$$(find bench -name "*.host" -type f 2>/dev/null); \
	if [ -z "$$available_tests" ]; then \
		echo "❌ 没有找到编译成功的程序"; \
		echo "请先运行: make sequential-host kernel-host"; \
		exit 1; \
	fi; \
	echo "找到以下可用测试:"; \
	echo "$$available_tests" | sed 's/^/  ✓ /'; \
	echo ""; \
	for test in $$available_tests; do \
		name=$$(basename "$$test" .host); \
		category=$$(echo "$$test" | cut -d'/' -f2); \
		echo "--- 测试 $$name ($$category) ---"; \
		echo "路径: $$test"; \
		echo -n "运行结果: "; \
		start_time=$$(date +%s.%N); \
		timeout 10s "$$test" > /tmp/bench_output 2>&1; \
		exit_code=$$?; \
		end_time=$$(date +%s.%N); \
		duration=$$(echo "$$end_time - $$start_time" | bc -l 2>/dev/null || echo "未知"); \
		case $$exit_code in \
			0) echo "✓ 成功 ($$duration 秒)";; \
			124) echo "⏰ 超时 (>10秒)";; \
			*) echo "❌ 失败 (退出码: $$exit_code)";; \
		esac; \
		output_size=$$(wc -c < /tmp/bench_output 2>/dev/null || echo 0); \
		if [ $$output_size -gt 0 ]; then \
			echo "程序输出:"; \
			head -5 /tmp/bench_output | sed 's/^/  | /'; \
			if [ $$output_size -gt 200 ]; then \
				echo "  | ... (输出已截断)"; \
			fi; \
		else \
			echo "程序无输出 (静默运行)"; \
		fi; \
		echo ""; \
	done; \
	rm -f /tmp/bench_output

# 详细分析单个程序
analyze-program:
	@if [ -z "$(PROG)" ]; then \
		echo "用法: make analyze-program PROG=程序路径"; \
		echo "示例: make analyze-program PROG=bench/kernel/sha/sha.host"; \
		exit 1; \
	fi
	@if [ ! -f "$(PROG)" ]; then \
		echo "错误: 文件 $(PROG) 不存在"; \
		exit 1; \
	fi
	@echo "=== 详细分析程序: $(PROG) ==="
	@echo ""
	@echo "1. 文件信息:"
	@ls -lh "$(PROG)"
	@echo ""
	@echo "2. 依赖分析:"
	@ldd "$(PROG)" 2>/dev/null || echo "静态链接或无法分析依赖"
	@echo ""
	@echo "3. 运行测试 (带时间测量):"
	@echo "运行 5 次取平均值..."
	@total_time=0; \
	success_count=0; \
	for i in 1 2 3 4 5; do \
		echo -n "  第 $$i 次: "; \
		start_time=$$(date +%s.%N); \
		timeout 30s "$(PROG)" > /tmp/bench_detail_$$i 2>&1; \
		exit_code=$$?; \
		end_time=$$(date +%s.%N); \
		if [ $$exit_code -eq 0 ]; then \
			duration=$$(echo "$$end_time - $$start_time" | bc -l 2>/dev/null || echo "0"); \
			total_time=$$(echo "$$total_time + $$duration" | bc -l 2>/dev/null || echo "$$total_time"); \
			success_count=$$((success_count + 1)); \
			printf "%.6f 秒 ✓\n" "$$duration"; \
		elif [ $$exit_code -eq 124 ]; then \
			echo "超时 ⏰"; \
		else \
			echo "失败 (退出码: $$exit_code) ❌"; \
		fi; \
	done; \
	if [ $$success_count -gt 0 ]; then \
		avg_time=$$(echo "scale=6; $$total_time / $$success_count" | bc -l 2>/dev/null || echo "未知"); \
		echo ""; \
		echo "平均执行时间: $$avg_time 秒 ($$success_count/5 成功)"; \
	fi
	@echo ""
	@echo "4. 程序输出分析:"
	@if [ -f /tmp/bench_detail_1 ]; then \
		output_size=$$(wc -c < /tmp/bench_detail_1); \
		if [ $$output_size -gt 0 ]; then \
			echo "程序有输出 ($$output_size 字节):"; \
			cat /tmp/bench_detail_1 | head -10 | sed 's/^/  | /'; \
		else \
			echo "程序无输出 (这在某些基准测试中是正常的)"; \
			echo "这类程序专注于内部计算，不产生用户可见输出"; \
		fi; \
	fi
	@rm -f /tmp/bench_detail_*

.PHONY: all all-host all-arm app-host kernel-host parallel-host sequential-host \
        app-arm kernel-arm parallel-arm sequential-arm clean help debug test-single \
        list-benchmarks help-complex list-simple test-available analyze-program