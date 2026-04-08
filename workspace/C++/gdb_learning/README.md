# GDB 调试学习代码示例

这个目录包含了一系列用于学习 GDB 调试的 C++ 代码示例。

## 文件说明

| 文件 | 主题 | 练习内容 |
|------|------|----------|
| `01_basic_debug.cpp` | 基础调试 | 断点设置、单步执行、变量查看 |
| `02_pointers_arrays.cpp` | 指针和数组 | 数组打印、指针解引用、内存查看 |
| `03_classes_objects.cpp` | 类和对象 | 对象调试、成员函数、this 指针 |
| `04_call_stack.cpp` | 调用栈分析 | backtrace、frame 切换 |
| `05_conditional_breakpoints.cpp` | 条件断点 | 条件断点、ignore、tbreak |
| `06_watchpoints.cpp` | 观察点 | watch、rwatch、awatch |
| `07_multithread.cpp` | 多线程调试 | 线程切换、线程断点 |
| `08_segfault_debug.cpp` | 段错误调试 | Core dump 分析、崩溃定位 |

## 快速开始

### 1. 编译所有示例

```bash
cd workspace/gdb_learning
make all
```

### 2. 单独编译某个示例

```bash
g++ -std=c++17 -g -O0 01_basic_debug.cpp -o 01_basic_debug
g++ -std=c++17 -g -O0 -pthread 07_multithread.cpp -o 07_multithread
```

### 3. 启动 GDB

```bash
# 基本启动
gdb ./01_basic_debug

# 带参数启动
gdb --args ./01_basic_debug arg1 arg2

# TUI 模式（推荐）
gdb -tui ./01_basic_debug
```

## 学习路径建议

### 第1步：基础调试 (01_basic_debug.cpp)

练习命令：
```gdb
(gdb) break main
(gdb) run
(gdb) next
(gdb) print a
(gdb) step
(gdb) continue
```

### 第2步：指针和数组 (02_pointers_arrays.cpp)

练习命令：
```gdb
(gdb) print *arr@10        # 打印数组
(gdb) x/10dw arr           # 内存查看
(gdb) display arr[i]       # 自动显示
```

### 第3步：类和对象 (03_classes_objects.cpp)

练习命令：
```gdb
(gdb) break Person::setAge
(gdb) print person
(gdb) print this->name
```

### 第4步：调用栈分析 (04_call_stack.cpp)

练习命令：
```gdb
(gdb) backtrace            # 完整调用栈
(gdb) bt 5                 # 前5层
(gdb) frame 2              # 切换到第2层
(gdb) up/down              # 上下移动
(gdb) info locals          # 当前帧局部变量
```

### 第5步：条件断点 (05_conditional_breakpoints.cpp)

练习命令：
```gdb
(gdb) break 45 if i == 50
(gdb) break 45 if i % 10 == 0
(gdb) ignore 1 10
(gdb) tbreak 60
```

### 第6步：观察点 (06_watchpoints.cpp)

练习命令：
```gdb
(gdb) watch global_var
(gdb) rwatch secret_value
(gdb) awatch shared_data
```

### 第7步：多线程调试 (07_multithread.cpp)

练习命令：
```gdb
(gdb) info threads
(gdb) thread 2
(gdb) thread apply all bt
(gdb) break 40 thread 2
```

### 第8步：段错误调试 (08_segfault_debug.cpp)

练习命令：
```bash
# 启用 core dump
ulimit -c unlimited
./08_segfault_debug
# 程序崩溃后
gdb ./08_segfault_debug core
(gdb) bt
(gdb) info locals
```

## GDB 配置建议

创建 `~/.gdbinit` 文件：

```gdb
# 更好的打印格式
set print pretty on
set print object on
set print static-members on
set print vtbl on
set print demangle on

# 关闭确认提示
set confirm off

# 保存命令历史
set history save on
set history filename ~/.gdb_history

# 更好的分页
set pagination off

# 智能提示
set prompt (gdb)\n
# C++ 标准库 pretty printers
python
import sys
sys.path.insert(0, '/usr/share/gcc-python/')
from libstdcxx.v6.printers import register_libstdcxx_printers
register_libstdcxx_printers(None)
end
```

## 快捷键

| 快捷键 | 说明 |
|--------|------|
| `Ctrl+C` | 中断程序运行 |
| `Ctrl+X+A` | 切换 TUI 模式 |
| `Enter` | 重复上一条命令 |
| `Tab` | 命令补全 |

## 常见问题

**Q: GDB 提示 "No symbol table loaded"**
A: 编译时忘记加 `-g` 选项，重新编译。

**Q: 变量优化掉了，无法打印**
A: 使用 `-O0` 编译，关闭优化。

**Q: STL 容器打印出来看不懂**
A: 配置 pretty printers，或手动打印：`print *(vec._M_impl._M_start)@vec.size()`

## 参考

- [[../C++/GDB调试指南.md|GDB 调试指南]] - 完整的 GDB 命令参考
- [[../C++/调试器核心概念与原理.md|调试器核心概念与原理]]
