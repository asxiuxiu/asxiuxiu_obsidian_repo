---
title: 引擎构建系统通俗解读
date: 2026-03-30
tags:
  - game/build
  - cmake
aliases:
  - 引擎脚本构建分析-通俗版
---

# 引擎构建系统解读

---

## Why：为什么游戏引擎的构建这么复杂？

### 你熟悉的简单 CMake 项目

平时写的小项目通常长这样：

```
my_project/
├── CMakeLists.txt          ← 唯一入口
├── src/
│   ├── main.cpp
│   └── utils.cpp
└── build/                  ← cmake -B build 的输出
```

Workflow 也很简单：

```bash
cmake -B build
cmake --build build
```

**特点**：源码在一个地方，`CMakeLists.txt` 是静态写死的，谁来编译都是这一套。

### 游戏引擎为什么不行？

因为引擎是一个**超级项目**，简单模式根本不够用：

| 简单项目                  | 游戏引擎                                       |
| --------------------- | ------------------------------------------ |
| 源码在一个文件夹              | 源码分散在 **N 个仓库**（引擎核心、游戏逻辑、插件、C# 工具链...）    |
| 依赖几个库，手动配一下就行         | 依赖 **上百个三方库**，必须自动下载                       |
| 只有一个 `CMakeLists.txt` | 不同组的人需要不同的 VS Solution（引擎组、Gameplay 组、工具组） |
| 没有历史包袱                | 升级版本后必须清理旧文件，否则编译报错                        |

所以引擎的 `build.bat` 不是直接跑 `cmake`，而是像**做年夜饭**一样，先完成一堆准备工作，最后才"开火"。

---

## What：这到底是什么？

### 用"做年夜饭"来理解每个脚本

| 脚本                             | 比喻                | 实际在干嘛                                         |
| ------------------------------ | ----------------- | --------------------------------------------- |
| `setup_project_tree.bat`       | **看菜单，确认今天请哪些客人** | 检查 `project_tree_config.json`，决定这次构建要包含哪些代码仓库 |
| `delete_old_path.ps1`          | **清理冰箱里过期的食材**    | 删除旧版本残留文件，防止干扰新构建                             |
| `conan_install.bat`            | **去菜市场买菜**        | 自动下载所有第三方依赖库（Conan 就是包管理器）                    |
| `conanbuild.bat`               | **把买回来的菜摆到案板上**   | 设置环境变量，让 CMake 能找到这些库的路径                      |
| `generate_launch_projects.bat` | **把各房间的食材搬到厨房拼盘** | 把分散在各处的源码"组装"到一个临时目录里                         |
| `cmake -S ... -B ...`          | **真正开始炒菜**        | 生成最终的 Visual Studio 工程                        |

**所以 `build.bat` 本质上就是：先准备食材和厨房，最后才调用你熟悉的 `cmake`。**

### 最困惑的概念：Source Forest（源码森林）

原笔记里大篇幅讲的 Source Forest、代理 `CMakeLists.txt`、JSON 驱动，是整篇最难懂的部分。

#### 问题：CMake 有个"硬伤"

CMake 的 `add_subdirectory()` 有个限制：**只能从当前目录或子目录往里加项目**。

但引擎的源码分布可能是这样的：
- 引擎核心在 `<vault-root>/_engine/source/`
- 游戏代码在 `<vault-root>/_game/source/`
- 插件在 `<vault-root>/_plugin/`
- 工具链在 `<vault-root>/_tools/`

这些目录互相之间**没有父子关系**，你没法在一个 `CMakeLists.txt` 里用 `add_subdirectory` 把它们全拉进来。

#### 解决方案：搭一个"临时厨房"

引擎的做法是：在 `build/sourcetree/` 里**凭空造出一个临时目录结构**，然后在这个临时目录里生成一堆"**代理 CMakeLists.txt**"。

这些代理文件内容极其简单，只做一件事：

```cmake
set(真实源码路径, <vault-root>/_engine/source/core/)
include(${真实源码路径}/CMakeLists.txt)
```

然后 CMake 以 `build/sourcetree/CMakeLists.txt` 为入口，通过代理文件**间接调用**各个分散的源码目录。

> [!tip] 比喻
> 就像你要同时做川菜、粤菜、鲁菜，但三个厨师分别在三个不同的厨房。引擎的做法是：在主厨房（`sourcetree`）里给每个厨师搭一个"**传菜窗口**"（代理 `CMakeLists.txt`），主厨房通过这个窗口调用三个分厨房的菜谱。
>
> **你不需要手动创建这些代理文件**，`generate_launch_projects.bat` 会自动帮你搭好这个临时厨房。

#### 临时厨房是怎么一步步搭起来的？

整个过程分两个阶段：

**第一阶段：Batch 脚本阶段（搭建厨房框架）**

`generate_launch_projects.bat` 调用 `project_generator.exe`，把 `launch/templates/` 模板目录渲染到 `build/sourcetree/`：

```
launch/templates/           ← 模板目录（源码里）
    ├── CMakeLists.txt      ← 顶层 CMake 入口
    ├── cmake/              ← CMake 模块
    │   ├── directory_tree.cmake    ← 解析 JSON 的核心逻辑
    │   ├── json_parser.cmake
    │   └── proxy_cmakelists.txt.in  ← 代理文件模板
    └── launch/             ← 启动配置

        │
        ▼ project_generator.exe
        
build/sourcetree/           ← 生成的临时厨房（只有框架）
```

**第二阶段：CMake 配置阶段（真正的组装）**

执行 `cmake -S build/sourcetree -B build` 时，CMake 调用一系列函数完成组装：

| 函数 | 作用 |
|------|------|
| `ParseSourceForest()` | 解析 `project_tree_config.json`，获取所有 `cmake_projects.json` 路径 |
| `ParseDirectoryTree()` | **深度遍历 JSON**，把 `Projects` 字段转成路径列表 |
| `GenerateProjectSourceTree()` | **生成代理 CMakeLists.txt** |
| `add_subdirectory()` | 调用代理文件，间接加载真实源码 |

**代理文件的生成**

`proxy_cmakelists.txt.in` 模板内容：
```cmake
set(REAL_SOURCE_PATH @REAL_SOURCE_PATH@)
include(${REAL_SOURCE_PATH}/CMakeLists.txt)
```

生成后的代理文件（如 `build/sourcetree/Engine/_engine/source/core/CMakeLists.txt`）：
```cmake
set(REAL_SOURCE_PATH
    <vault-root>/_engine/source/core/)
include(${REAL_SOURCE_PATH}/CMakeLists.txt)
```

### JSON 配置是干什么的？

你可以把两层 JSON 理解为**"菜单系统"**：

#### 第一层：`project_tree_config.json`（宴会总菜单）

它决定：
- 这次生成的 Solution 叫什么名字（比如 `GameEngine.sln`）
- 今天做哪几大类菜：引擎核心、游戏代码、插件、工具

```json
{
    "SolutionName": "GameEngine",
    "SourceTree": [
        "引擎核心的 cmake_projects.json",
        "游戏代码的 cmake_projects.json",
        "插件的 cmake_projects.json",
        "工具链的 cmake_projects.json"
    ]
}
```

#### 第二层：`cmake_projects.json`（每类菜的详细菜谱）

它决定：
- 具体编译哪些模块（`core`、`renderer`、`client`、`server`...）
- 哪些模块根据条件编译（比如 `editor` 只在 Windows 上编，`server` 只在开服务器时编）

> [!info]
> 那几百个 VS 项目，不是人手写在 CMakeLists.txt 里的，而是 CMake 读 JSON "自动种出来的"。

### 为什么要搞这么复杂？

一句话：**为了灵活性**。

- **同一套引擎，可以给不同游戏用**：换一下 `project_tree_config.json` 里的游戏路径就行。
- **同一套源码，可以生成不同的 Solution**：有人只要引擎核心，有人要引擎+游戏，有人只要工具链。
- **源码目录不被构建系统污染**：真实的源码目录保持干净，所有构建相关的"拼盘"都在 `build/` 里。

---

## How：作为使用者，你只需要记住这些

底层那些复杂机制都是自动的，你日常开发只需要知道几个命令：

```bat
REM 默认：生成完整的 VS 2022 工程（引擎+游戏+插件+工具）
build.bat

REM 只编译引擎核心（不带游戏）
build.bat --config build_engine.json

REM 只编译工具链
build.bat --config build_only_tools.json

REM 用 Ninja 代替 VS，生成静态库
build.bat --ninja --static
```

跑完之后，去 `build/` 目录下找 `.sln` 文件，双击打开就行。

### 一张表串起来

| 你想干嘛 | 命令 |
|---------|------|
| 完整构建（默认） | `build.bat` |
| 不暂停直接退出 | `build.bat --nopause` |
| 换输出目录 | `build.bat --build_path D:\mybuild` |
| 只编引擎 | `build.bat --config build_engine.json` |
| 只编工具 | `build.bat --config build_only_tools.json` |
| 编静态库 | `build.bat --static` |
| 用 Ninja | `build.bat --ninja` |

---

## 总结

游戏引擎的构建系统之所以看起来复杂，是因为它要解决三个你平时遇不到的问题：

1. **源码太分散** → 用 `sourcetree` 临时拼盘
2. **依赖太多** → 用 Conan 自动下载
3. **不同人要不同的工程** → 用 JSON 配置动态生成

**但对你来说，入口始终只有一个 `build.bat`。理解它背后的逻辑，是为了在出问题时知道该看哪个脚本，而不是被 425 个项目吓到。**
