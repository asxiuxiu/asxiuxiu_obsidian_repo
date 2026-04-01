# ============================================================
# library-practice 顶层 Makefile
# ============================================================
#
# 用法：
#   make static   → 构建静态库示例（01）
#   make dynamic  → 构建动态库示例（02）
#   make fmt      → 构建 fmt 示例（03）
#   make all      → 构建所有
#   make run      → 构建并运行所有
#   make clean    → 清理所有 build 目录

.PHONY: all static dynamic fmt run clean

all: static dynamic fmt

static:
	@echo "\n========== [1/3] 静态库 libmymath ==========\n"
	$(MAKE) -C 01-static-lib

dynamic:
	@echo "\n========== [2/3] 动态库 libmystr ==========\n"
	$(MAKE) -C 02-dynamic-lib

fmt:
	@echo "\n========== [3/3] 开源库 {fmt} ==========\n"
	$(MAKE) -C 03-open-source

run:
	@echo "\n========== 运行静态库 demo ==========\n"
	./01-static-lib/build/demo

	@echo "\n========== 运行动态库 demo ==========\n"
	@cd 02-dynamic-lib && \
	  $(if $(filter Darwin,$(shell uname)), \
	    DYLD_LIBRARY_PATH=build ./build/demo, \
	    LD_LIBRARY_PATH=build ./build/demo)

	@echo "\n========== 运行 fmt demo ==========\n"
	./03-open-source/build/demo

clean:
	$(MAKE) -C 01-static-lib clean
	$(MAKE) -C 02-dynamic-lib clean
	$(MAKE) -C 03-open-source clean
