---
title: C++ 编译选项
tags:
  - cpp
  - compiler
  - gcc
  - g++
date: 2026-03-09
---

# C++ 编译选项

> [!info] 关于静态库和动态库的详细解释
> 参考 [[C++/静态库与动态库.md|静态库与动态库]] —— 解释了为什么要分两种库、各自原理和用途

## 编译过程概述

使用 g++ 编译 C++ 程序通常经历四个阶段：

> [!info] 想了解"为什么要分四个阶段"的设计原理？
> 参考 [[C++/C++编译过程原理.md|C++ 编译过程原理]] —— 解释分层架构的设计哲学和收益

```mermaid
graph LR
    A[源文件.cpp] -->|预处理| B[.i 文件]
    B -->|编译| C[.s 汇编文件]
    C -->|汇编| D[.o 目标文件]
    D -->|链接| E[可执行文件]
```

### 分步编译示例

```bash
# 1. 预处理 (-E): 处理 #include、#define、条件编译等
g++ -E test.cpp -o test.i

# 2. 编译 (-S): 生成汇编代码
g++ -S test.i -o test.s

# 3. 汇编 (-c): 生成目标文件（机器码）
g++ -c test.s -o test.o

# 4. 链接: 生成可执行文件
g++ test.o -o test
```

### 一键编译

```bash
# 直接编译生成可执行文件
g++ test.cpp -o test

# 编译多个源文件
g++ main.cpp utils.cpp -o myapp
```

---

## 常用编译选项

### 输出相关

| 选项          | 说明                  |
| ----------- | ------------------- |
| `-o <file>` | 指定输出文件名             |
| `-E`        | 仅预处理，输出到标准输出        |
| `-S`        | 仅编译到汇编代码            |
| `-c`        | 仅编译/汇编，不链接，生成 .o 文件 |

### 警告选项

> [!tip] 建议始终开启 `-Wall -Wextra`，有助于发现潜在问题

| 选项                  | 说明              |     |
| ------------------- | --------------- | --- |
| `-Wall`             | 开启大多数常用警告       |     |
| `-Wextra`           | 开启额外的警告         |     |
| `-Werror`           | 将警告视为错误（强制修复）   |     |
| `-pedantic`         | 严格遵守 ISO C++ 标准 |     |
| `-Wshadow`          | 变量遮蔽警告          |     |
| `-Wconversion`      | 隐式类型转换警告        |     |
| `-Wsign-conversion` | 有符号/无符号转换警告     |     |
| `-Wunused`          | 未使用变量/函数警告      |     |
| `-Wno-<warning>`    | 关闭特定警告          |     |

```bash
# 推荐的警告组合
g++ -Wall -Wextra -Werror -pedantic test.cpp -o test
```

### 调试选项

| 选项      | 说明              |
| ------- | --------------- |
| `-g`    | 生成调试信息（用于 GDB）  |
| `-g0`   | 不生成调试信息         |
| `-g1`   | 生成最少调试信息        |
| `-g3`   | 生成最多调试信息（包含宏定义） |
| `-ggdb` | 生成 GDB 专用的调试信息  |

### 优化选项

> [!warning] 发布版本使用 `-O2`，追求极致性能可尝试 `-O3`，调试时用 `-O0`

| 选项       | 说明                |
| -------- | ----------------- |
| `-O0`    | 无优化（默认，编译最快，调试友好） |
| `-O1`    | 基本优化              |
| `-O2`    | 更多优化（推荐用于发布版本）    |
| `-O3`    | 最高级别优化（可能增加体积）    |
| `-Os`    | 优化代码体积            |
| `-Ofast` | 无视标准合规性的最大优化      |
| `-Og`    | 优化同时保留调试信息        |

### C++ 标准版本

| 选项 | 说明 |
|------|------|
| `-std=c++98` / `-std=c++03` | C++98/C++03 标准 |
| `-std=c++11` | C++11 标准 |
| `-std=c++14` | C++14 标准 |
| `-std=c++17` | C++17 标准 |
| `-std=c++20` | C++20 标准 |
| `-std=c++23` | C++23 标准 |
| `-std=gnu++17` | GNU 扩展的 C++17 |
| `-ansi` | 等同于 `-std=c++98` |

```bash
# 使用 C++17 标准编译
g++ -std=c++17 test.cpp -o test
```

### 预处理选项

| 选项 | 说明 |
|------|------|
| `-D<name>` | 定义宏 |
| `-D<name>=<value>` | 定义宏并赋值 |
| `-U<name>` | 取消定义宏 |
| `-I<dir>` | 添加头文件搜索路径 |
| `-include <file>` | 自动包含指定头文件 |

```bash
# 定义宏
g++ -DDEBUG -DVERSION=1.0 test.cpp -o test

# 添加头文件路径
g++ -I./include -I/usr/local/include test.cpp -o test
```

### 链接选项

| 选项               | 说明              |
| ---------------- | --------------- |
| `-L<dir>`        | 添加库文件搜索路径       |
| `-l<name>`       | 链接指定的库          |
| `-static`        | 静态链接            |
| `-shared`        | 生成动态库/共享库       |
| `-fPIC`          | 生成位置无关代码（用于动态库） |
| `-Wl,<option>`   | 传递选项给链接器        |
| `-nostdlib`      | 不链接标准库          |
| `-nodefaultlibs` | 不链接默认库          |

```bash
# 链接数学库和线程库
g++ test.cpp -o test -lm -lpthread

# 添加库搜索路径
g++ test.cpp -o test -L./lib -lmylib

# 生成动态库
g++ -shared -fPIC -o libmylib.so mylib.cpp
```

### 代码生成选项

| 选项 | 说明 |
|------|------|
| `-m32` | 生成 32 位代码 |
| `-m64` | 生成 64 位代码 |
| `-march=<arch>` | 指定目标架构 |
| `-mtune=<arch>` | 针对特定架构优化 |
| `-fomit-frame-pointer` | 省略帧指针 |
| `-fno-exceptions` | 禁用异常处理 |
| `-fno-rtti` | 禁用 RTTI（运行时类型信息） |
| `-fvisibility=hidden` | 默认隐藏符号（动态库） |

### 其他常用选项

| 选项 | 说明 |
|------|------|
| `-v` | 显示编译过程的详细信息 |
| `-###` | 显示编译命令但不执行 |
| `-MMD` | 生成依赖关系（用于 Make） |
| `-MF <file>` | 将依赖关系写入文件 |
| `-save-temps` | 保存中间文件（.i, .s, .o） |
| `-pipe` | 使用管道而非临时文件 |
| `-fopenmp` | 启用 OpenMP 支持 |
| `-pthread` | 启用 POSIX 线程支持 |
| `-fsanitize=address` | 启用 Address Sanitizer |
| `-fsanitize=undefined` | 启用 Undefined Behavior Sanitizer |

---

## 常用组合示例

### 开发调试模式

```bash
g++ -std=c++17 -Wall -Wextra -g -O0 main.cpp -o myapp
```

### 发布优化模式

```bash
g++ -std=c++17 -O2 -DNDEBUG main.cpp -o myapp
```

### 静态编译（不依赖动态库）

```bash
g++ -static -std=c++17 main.cpp -o myapp
```

### 生成动态库

```bash
# 编译位置无关代码
g++ -c -fPIC mylib.cpp -o mylib.o

# 生成动态库
g++ -shared mylib.o -o libmylib.so

# 或使用一步命令
g++ -shared -fPIC mylib.cpp -o libmylib.so
```

### 使用 Sanitizer 检测问题

```bash
# 检测内存错误（越界、使用后释放等）
g++ -fsanitize=address -g test.cpp -o test

# 检测未定义行为
g++ -fsanitize=undefined -g test.cpp -o test
```

### 生成依赖文件（用于 Makefile）

```bash
g++ -MMD -MF deps.d -c main.cpp -o main.o
```

---

## 编译器对比

| 功能     | GCC/G++         | MSVC (cl.exe) |
| ------ | --------------- | ------------- |
| 警告全开   | `-Wall -Wextra` | `/W4`         |
| 警告视为错误 | `-Werror`       | `/WX`         |
| 调试信息   | `-g`            | `/Zi`         |
| 优化级别   | `-O2`           | `/O2`         |
| 定义宏    | `-DDEBUG`       | `/DDEBUG`     |
| 头文件路径  | `-I./include`   | `/I./include` |
| 输出文件   | `-o test.exe`   | `/Fetest.exe` |
| C++ 标准 | `-std=c++17`    | `/std:c++17`  |

---

## 参考

- [[GCC 官方文档]]
- [[C++ 编译过程详解]]
- [[Makefile 编写指南]]
