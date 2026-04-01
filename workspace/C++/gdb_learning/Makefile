# GDB 学习示例 Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O0
LDFLAGS = -pthread

TARGETS = 01_basic_debug 02_pointers_arrays 03_classes_objects \
          04_call_stack 05_conditional_breakpoints 06_watchpoints \
          07_multithread 08_segfault_debug

.PHONY: all clean gdb-help

all: $(TARGETS)

01_basic_debug: 01_basic_debug.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

02_pointers_arrays: 02_pointers_arrays.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

03_classes_objects: 03_classes_objects.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

04_call_stack: 04_call_stack.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

05_conditional_breakpoints: 05_conditional_breakpoints.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

06_watchpoints: 06_watchpoints.cpp
	$(CXX) $(CFLAGS) $< -o $@

07_multithread: 07_multithread.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

08_segfault_debug: 08_segfault_debug.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TARGETS)
	rm -f core core.*

gdb-help:
	@echo "GDB 调试命令速查:"
	@echo "=================="
	@echo "启动:"
	@echo "  gdb ./program              # 启动 GDB"
	@echo "  gdb --args ./program arg1  # 带参数启动"
	@echo "  gdb ./program core         # 分析 core dump"
	@echo ""
	@echo "断点:"
	@echo "  break main                 # 在 main 设置断点"
	@echo "  break 20                   # 在第20行设置断点"
	@echo "  break func if x>10         # 条件断点"
	@echo "  info breakpoints           # 查看所有断点"
	@echo "  delete 1                   # 删除断点1"
	@echo ""
	@echo "运行:"
	@echo "  run                        # 运行程序"
	@echo "  continue                   # 继续运行"
	@echo "  step                       # 单步进入"
	@echo "  next                       # 单步跳过"
	@echo "  finish                     # 运行到函数返回"
	@echo ""
	@echo "查看:"
	@echo "  print var                  # 打印变量"
	@echo "  print *arr@10              # 打印数组"
	@echo "  display var                # 自动显示变量"
	@echo "  info locals                # 查看局部变量"
	@echo "  backtrace                  # 查看调用栈"
	@echo ""
	@echo "线程:"
	@echo "  info threads               # 查看所有线程"
	@echo "  thread 2                   # 切换到线程2"
	@echo "  thread apply all bt        # 所有线程的调用栈"
