# Algo Practice — C++ 算法练习环境

对应笔记：[[算法打卡]]

## 依赖：安装 GCC

> 统一使用 GCC，跨 Windows / macOS 行为一致。

**Windows（MSYS2 ucrt64）**
```bash
# 在 MSYS2 ucrt64 终端中运行（含 Ninja，推荐作生成器）
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

**macOS**
```bash
brew install gcc cmake ninja
```

## 构建与运行

> 以下命令在 **MSYS2 ucrt64 终端**（Windows）或普通终端（macOS）中运行。

### 生成器（GCC）

本仓库按 **GCC + 单配置生成器** 使用（例如 **Ninja** 或 **Unix Makefiles**）。这样可执行文件在 `build/<练习目录>/` 下，**不会**再套一层 `Debug/` / `Release/`（那是 Visual Studio 等多配置生成器的习惯）。

首次配置示例（推荐 Ninja + Debug，便于日常改题）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

若本机未装 Ninja，可改用 Makefiles：

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
```

> 若 `build/` 曾用其它生成器配置过，换生成器前请删除 `build` 目录后重新执行 `cmake`。

### 编译与测试

```bash
# 全量编译
cmake --build build

# 运行所有测试
ctest --test-dir build --output-on-failure
```

**只编 / 只测当前这一题**（日常改 `solution.cpp` 或 `main.cpp` 时最省事）：

```bash
# 只编译 day01_two_sum 这一目标（把名字换成当天的 target）
cmake --build build --target day01_two_sum

# 只跑该目标的测试（-R 为正则，可写成 day01、two_sum 等）
ctest --test-dir build -R day01_two_sum --output-on-failure
```

`add_test` 的名字与可执行目标一致，因此 **不必手抄 exe 路径**，`ctest -R` 会找到对应程序。

### 单独运行某一天（直接跑可执行文件）

在 GCC + 单配置下，可执行文件路径形如 `build/<练习目录>/<目标名>`（Windows 带 `.exe`）：

```bash
# Windows (MSYS2)
./build/day01_two_sum/day01_two_sum.exe

# macOS / Linux
./build/day01_two_sum/day01_two_sum
```

## 目录结构

```
algo-practice/
├── CMakeLists.txt          # 根构建文件
├── include/
│   └── common.h            # 轻量测试宏（EXPECT_EQ / RUN_TEST）
├── day01_two_sum/          # Day 1: Two Sum
│   ├── CMakeLists.txt
│   ├── solution.h
│   ├── solution.cpp        # ← 在这里写解法
│   └── main.cpp            # 测试用例
└── ...
```

## 新增每日练习

1. 复制 `day01_two_sum/` 目录，改名为 `dayXX_题目名/`
2. 修改 `solution.h` / `solution.cpp` 为新题目
3. 在根 `CMakeLists.txt` 末尾取消注释或新增 `add_subdirectory(dayXX_题目名)`
