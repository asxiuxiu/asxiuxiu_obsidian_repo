# make-cmake

> 📝 源自笔记：[[C++/构建系统：make 与 CMake]]、[[C++/静态库与动态库]]
> 📅 更新时间：2026-03-19

## 简介

`make` 解决了**重复编译**问题；`CMake` 解决了**跨平台构建描述**问题，并能生成 Makefile、Visual Studio 解决方案等各种构建文件。

本工作区在基础可执行文件之上，新增了**静态库（mymath）**和**动态库（mystr）**两个子模块，用 `add_subdirectory` 组织，演示 CMake 管理库的完整流程。

---

## 核心知识点

| CMake 指令 | 作用 |
|---|---|
| `add_library(mymath STATIC ...)` | 构建静态库（`.a` / `.lib`） |
| `add_library(mystr SHARED ...)` | 构建动态库（`.dylib` / `.so` / `.dll`） |
| `add_subdirectory(libs/mymath)` | 引入子模块，让根项目能找到子库目标 |
| `target_link_libraries(my_app PRIVATE mymath mystr)` | 将两个库链接进主程序 |
| `target_include_directories(mymath PUBLIC include/)` | `PUBLIC` 让调用方自动继承头文件路径 |

---

## 快速开始

### CMake 构建（推荐）

```bash
# 1. 进入工作区根目录
cd workspace/make-cmake

# 2. 清理并重新配置（build 目录存放所有生成文件）
rm -rf build && mkdir build && cd build
cmake ..

# 3. 编译（同时构建静态库、动态库、可执行文件）
cmake --build .

# 4. 运行
# macOS（需要告知动态链接器去哪找 libmystr.dylib）
DYLD_LIBRARY_PATH=. ./my_app

# Linux
# LD_LIBRARY_PATH=. ./my_app
```

预期输出：

```
=== CMake 静态库 & 动态库 实践 ===

[静态库 mymath]
  power(2, 10)  = 1024
  gcd(48, 18)   = 6
  lcm(4, 6)     = 12

[动态库 mystr]
  原字符串        : Hello, CMake!
  to_upper        : HELLO, CMAKE!
  to_lower        : hello, cmake!
  count_char('l') : 3
  reverse_str     : !ekamc ,olleH
```

### 验证静态库 vs 动态库的区别

```bash
# 查看静态库内容（打包了哪些 .o）
ar -t libmymath.a

# 查看动态库导出的符号（macOS）
nm -gU libmystr.dylib

# 查看可执行文件依赖的动态库（macOS）
otool -L my_app
# 你会看到 libmystr.dylib，但看不到 mymath（已静态合并进去了）

# Linux 等效命令
# nm -D libmystr.so
# ldd my_app
```

### 对比文件大小（体会静态 vs 动态）

```bash
cd build
ls -lh my_app libmystr.dylib libmymath.a
```

### CMake 生成 Visual Studio .sln（Windows）

```powershell
mkdir build_vs && cd build_vs
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
.\Release\my_app.exe
```

---

## 文件结构

```
make-cmake/
├── README.md
├── CMakeLists.txt            # 根构建：add_subdirectory + target_link_libraries
├── Makefile                  # 仅可执行文件的旧版 make 构建（参考用）
├── src/
│   ├── main.cpp              # 入口：同时调用 mymath（静态）和 mystr（动态）
│   ├── utils.cpp
│   └── math.cpp
├── include/                  # 旧版头文件（utils.h 等）
│   ├── utils.h
│   └── mymath.h
├── libs/
│   ├── mymath/               # 静态库子模块
│   │   ├── CMakeLists.txt    # add_library(mymath STATIC ...)
│   │   ├── include/
│   │   │   └── mymath.h      # 对外接口（幂运算、GCD、LCM）
│   │   └── src/
│   │       └── mymath.cpp
│   └── mystr/                # 动态库子模块
│       ├── CMakeLists.txt    # add_library(mystr SHARED ...)
│       ├── include/
│       │   └── mystr.h       # 对外接口（字符串处理）
│       └── src/
│           └── mystr.cpp
└── build/                    # CMake 构建输出（gitignore）
```

---

## 关键概念回顾

**为什么动态库运行时要设置 `DYLD_LIBRARY_PATH`？**

动态库在**运行时**才加载，操作系统按固定路径搜索（`/usr/lib` 等）。我们自己的 `libmystr.dylib` 不在系统路径里，所以要用环境变量临时指定搜索目录。

**`PUBLIC` vs `PRIVATE` 的区别？**

```cmake
target_include_directories(mymath PUBLIC include/)
#  ↑ PUBLIC：mymath 自己用，链接 mymath 的目标（如 my_app）也自动继承

target_link_libraries(my_app PRIVATE mymath)
#  ↑ PRIVATE：my_app 自己链接 mymath，但 my_app 的调用方不会传递这个依赖
```

**静态库编译进去了，动态库没有**

```bash
otool -L my_app
# libmystr.dylib 出现在列表里 ← 运行时依赖
# libmymath.a 不在列表里    ← 已经合并进 my_app 二进制
```
