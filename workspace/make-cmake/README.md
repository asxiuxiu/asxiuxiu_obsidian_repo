# make-cmake

> 📝 源自笔记：[[C++/构建系统：make 与 CMake]]
> 📅 生成时间：2026-03-19

## 简介

`make` 解决了**重复编译**问题；`CMake` 解决了**跨平台构建描述**问题，并能生成 Makefile、Visual Studio 解决方案等各种构建文件。

本工作区同时提供 Makefile 和 CMakeLists.txt 两套构建方式，方便对比学习。

## 快速开始

### 方式一：直接用 make

```bash
# 在项目根目录
make

# 运行
./my_app

# 清理
make clean
```

### 方式二：用 CMake（推荐，跨平台）

```bash
# 在项目根目录
mkdir build && cd build

# 配置（Linux/macOS 生成 Makefile）
cmake ..

# 构建
cmake --build .

# 运行
./my_app
```

### 方式三：CMake 生成 Visual Studio .sln（Windows）

```powershell
mkdir build_vs
cd build_vs
cmake .. -G "Visual Studio 17 2022" -A x64
# 双击 MyProject.sln 用 VS 打开，或：
cmake --build . --config Release
```

## 文件结构

```
make-cmake/
├── README.md
├── CMakeLists.txt        # CMake 构建描述（跨平台）
├── Makefile              # 直接 make 构建（Unix）
├── .gitignore
├── src/
│   ├── main.cpp          # 程序入口
│   ├── utils.cpp         # 工具函数
│   └── math.cpp          # 数学函数
├── include/
│   ├── utils.h
│   └── mymath.h
└── build/                # CMake 构建输出目录（gitignore）
```

## 扩展建议

- 添加子模块：参考笔记中的 `add_subdirectory(libs/mathlib)` 示例
- 集成第三方库：使用 `find_package()` 或 vcpkg toolchain file
- 添加测试：在 CMakeLists.txt 中启用 `enable_testing()` + `add_test()`
