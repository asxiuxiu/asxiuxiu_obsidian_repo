---
name: workspace-gen
description: 根据当前笔记内容，在 vault 的 workspace/ 目录下生成对应的代码工作区骨架。分析笔记中的技术主题、代码示例、项目结构等，自动创建合适的目录结构、配置文件和示例代码。Use when the user wants to create a workspace, project scaffold, or code directory based on a note's content.
---

# Workspace Gen

根据笔记内容智能生成本地代码工作区，放置在 vault 内的 `workspace/` 目录下。

## 工作流程

### 第一步：分析笔记

读取当前笔记（通过 `<current_note>` 或 `$ARGUMENTS` 指定路径），提取以下信息：

- **技术栈**：笔记 frontmatter 的 `tags`、标题、正文中出现的语言/框架/工具关键词
- **代码示例**：笔记中的代码块（提取语言类型和内容）
- **项目结构提示**：笔记中明确提到的文件名、目录名、配置文件名
- **主题名称**：用于命名工作区目录（从笔记标题提取，转为 kebab-case）

### 第二步：推断工作区类型

根据检测到的技术栈，选择对应的工作区模板：

| 检测到的关键词 | 工作区类型 | 生成的核心文件 |
|---|---|---|
| `cmake`, `CMakeLists`, `c++`, `cpp` | C++ CMake 项目 | `CMakeLists.txt`, `src/main.cpp`, `include/`, `build/` |
| `makefile`, `make` + C/C++ | C++ Make 项目 | `Makefile`, `src/main.cpp`, `include/` |
| `python`, `pip`, `.py` | Python 项目 | `main.py`, `requirements.txt`, `venv/`（说明）, `.gitignore` |
| `rust`, `cargo`, `.rs` | Rust 项目 | `Cargo.toml`, `src/main.rs` |
| `javascript`, `node`, `npm`, `typescript` | Node.js 项目 | `package.json`, `src/index.js` 或 `index.ts`, `.gitignore` |
| `go`, `golang` | Go 项目 | `go.mod`, `main.go` |
| `java`, `maven`, `gradle` | Java 项目 | `pom.xml` 或 `build.gradle`, `src/main/java/Main.java` |
| `shell`, `bash`, `sh` | Shell 脚本项目 | `main.sh`, `lib/`, `README.md` |
| 其他/通用 | 通用项目 | `README.md`, `notes/`, `.gitignore` |

### 第三步：生成工作区

1. **确定目录名**：`workspace/<note-title-in-kebab-case>/`
2. **创建目录结构**：根据工作区类型创建对应目录和文件
3. **填充内容**：
   - 配置文件使用笔记中提取的项目名、依赖等真实信息
   - 代码文件直接复用笔记中的代码示例（如有）
   - 添加注释说明代码来源（`# From note: <note-title>`）
4. **生成 README.md**：简要说明工作区用途、来源笔记、如何运行

### 第四步：输出摘要

告知用户：
- 工作区路径（可点击的 wikilink）
- 生成了哪些文件
- 如何开始使用（编译/运行命令）

---

## 执行规范

### 目录命名规则

```
笔记标题 → kebab-case → workspace/<name>/
例：
  "构建系统：make 与 CMake"  →  workspace/make-cmake/
  "Python 异步编程"          →  workspace/python-async/
  "React Hooks 实践"         →  workspace/react-hooks/
```

### README.md 模板

每个工作区必须包含 `README.md`：

```markdown
# <工作区名称>

> 📝 源自笔记：[[<note-path>]]
> 📅 生成时间：<date>

## 简介

<从笔记标题和一句话总结提取>

## 快速开始

<根据技术栈填写编译/运行命令>

## 文件结构

<tree 格式展示>
```

### C++ CMake 工作区示例

当检测到 C++/CMake 相关笔记时，生成：

```
workspace/make-cmake/
├── README.md
├── CMakeLists.txt        # 基于笔记内容的最小可用配置
├── Makefile              # 如笔记同时讲了 make，也生成
├── src/
│   └── main.cpp          # 包含笔记中的示例代码片段
├── include/
│   └── .gitkeep
└── .gitignore
```

**CMakeLists.txt 内容参考笔记中的 CMake 示例，至少包含：**
```cmake
cmake_minimum_required(VERSION 3.10)
project(<ProjectName>)
set(CMAKE_CXX_STANDARD 17)
add_executable(<target> src/main.cpp)
```

### C++ 代码规范：宏污染与头文件污染

生成 C++ 代码时，必须遵守以下规范，防止宏污染和头文件污染：

#### 宏定义污染（Macro Pollution）

- **禁止在头文件中使用 `#define` 定义常量**，改用 `constexpr` 或 `inline constexpr`：
  ```cpp
  // ❌ 错误
  #define MAX_SIZE 1024
  // ✅ 正确
  inline constexpr int kMaxSize = 1024;
  ```
- **禁止用宏定义函数**，改用 `inline` 函数或模板：
  ```cpp
  // ❌ 错误
  #define MAX(a, b) ((a) > (b) ? (a) : (b))
  // ✅ 正确
  template<typename T>
  inline T Max(T a, T b) { return a > b ? a : b; }
  ```
- **宏名必须加项目前缀**，若无法避免使用宏，必须加唯一前缀防止冲突：
  ```cpp
  // ❌ 错误
  #define DEBUG 1
  // ✅ 正确
  #define MYPROJECT_DEBUG 1
  ```
- **使用完后立即 `#undef`**，局部宏用完即清除：
  ```cpp
  #define MYPROJECT_TEMP_HELPER(x) ...
  // ... 使用 ...
  #undef MYPROJECT_TEMP_HELPER
  ```

#### 头文件污染（Header Pollution）

- **所有头文件必须有 Include Guard 或 `#pragma once`**（优先用 `#pragma once`）：
  ```cpp
  #pragma once
  // 或
  #ifndef MYPROJECT_FOO_H_
  #define MYPROJECT_FOO_H_
  // ...
  #endif  // MYPROJECT_FOO_H_
  ```
- **头文件中禁止 `using namespace`**，避免污染所有引用该头文件的翻译单元：
  ```cpp
  // ❌ 头文件中绝对禁止
  using namespace std;
  // ✅ 在 .cpp 实现文件中可以使用（仅限局部作用域）
  ```
- **最小化头文件 `#include`**，头文件只包含其声明所必须的依赖；能前向声明（forward declaration）的绝不 `#include`：
  ```cpp
  // ❌ 头文件中不必要的包含
  #include <vector>
  #include <string>
  // ✅ 如果只用到指针/引用，用前向声明
  class Foo;  // 替代 #include "foo.h"
  ```
- **实现细节放 `.cpp`，不暴露到头文件**：内部使用的 `#include`、辅助类、匿名 namespace 统一放在 `.cpp` 文件中。
- **头文件包含顺序**（防止隐式依赖）：
  1. 对应的 `.h` 文件（`foo.cpp` 先包含 `foo.h`）
  2. C 标准库头文件
  3. C++ 标准库头文件
  4. 第三方库头文件
  5. 项目内其他头文件

#### 符号命名污染（Symbol Name Pollution）

- **禁止与系统库、标准库重名**，不得定义与 C/C++ 标准库、POSIX、常见系统库中已有的函数名、类型名、全局变量同名的符号：
  ```cpp
  // ❌ 危险：与 <string.h> 中的 strlen、<math.h> 中的 max、POSIX 的 read 等重名
  int read(const char* buf);
  double max(double a, double b);
  size_t strlen(const char* s);

  // ✅ 加项目命名空间或前缀
  namespace myproject {
      int read(const char* buf);
      double max(double a, double b);
  }
  ```

- **所有项目代码放在具名 namespace 中**，避免全局命名空间污染：
  ```cpp
  // ❌ 直接暴露在全局命名空间
  class Parser { ... };
  void init();

  // ✅ 包裹在项目 namespace 内
  namespace myproject {
      class Parser { ... };
      void init();
  }
  ```

- **禁止使用保留标识符**，以下命名模式由 C/C++ 标准保留，不得使用：
  - 双下划线开头：`__foo`
  - 下划线 + 大写字母开头：`_Foo`
  - 全局作用域下单下划线开头：`_foo`
  ```cpp
  // ❌ 全部属于保留标识符，行为未定义
  int __buffer;
  class _Handler;
  void _init();
  ```

- **类型别名不得遮蔽标准类型**，`typedef` / `using` 不得与 `size_t`、`uint8_t`、`int32_t` 等标准类型同名：
  ```cpp
  // ❌ 遮蔽标准类型，引发隐蔽 bug
  typedef unsigned int size_t;
  using int32_t = int;

  // ✅ 使用项目专属名称
  using MySize = std::size_t;
  ```

- **常见高危重名清单**（生成代码时自动规避）：

  | 危险名称 | 来源 | 替代建议 |
  |---|---|---|
  | `read` / `write` / `open` / `close` | POSIX `<unistd.h>` | 加 namespace 或前缀 |
  | `max` / `min` / `abs` | `<algorithm>` / `<cmath>` | 加 namespace |
  | `printf` / `scanf` | `<cstdio>` | 不重名，直接用标准版 |
  | `error` / `errno` | `<cerrno>` | 加 namespace |
  | `index` / `rindex` | POSIX `<strings.h>` | 改名为 `find_index` 等 |
  | `toupper` / `tolower` | `<cctype>` | 加 namespace |
  | `assert` | `<cassert>` | 改名或用宏前缀 |
  | `TRUE` / `FALSE` / `NULL` | C 宏 | 使用 `true`/`false`/`nullptr` |

### 冲突处理

- 若 `workspace/<name>/` 已存在：**询问用户**是否覆盖、跳过或使用新名称（加 `-2` 后缀）
- 不覆盖任何已有文件，除非用户明确同意

---

## 调用方式

```
/workspace-gen                    # 对当前笔记生成工作区
/workspace-gen path/to/note.md    # 对指定笔记生成工作区
```

## 注意事项

- 工作区目录 `workspace/` 在 vault 根目录下，已在 `.gitignore` 排除（如有 git）
- 代码文件只是**骨架**，用户需要自行完善业务逻辑
- 如果笔记没有明显技术内容，生成通用项目结构并提示用户
- 始终在生成前展示计划，让用户确认后再执行
