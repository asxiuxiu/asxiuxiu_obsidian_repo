# 库文件实践区

> 配套理论笔记：[[C++/静态库与动态库]]

## 目录结构

```
library-practice/
├── 01-static-lib/     ← 自己写静态库 libmymath.a
├── 02-dynamic-lib/    ← 自己写动态库 libmystr.dylib
├── 03-open-source/    ← 使用开源库（brew 安装）
└── Makefile           ← 可选，用于一键构建
```

---

## 01 · 自写静态库 libmymath

### 位置

```bash
cd workspace/library-practice/01-static-lib
```

### 文件说明

| 文件 | 作用 |
|------|------|
| `include/mymath.h` | 头文件，声明函数接口 |
| `src/power.cpp` | 快速幂实现 |
| `src/gcd_lcm.cpp` | 最大公约数/最小公倍数 |
| `src/prime.cpp` | 质数判断 |
| `src/fibonacci.cpp` | 斐波那契数列 |
| `main.cpp` | 使用库的示例程序 |

### g++ 命令逐步构建

```bash
# 第一步：创建 build 目录
mkdir -p build

# 第二步：编译源文件为目标文件（-c 表示只编译不链接）
g++ -std=c++17 -Wall -Iinclude -c src/power.cpp -o build/power.o
g++ -std=c++17 -Wall -Iinclude -c src/gcd_lcm.cpp -o build/gcd_lcm.o
g++ -std=c++17 -Wall -Iinclude -c src/prime.cpp -o build/prime.o
g++ -std=c++17 -Wall -Iinclude -c src/fibonacci.cpp -o build/fibonacci.o

# 第三步：用 ar 打包成静态库
ar rcs build/libmymath.a build/power.o build/gcd_lcm.o build/prime.o build/fibonacci.o

# 第四步：编译主程序并链接静态库
g++ -std=c++17 -Wall -Iinclude main.cpp -Lbuild -lmymath -o build/demo

# 第五步：运行
./build/demo
```

### ar 命令参数说明

`ar rcs libmymath.a *.o`
- `r` — 插入或替换目标文件
- `c` — 创建库文件（如不存在）
- `s` — 写入符号索引（加快链接器查找）

### 验证静态库

```bash
# 查看静态库里包含哪些 .o 文件
ar -t build/libmymath.a

# 查看静态库里的符号
nm build/libmymath.a | grep -E "T |U "
```

---

## 02 · 自写动态库 libmystr

### 位置

```bash
cd workspace/library-practice/02-dynamic-lib
```

### g++ 命令逐步构建

```bash
# 第一步：创建 build 目录
mkdir -p build

# 第二步：编译为位置无关代码（-fPIC 是关键！）
g++ -std=c++17 -Wall -Iinclude -fPIC -c src/mystr.cpp -o build/mystr.o

# 第三步：生成动态库（macOS 用 .dylib，Linux 用 .so）
# macOS:
g++ -shared build/mystr.o -o build/libmystr.dylib
# Linux:
# g++ -shared build/mystr.o -o build/libmystr.so

# 第四步：编译主程序并链接动态库
g++ -std=c++17 -Wall -Iinclude main.cpp -Lbuild -lmystr -o build/demo

# 第五步：运行（必须告诉系统去哪找动态库）
# macOS:
DYLD_LIBRARY_PATH=build ./build/demo
# Linux:
# LD_LIBRARY_PATH=build ./build/demo
```

### 为什么需要 DYLD_LIBRARY_PATH？

动态库在**运行时**才加载，操作系统需要知道去哪找 `.dylib` 文件。

默认搜索路径：
1. `DYLD_LIBRARY_PATH` 环境变量指定的路径
2. `/usr/local/lib`
3. `/usr/lib`

自己写的库不在系统路径里，所以要手动指定。

### 验证动态库

```bash
# 查看动态库导出了哪些符号（macOS）
nm -gU build/libmystr.dylib

# 查看可执行文件依赖哪些动态库（macOS）
otool -L build/demo
# 输出示例：
# build/demo:
#   libmystr.dylib (compatibility version 0.0.0, current version 0.0.0)
#   /usr/lib/libc++.1.dylib
#   ...
```

---

## 03 · 使用开源库

### 3.1 {fmt} 格式化库

**安装：**
```bash
brew install fmt
```

**g++ 命令构建：**
```bash
cd workspace/library-practice/03-open-source

# 创建 build 目录
mkdir -p build

# 编译并链接 fmt（需要指定头文件路径和库路径）
g++ -std=c++17 -Wall \
    -I/usr/local/include \
    src/demo_fmt.cpp \
    -L/usr/local/lib -lfmt \
    -o build/demo_fmt

# 运行（macOS 需要指定库路径）
DYLD_LIBRARY_PATH=/usr/local/lib ./build/demo_fmt
```

### 3.2 SQLite3 数据库库

**安装：**
```bash
brew install sqlite3   # 通常系统已自带
```

**g++ 命令构建：**
```bash
cd workspace/library-practice/03-open-source

# 创建 build 目录
mkdir -p build

# 编译并链接 sqlite3
g++ -std=c++17 -Wall \
    -I/usr/local/include \
    src/demo_sqlite.cpp \
    -L/usr/local/lib -lsqlite3 \
    -o build/demo_sqlite

# 运行
DYLD_LIBRARY_PATH=/usr/local/lib ./build/demo_sqlite
```

### 链接开源库的通用模式

```bash
# Intel Mac: /usr/local/include + /usr/local/lib
# Apple Silicon: /opt/homebrew/include + /opt/homebrew/lib

g++ main.cpp \
    -I$(brew --prefix)/include \   # 头文件搜索路径
    -L$(brew --prefix)/lib \       # 库文件搜索路径
    -l<库名> \                      # 链接的库（-lfmt, -lsqlite3）
    -o output
```

---

## 静态库 vs 动态库对比

| 特性 | 静态库 (.a) | 动态库 (.dylib/.so) |
|------|-------------|---------------------|
| **链接时机** | 编译时 | 运行时 |
| **代码复制** | 是，完整复制到可执行文件 | 否，只记录引用 |
| **文件大小** | 可执行文件较大 | 可执行文件较小 |
| **运行方式** | 直接 `./demo` | 需要设置 `DYLD_LIBRARY_PATH` |
| **更新库** | 需重新编译程序 | 替换库文件即可 |
| **多程序共享** | 否，每个程序一份 | 是，内存中共享 |
| **编译选项** | 无特殊要求 | 必须加 `-fPIC` |

### 文件大小直观对比

```bash
cd workspace/library-practice

# 静态库方案
ls -lh 01-static-lib/build/demo

# 动态库方案
ls -lh 02-dynamic-lib/build/demo
ls -lh 02-dynamic-lib/build/libmystr.dylib
```

---

## 常用诊断命令

```bash
# ========== 静态库相关 ==========

# 查看静态库包含哪些 .o 文件
ar -t libxxx.a

# 解压静态库（提取所有 .o）
ar -x libxxx.a

# 查看静态库符号
nm libxxx.a | grep " T "


# ========== 动态库相关 ==========

# 查看动态库导出符号（macOS）
nm -gU libxxx.dylib
# Linux: nm -D libxxx.so

# 查看可执行文件依赖的动态库（macOS）
otool -L myapp
# Linux: ldd myapp


# ========== 通用 ==========

# 查看文件类型
file libxxx.a
file libxxx.dylib
file myapp
```

---

## 附录：Makefile 参考（可选）

如果以后学了 make，可以用这些命令一键构建：

```bash
# 构建全部
make all

# 分别构建
make static   # → 01-static-lib
make dynamic  # → 02-dynamic-lib
make fmt      # → 03-open-source fmt
make sqlite   # → 03-open-source sqlite

# 清理
make clean
```
