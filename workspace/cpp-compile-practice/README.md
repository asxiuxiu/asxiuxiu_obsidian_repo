# C++ 编译选项实践

这个目录包含简单的 C++ 示例，用于实践 [[C++/C++编译选项.md]] 中的各种编译选项。

## 文件说明

| 文件 | 说明 |
|------|------|
| `main.cpp` | 主程序，包含条件编译示例 |
| `utils.cpp` | 工具函数实现 |
| `utils.h` | 工具函数头文件 |
| `test_warnings.cpp` | 用于测试警告选项（故意包含一些问题） |

### 中文与编码

源文件为 UTF-8，编译时建议加 `-finput-charset=UTF-8 -fexec-charset=UTF-8`。

**乱码原因**：Windows 系统控制台代码页默认是 936 (GBK)。Cursor/VS Code 集成终端底层走 `conpty`，按此代码页解释输出，而程序输出的是 UTF-8 字节，编码不匹配导致乱码。仅设 `LANG=en_US.UTF-8` 不够，必须切换控制台代码页。

- **Cursor / VS Code 集成终端（Git Bash 或 CMD）**：运行前先切换代码页：
  ```bash
  chcp.com 65001        # 整个终端会话期间生效
  ./myapp
  ```
- **Windows CMD（独立窗口）**：同理，先执行 `chcp 65001` 再运行程序。
- **独立 Git Bash（mintty）**：mintty 不走 conpty，需单独设编码——窗口标题栏 **右键 → Options → Text**，把 **Character set** 设为 **UTF-8**。
- **VS Code 任务**：`tasks.json` 的 **run** 任务已通过 `chcp 65001` 处理，直接用任务运行即可。

**永久修复（推荐）**

在 `~/.bashrc` 末尾加一行，新开的 Git Bash 终端会自动切换代码页：

```bash
chcp.com 65001 > /dev/null 2>&1
```

> 注意：Git Bash 作为登录 shell 会先读 `~/.bash_profile` 而不一定读 `~/.bashrc`。请确保 `~/.bash_profile` 中有 `[ -f ~/.bashrc ] && . ~/.bashrc`，或把上面这行同时加到两个文件中。

## 实践任务

### 1. 基础编译

```bash
# 直接编译单个文件
g++ main.cpp -o main

# 编译多个文件
g++ main.cpp utils.cpp -o myapp

# 运行
./myapp
```

### 2. 分步编译（了解编译过程）

```bash
# 1. 预处理 - 展开头文件、宏替换
g++ -E main.cpp -o main.i

# 2. 编译 - 生成汇编代码
g++ -S main.i -o main.s

# 3. 汇编 - 生成目标文件
g++ -c main.s -o main.o

# 4. 链接 - 生成可执行文件
g++ main.o -o main
```

### 3. 定义宏

```bash
# 定义 DEBUG 宏
g++ -DDEBUG main.cpp utils.cpp -o myapp

# 定义带值的宏
g++ -DVERSION=\"2.0.0\" main.cpp utils.cpp -o myapp

# 同时定义多个宏
g++ -DDEBUG -DVERSION=\"2.0.0\" main.cpp utils.cpp -o myapp
```

### 4. 警告选项

```bash
# 开启所有警告
g++ -Wall main.cpp utils.cpp -o myapp

# 开启额外警告
g++ -Wall -Wextra main.cpp utils.cpp -o myapp

# 将警告视为错误（强制修复）
g++ -Wall -Wextra -Werror main.cpp utils.cpp -o myapp

# 测试警告选项（这个文件故意有问题）
g++ -Wall -Wextra -Wshadow -Wconversion test_warnings.cpp -o test_warnings
```

### 5. 调试信息

```bash
# 生成调试信息（用于 GDB 调试）
g++ -g main.cpp utils.cpp -o myapp

# 生成最多调试信息
g++ -g3 main.cpp utils.cpp -o myapp
```

### 6. 优化选项

```bash
# 无优化（默认，调试友好）
g++ -O0 main.cpp utils.cpp -o myapp

# 基本优化
g++ -O1 main.cpp utils.cpp -o myapp

# 更多优化（推荐用于发布）
g++ -O2 main.cpp utils.cpp -o myapp

# 最高级别优化
g++ -O3 main.cpp utils.cpp -o myapp

# 优化同时保留调试信息
g++ -Og -g main.cpp utils.cpp -o myapp
```

### 7. C++ 标准版本

```bash
# 使用 C++11
g++ -std=c++11 main.cpp utils.cpp -o myapp

# 使用 C++14
g++ -std=c++14 main.cpp utils.cpp -o myapp

# 使用 C++17
g++ -std=c++17 main.cpp utils.cpp -o myapp

# 使用 C++20
g++ -std=c++20 main.cpp utils.cpp -o myapp
```

### 8. 查看编译过程

```bash
# 显示详细编译过程
g++ -v main.cpp utils.cpp -o myapp

# 显示编译命令但不执行
g++ -### main.cpp utils.cpp -o myapp

# 保存中间文件
g++ -save-temps main.cpp utils.cpp -o myapp
```

## 推荐组合

### 开发模式
```bash
g++ -std=c++17 -Wall -Wextra -g -O0 main.cpp utils.cpp -o myapp
```

### 发布模式
```bash
g++ -std=c++17 -O2 -DNDEBUG main.cpp utils.cpp -o myapp
```

## 下一步

熟悉这些基础编译选项后，可以学习：
- [[C++/Makefile 编写指南.md|Makefile 编写指南]] - 自动化编译
- [[C++/CMake 入门.md|CMake 入门]] - 跨平台构建
