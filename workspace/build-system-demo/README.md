# Source Forest 构建系统 Demo

> 📝 源自笔记：[[Generated/GameLearn/build-system-explained-simple.md]]
> 📅 生成时间：2026-03-30

## 简介

这个 Demo 演示了大型游戏引擎使用的 **Source Forest（源码森林）** 构建架构。核心思想：

- **源码分散在多个目录**（引擎、游戏、插件）
- **通过代理 CMakeLists.txt 间接引用**
- **JSON 驱动配置**，灵活组合不同模块

## 核心概念

| 概念 | 说明 |
|------|------|
| Source Forest | 分散在多个位置的源码集合，彼此无父子关系 |
| 代理 CMakeLists.txt | 位于 `build/sourcetree/` 的"传菜窗口"，只负责 `include` 真实源码 |
| JSON 配置 | 控制哪些模块参与构建，实现"同一套引擎，不同游戏使用" |

## 文件结构

```
build-system-demo/
├── _engine/              # ← 模拟引擎核心仓库
│   └── source/
│       ├── core/         # 核心模块
│       └── renderer/     # 渲染模块
├── _game/                # ← 模拟游戏项目仓库
│   └── source/
│       ├── client/       # 客户端模块
│       └── server/       # 服务端模块
├── _plugin/              # ← 模拟插件仓库
│   └── source/
│       └── physics/      # 物理插件
├── launch/               # 构建系统模板（相当于引擎工具链）
│   ├── templates/        # CMake 模板文件
│   └── generator.py      # sourcetree 生成器
├── configs/              # JSON 配置（不同构建目标）
│   ├── full_build.json   # 完整构建
│   └── engine_only.json  # 只构建引擎
└── build.bat             # 主构建入口
```

## 快速开始

### 1. 完整构建（引擎 + 游戏 + 插件）

```batch
build.bat
```

### 2. 只构建引擎核心

```batch
build.bat --config configs/engine_only.json
```

### 3. 手动分步执行（了解内部流程）

```batch
REM 1. 生成 sourcetree（搭建临时厨房）
python launch/generator.py --config configs/full_build.json

REM 2. 运行 CMake（开始炒菜）
cmake -S build/sourcetree -B build -G Ninja
cmake --build build
```

## 原理解析

### Step 1: 问题 - CMake 的 add_subdirectory 限制

CMake 只能添加**当前目录或子目录**，但引擎源码可能分散在：
- `E:/engine/source/core/`
- `E:/mygame/source/client/`
- `E:/plugins/source/physics/`

这些目录互相独立，无法直接在一个 CMakeLists.txt 里引用。

### Step 2: 解决方案 - 代理模式

在 `build/sourcetree/` 下创建代理文件：

```cmake
# build/sourcetree/engine/core/CMakeLists.txt（代理文件）
set(REAL_SOURCE_PATH E:/engine/source/core/)
include(${REAL_SOURCE_PATH}/CMakeLists.txt)
```

CMake 入口只需 `add_subdirectory(engine/core)`，就能间接加载真实源码。

### Step 3: JSON 驱动配置

通过 `full_build.json` 告诉生成器：
- 这次构建要包含哪些仓库
- 每个仓库里编译哪些模块
- 生成什么名字的 Solution

无需修改任何 CMakeLists.txt，换配置即可生成不同的工程。

## 运行结果

构建成功后：
- `build/` 目录包含 Ninja/Visual Studio 工程
- `build/GameEngine.exe` 是最终可执行文件
- 运行后会打印各个模块的初始化信息

## 架构流程图

```
┌─────────────────────────────────────────────────────────────┐
│                    build.bat (入口脚本)                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              launch/generator.py (Source Forest 生成器)       │
│                                                             │
│  1. 读取 configs/full_build.json (构建配置)                  │
│  2. 读取各仓库的 cmake_projects.json (模块配置)              │
│  3. 生成 build/sourcetree/ (临时源码森林)                    │
│     ├── CMakeLists.txt (根入口)                             │
│     ├── _engine/source/core/CMakeLists.txt (代理)           │
│     ├── _engine/source/renderer/CMakeLists.txt (代理)       │
│     ├── _game/source/client/CMakeLists.txt (代理)           │
│     └── ...                                                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     cmake -S sourcetree -B build            │
│                                                             │
│  1. 解析 generated/modules.cmake (模块列表)                  │
│  2. add_subdirectory(_engine/source/core)                   │
│     → 执行代理 CMakeLists.txt                               │
│     → include(真实源码目录/CMakeLists.txt)                  │
│  3. 在真实源码目录中查找源文件、编译静态库                    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    cmake --build build                      │
│                                                             │
│  输出:                                                       │
│  - build/lib/*.lib (静态库)                                 │
│  - build/bin/GameEngine.exe (可执行文件)                    │
└─────────────────────────────────────────────────────────────┘
```

## 核心原理：代理模式

为什么需要代理 CMakeLists.txt？

**问题**：CMake 的 `add_subdirectory()` 只能引用**当前目录或子目录**

```
# 假设真实源码分散在不同驱动器
E:/chaos/_engine/source/core/         ← 引擎核心
E:/wolfgang/_games/mygame/source/     ← 游戏代码  
E:/chaos/_plugins/physics/            ← 插件

# 无法在一个 CMakeLists.txt 中直接引用：
add_subdirectory(E:/chaos/_engine/source/core)  # ❌ 错误！不在子目录
```

**解决方案**：在 `build/sourcetree/` 创建"代理文件"

```cmake
# build/sourcetree/_engine/source/core/CMakeLists.txt (代理)
set(REAL_SOURCE_PATH "E:/chaos/_engine/source/core")
include(${REAL_SOURCE_PATH}/CMakeLists.txt)  # 间接包含真实配置
```

现在根 CMakeLists.txt 可以正常使用：
```cmake
add_subdirectory(_engine/source/core)  # ✅ 引用代理目录
# 实际效果：通过代理 include 了 E:/chaos/_engine/source/core/CMakeLists.txt
```

## 脱敏说明

本 Demo 完全使用虚构路径和模块名：
- 用 `_engine`, `_game`, `_plugin` 代替真实仓库名
- 用 `core`, `renderer`, `client` 等通用名称代替实际模块
- 不包含任何真实代码逻辑，仅展示构建架构
