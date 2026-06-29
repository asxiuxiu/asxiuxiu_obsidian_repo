---
name: workspace-gen
description: 在 vault 的 workspace/ 目录下生成代码工作区骨架。支持两种模式：根据笔记内容智能推断技术栈并生成项目；或快速创建一个空的 C++ 开发环境（含 VS Code 调试配置）。当用户说"帮我创建一个 C++ 工作区"、"新建一个 cpp 项目"、"初始化 C++ 环境"、"根据笔记生成工作区"或使用 /workspace-gen 命令时调用。
---

# Workspace Gen

在 `workspace/<kebab-case-name>/` 目录下生成代码工作区骨架。

## 两种工作模式

| 模式 | 触发条件 | 输出特点 |
|------|---------|---------|
| **笔记驱动** | 提供笔记路径、使用 `/workspace-gen`、说"根据笔记生成" | 分析笔记技术栈，智能推断类型，生成对应项目骨架 + README.md |
| **快速空项目** | "创建 C++ 工作区"、"新建 cpp 项目"、"初始化 C++ 环境" | 极简空 C++ CMake 项目 + VS Code 调试配置 |

---

## 模式一：笔记驱动

### 工作流程

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

### C++ CMake 项目规范（笔记驱动模式）

**CMakeLists.txt 最小配置**（统一使用 GCC + Ninja）：
```cmake
cmake_minimum_required(VERSION 3.20)
project(<name> LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 为 clangd 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
endif()

add_executable(<target> src/main.cpp)
```

**构建命令**：
```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
# 复制 compile_commands.json 到项目根目录，供 clangd 自动加载
cp -f build/compile_commands.json . 2>/dev/null || true
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

---

## 模式二：快速空项目（C++）

当用户说"帮我创建一个 C++ 工作区"、"新建一个 cpp 项目"、"初始化 C++ 环境"且**未提供笔记路径**时，按此模式执行。

### 目录结构

```
workspace/<name>/
├── CMakeLists.txt
├── .clang-format           # 复用用户默认格式化配置
├── .gitignore
├── main.cpp
├── .vscode/
│   ├── settings.json       # LSP / clangd / 格式化配置
│   ├── launch.json
│   └── tasks.json
└── .zed/
    ├── settings.json       # LSP / clangd 配置
    └── tasks.json
```

> 用户同时在 VS Code 和 Zed 中开发，因此编辑器任务/配置必须成对生成。LSP 配置统一指向 `compile_commands.json`，格式化统一使用用户默认的 `.clang-format`。

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(<name> LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 必须开启，供 clangd 做语义补全与跳转
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
endif()

add_executable(${PROJECT_NAME} main.cpp)
```

> `CMAKE_EXPORT_COMPILE_COMMANDS` 会在 `build/` 下生成 `compile_commands.json`；后续再通过 `build.sh` 或 CMake 自定义目标把它复制到项目根目录，方便编辑器自动加载。

### build.sh

```bash
#!/usr/bin/env bash
set -euo pipefail

# 优先使用 UCRT64 工具链，避免与 mingw64 的 DLL 混用
if [[ -d "/c/msys64/ucrt64/bin" ]]; then
    export PATH="/c/msys64/ucrt64/bin:${PATH}"
fi

cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

# 复制 compile_commands.json 到项目根目录，供 clangd 自动加载
if [[ -f build/compile_commands.json ]]; then
    cp -f build/compile_commands.json .
fi
```

> `build.sh` 负责统一构建环境（PATH、编译器、compile_commands 复制）。编辑器任务只调用 `bash build.sh`，避免在每个编辑器配置里重复环境处理逻辑。

### .gitignore

```gitignore
# 构建产物
build/
out/
.cache/

# 编译数据库（由 CMake 生成，每次构建后重新复制到根目录）
compile_commands.json

# 可执行文件 / 调试符号
*.exe
*.exe.dSYM/
*.pdb
```

### main.cpp

```cpp
#include <iostream>

int main() {
    // TODO: 在这里开始你的代码
    return 0;
}
```

### .vscode/settings.json

```json
{
    "C_Cpp.intelliSenseEngine": "disabled",
    "C_Cpp.formatting": "clangFormat",
    "clangd.path": "clangd",
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}",
        "--background-index",
        "--clang-tidy",
        "--header-insertion=iwyu"
    ],
    "editor.formatOnSave": true,
    "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd"
}
```

> `.vscode/settings.json` 负责关闭 VS Code 默认 C/C++ 引擎、启用 clangd，并指定 `compile_commands.json` 位置。若未安装 `vscode-clangd` 插件，可去掉 `defaultFormatter` 行或改用 `"ms-vscode.cpptools"`。

### .vscode/tasks.json

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Configure + Build",
            "type": "shell",
            "command": "bash",
            "args": [
                "build.sh"
            ],
            "options": {
                "shell": {
                    "executable": "<path-to-bash.exe>"
                }
            },
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Format current file",
            "type": "shell",
            "command": "clang-format",
            "args": [
                "-i",
                "${file}"
            ],
            "options": {
                "shell": {
                    "executable": "<path-to-bash.exe>"
                }
            },
            "problemMatcher": []
        }
    ]
}
```

> Windows 下 VS Code 默认终端可能是 PowerShell，因此每个 shell 任务都要通过 `options.shell.executable` 显式指定 Git Bash 路径。`<path-to-bash.exe>` 需替换为实际路径，常见如 `C:\Program Files\Git\bin\bash.exe`。

### .vscode/launch.json

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug with GDB",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/${workspaceFolderBasename}.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "Configure + Build"
        }
    ]
}
```

### .zed/settings.json

```json
{
    "lsp": {
        "clangd": {
            "arguments": [
                "--compile-commands-dir=${workspaceFolder}",
                "--background-index",
                "--clang-tidy",
                "--header-insertion=iwyu"
            ]
        }
    },
    "format_on_save": true,
    "formatter": "language_server"
}
```

> Zed 默认使用 clangd 作为 C/C++ 语言服务器；`--compile-commands-dir` 指向项目根目录的 `compile_commands.json`（由 `build.sh` 在构建后复制）。

### .zed/tasks.json

```json
[
    {
        "label": "Configure + Build",
        "command": "bash",
        "args": ["build.sh"],
        "cwd": "$ZED_WORKTREE_ROOT",
        "use_new_terminal": false,
        "allow_concurrent_runs": false,
        "reveal": "always",
        "hide": "never",
        "shell": {
            "program": "<path-to-bash.exe>"
        }
    },
    {
        "label": "Format current file",
        "command": "clang-format",
        "args": ["-i", "$ZED_FILE"],
        "cwd": "$ZED_WORKTREE_ROOT",
        "use_new_terminal": false,
        "allow_concurrent_runs": false,
        "reveal": "always",
        "hide": "never",
        "shell": {
            "program": "<path-to-bash.exe>"
        }
    }
]
```

> Zed 任务文件位于 `.zed/tasks.json`。`shell.program` 必须显式指向 Git Bash 的绝对路径，否则 Zed 默认会用 PowerShell/CMD 执行，导致 `bash` 命令找不到。`<path-to-bash.exe>` 需替换为实际路径，常见如 `C:\Program Files\Git\bin\bash.exe`。
>
> 当工作区已有 `build.sh` 时优先调用它，以复用 UCRT64 PATH 等环境处理逻辑；否则直接写 `cmake -S . -B build ... && cmake --build build`。

### 构建与调试

**初始化构建（首次）**：
```bash
cd workspace/<name>
bash build.sh
```

**后续构建：** 按 `Ctrl+Shift+B` 或运行 `"Configure + Build"` 任务。

**开始调试：** 在 VS Code 中按 `F5`，选择 `"Debug with GDB"`。

> **注意**：Windows 用户需确保已安装 MinGW-w64 并已将 `g++` 和 `gdb` 添加到系统 PATH。

---

## 通用规范

### 编辑器配置

用户在 VS Code 和 Zed 中都会开发。任何需要生成编辑器任务、启动配置或相关 JSON 的 C++ 工作区，必须同时提供：

- `.vscode/settings.json`（LSP / 格式化 / clangd 配置）
- `.vscode/tasks.json`（必要时加 `.vscode/launch.json`）
- `.zed/settings.json`（LSP / 格式化 / clangd 配置）
- `.zed/tasks.json`（必要时加 `.zed/debug.json`）

两套配置保持行为一致，并遵循以下任务规范：

1. **显式指定运行时 shell**：所有 shell 任务必须写明 shell 可执行文件绝对路径，不能依赖编辑器默认终端。Windows 下优先使用 Git Bash（常见路径如 `C:\Program Files\Git\bin\bash.exe`、`C:\Program Files\Git\usr\bin\bash.exe`）。
2. **封装复杂环境逻辑**：构建/运行所需的 PATH 调整、编译器选择等，优先写在项目根目录的 `build.sh`/`run.sh` 中；编辑器任务只负责调用脚本，保持简洁。
3. **提供可替换占位符**：模板中 shell 路径使用占位符或注释说明，避免让用户误以为只能固定在某一个绝对路径。

### compile_commands.json 链路

C++ CMake 工作区必须让 clangd 能拿到准确的编译命令：

1. `CMakeLists.txt` 中设置 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`。
2. 首次构建后，必须有一份 `compile_commands.json` 位于项目根目录，或明确配置 `--compile-commands-dir`。
3. 推荐在 `build.sh` 末尾追加复制命令（比 CMake 自定义目标更可控）：
   ```bash
   if [[ -f build/compile_commands.json ]]; then
       cp -f build/compile_commands.json .
   fi
   ```
4. `.vscode/settings.json` 与 `.zed/settings.json` 统一把 `--compile-commands-dir` 指向 `${workspaceFolder}`（即项目根目录）。
5. `.gitignore` 必须忽略 `build/` 与 `compile_commands.json`，避免把生成产物提交。

### 格式化链路

C++ 工作区必须统一使用用户默认的 `.clang-format`：

1. 生成工作区时，若用户 home 目录存在 `~/.clang-format`（Windows 下为 `C:\Users\<User>\.clang-format`），则复制到工作区根目录作为 `.clang-format`。
2. 若不存在，生成一个最小默认 `.clang-format`，风格为 `BasedOnStyle: Microsoft`，并提示用户后续可自行替换。
3. `.vscode/settings.json` 开启 `editor.formatOnSave`，`.zed/settings.json` 开启 `format_on_save`。
4. 两套编辑器任务都提供 `"Format current file"` 任务，调用 `clang-format -i <file>`。
5. 若编辑器未找到 `clang-format`，任务会失败并提示安装 LLVM/clangd 工具链。

### 冲突处理

若目录已存在，**询问用户**是否覆盖、跳过或加后缀（如 `-2`）。

### 调用方式

```
/workspace-gen                   # 对当前笔记生成（笔记驱动模式）
/workspace-gen path/to/note.md   # 对指定笔记生成（笔记驱动模式）
"帮我创建一个 C++ 工作区"        # 快速空项目模式
```

### 注意事项

- 工作区目录在 vault 根目录，已加入 `.gitignore`
- 代码文件只是**骨架**，需用户自行完善逻辑
- 生成前展示计划，用户确认后执行
