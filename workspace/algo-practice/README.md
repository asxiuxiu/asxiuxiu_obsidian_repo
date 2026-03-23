# Algo Practice — C++ 算法练习环境

对应笔记：[[算法打卡]]

## 依赖：安装 GCC

> 统一使用 GCC，跨 Windows / macOS 行为一致。

**Windows（MSYS2 ucrt64）**
```bash
# 在 MSYS2 ucrt64 终端中运行
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake ninja
```

**macOS**
```bash
brew install gcc cmake ninja
```

## 构建与运行

> 以下命令在 **MSYS2 ucrt64 终端**（Windows）或普通终端（macOS）中运行。

```bash
# 配置（Ninja + GCC，两端通用）
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++

# 编译
cmake --build build

# 运行所有测试
ctest --test-dir build --output-on-failure
```

### 单独运行某一天

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
