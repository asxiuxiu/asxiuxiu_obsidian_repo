---
name: create-cpp-workspace
description: 在 vault 的 workspace/ 目录下快速创建一个极简 C++ 项目，使用 CMake + Ninja 构建，并自动生成 VS Code 调试配置。当用户说"帮我创建一个 C++ 工作区"、"新建一个 cpp 项目"、"初始化 C++ 环境"或类似请求时调用。
---

# Create C++ Workspace

## 工作流程

1. **获取项目名**：如果用户没提供，询问一个名字（kebab-case 推荐）。
2. **确定路径**：`workspace/<name>/`
3. **检查冲突**：若目录已存在，询问是否覆盖、重命名或取消。
4. **生成文件**：
   - `CMakeLists.txt`
   - `main.cpp`（尽量空，只留基本骨架）
   - `.vscode/launch.json`（GDB 调试配置）
   - `.vscode/tasks.json`（Build 任务）
5. **输出摘要**：告知路径、构建命令、调试方式。

## 目录结构

```
workspace/<name>/
├── CMakeLists.txt
├── main.cpp
└── .vscode/
    ├── launch.json
    └── tasks.json
```

## 文件模板

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(<name> LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
endif()

add_executable(${PROJECT_NAME} main.cpp)
```

### main.cpp

```cpp
#include <iostream>

int main() {
    // TODO: 在这里开始你的代码
    return 0;
}
```

### .vscode/tasks.json

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Configure CMake",
            "type": "shell",
            "command": "cmake",
            "args": [
                "-S", ".",
                "-B", "build",
                "-G", "Ninja",
                "-DCMAKE_CXX_COMPILER=g++"
            ],
            "group": "build"
        },
        {
            "label": "Build with Ninja",
            "type": "shell",
            "command": "cmake",
            "args": [
                "--build",
                "build"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "dependsOn": "Configure CMake"
        }
    ]
}
```

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
            "preLaunchTask": "Build with Ninja"
        }
    ]
}
```

## 构建与调试

**初始化构建（首次）：**
```bash
cd workspace/<name>
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

**后续构建：** 按 `Ctrl+Shift+B` 或运行 `"Build with Ninja"` 任务。

**开始调试：** 在 VS Code 中按 `F5`，选择 `"Debug with GDB"`。

> **注意**：Windows 用户需确保已安装 MinGW-w64 并已将 `g++` 和 `gdb` 添加到系统 PATH。
