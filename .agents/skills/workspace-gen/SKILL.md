---
name: workspace-gen
description: 根据笔记内容，在 vault 的 workspace/ 目录下生成代码工作区骨架。分析技术栈、代码示例，自动创建目录结构、配置文件和示例代码。
---

# Workspace Gen

根据笔记内容智能生成代码工作区，放置在 `workspace/<kebab-case-note-title>/` 目录下。

## 工作流程

1. **分析笔记** - 提取技术栈(tags/标题/正文)、代码块、文件名提示
2. **推断类型** - 根据技术栈选择模板（见下表）
3. **生成工作区** - 创建目录结构，填充配置文件和代码骨架
4. **输出摘要** - 告知工作区路径、文件列表、运行命令

### 技术栈与模板对应

| 关键词 | 类型 | 核心文件 |
|--------|------|----------|
| cmake, c++, cpp | C++ CMake | `CMakeLists.txt`, `src/main.cpp` |
| make, makefile | C++ Make | `Makefile`, `src/main.cpp` |
| python, .py | Python | `main.py`, `requirements.txt` |
| rust, cargo | Rust | `Cargo.toml`, `src/main.rs` |
| javascript, typescript, node | Node.js | `package.json`, `src/index.ts` |
| go, golang | Go | `go.mod`, `main.go` |
| java, maven, gradle | Java | `pom.xml`/`build.gradle` |
| shell, bash, sh | Shell | `main.sh`, `lib/` |
| 算法, 刷题, LeetCode | 算法练习 | 见下方特殊规范 |
| 其他 | 通用 | `README.md`, `notes/` |

## 执行规范

### 目录命名
笔记标题 → kebab-case → `workspace/<name>/`

例：
- "构建系统：make 与 CMake" → `workspace/make-cmake/`
- "Python 异步编程" → `workspace/python-async/`

### 算法练习工作区（algo-practice）

当标题含"算法"、"LeetCode"或目标目录为 `algo-practice/` 时：

**solution.cpp 规则（重要）**：
- 只生成函数签名骨架 + `// TODO`，**不复制题解代码**
- 顶部必须包含 LeetCode 题目链接：`// LeetCode: https://leetcode.com/problems/<slug>/`

```cpp
// LeetCode: https://leetcode.com/problems/two-sum/
#include "solution.h"

// TODO: 在此实现你的解法
```

**目录结构**：
```
workspace/algo-practice/
├── CMakeLists.txt          # 根构建文件
├── include/common.h        # 轻量测试宏
├── dayXX_<题目名>/
│   ├── CMakeLists.txt
│   ├── solution.h          # 函数/类声明
│   ├── solution.cpp        # ← 在此写解法
│   └── main.cpp            # 本地测试
└── README.md
```

### C++ CMake 项目规范

**CMakeLists.txt 最小配置**（统一使用 GCC + Ninja）：
```cmake
cmake_minimum_required(VERSION 3.20)
project(<name> LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2)
endif()

add_executable(<target> src/main.cpp)
```

**构建命令**：
```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

### C++ 代码规范（防污染）

**宏污染**：
- 头文件中用 `constexpr` 替代 `#define` 常量
- 头文件中用 `inline` 函数替代宏函数
- 必须用宏时加项目前缀（如 `MYPROJECT_DEBUG`），用完 `#undef`

**头文件污染**：
- 所有头文件必须 `#pragma once`
- 头文件中禁止 `using namespace`
- 最小化 `#include`，能用前向声明的绝不包含
- 实现细节放 `.cpp`，不暴露到头文件
- 包含顺序：对应.h → C库 → C++库 → 第三方库 → 项目内头文件

**符号命名污染**：
- 所有代码放在具名 namespace 中
- 禁止与标准库/POSIX 重名（如 `read`, `write`, `max`, `min`, `strlen` 等）
- 禁止使用保留标识符（`__foo`, `_Foo`, `_foo`）
- 类型别名不得遮蔽 `size_t`, `uint32_t` 等标准类型

### README.md 模板

```markdown
# <工作区名称>

> 📝 源自笔记：[[<note-path>]]
> 📅 生成时间：<date>

## 简介
<一句话描述>

## 快速开始
<编译/运行命令>

## 文件结构
<tree>
```

### 冲突处理

若目录已存在，**询问用户**是否覆盖、跳过或加后缀（如 `-2`）。

## 调用方式

```
/workspace-gen                   # 对当前笔记生成
/workspace-gen path/to/note.md   # 对指定笔记生成
```

## 注意事项

- 工作区目录在 vault 根目录，已加入 `.gitignore`
- 代码文件只是**骨架**，需用户自行完善逻辑
- 生成前展示计划，用户确认后执行
