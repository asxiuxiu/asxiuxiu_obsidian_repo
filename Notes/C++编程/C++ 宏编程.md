---
title: C++ 宏编程
date: 2026-04-13
tags:
  - C++
  - 宏
  - 元编程
  - 预处理
aliases:
  - C++ Macro
  - 预处理器宏
---

> [[Notes/C++编程/索引|← 返回 C++ 索引]]

# C++ 宏编程

> [!info] 一句话概括
> 宏是**纯文本替换**的预处理工具。虽然现代 C++ 更推荐模板和 `constexpr`，但在**游戏引擎**等大规模 C++ 代码库中，宏仍然是生成重复样板代码、实现底层反射和跨平台抽象的**核心武器**。

---

## Why：为什么游戏引擎如此依赖宏？

### 问题背景

游戏引擎通常包含成百上千个类、枚举、属性，需要大量的**重复性代码**：

- 为每个类生成反射信息（类名、字段列表、类型）
- 为每个枚举生成 `ToString` / `FromString` 转换
- 在每个属性上标记序列化、编辑器可见性、网络同步等元数据
- 跨平台封装：不同编译器、不同 API 的适配层

如果全靠手写，不仅繁琐，而且容易遗漏和出错。

### 不用宏的后果

- **样板代码爆炸**：每个新增类都要手写 50 行反射注册代码
- **维护困难**：修改一个枚举要同时改 3-5 处相关代码
- **元数据与定义分离**：属性声明和反射信息不同步，导致运行时错误

### 应用场景

| 场景 | 宏的作用 |
|------|---------|
| **反射系统** | `REFLECT_CLASS()` 宏在类体内展开为类型信息静态变量和注册函数 |
| **字段注册** | `PROPERTY()` 宏展开为将字段指针、名称、偏移量填入反射表的代码 |
| **枚举生成** | 用一个 `.def` 文件配合 X-Macro 同时生成枚举定义和字符串转换函数 |
| **平台抽象** | `#ifdef _WIN32` 封装平台相关 API，统一调用接口 |
| **断言与日志** | `ASSERT(x)` 在 Debug 模式下展开为带文件名/行号的断点，Release 下为空 |

> [!tip] 现代 C++ 中的定位
> - 能用 **模板** 和 **`constexpr`** 解决的需求，优先不用宏。
> - 宏的优势在于：它操作的是**标识符和代码片段本身**，可以在编译器语义分析之前就**生成类定义和代码结构**。

---

## What：宏的本质与核心机制

### 核心定义

C++ 宏是**预处理器（Preprocessor）**在编译前执行的**纯文本替换**。它不理解 C++ 的类型系统、作用域、语法树，只进行 TOKEN 级别的替换、拼接和删除。

预处理阶段发生在编译之前，流程如下：

```
源代码 (.cpp/.h)
    ↓
预处理器 (cpp/clang -E)
    ├── 宏展开
    ├── 文件包含 (#include)
    ├── 条件编译 (#if/#ifdef)
    └── 行号控制 (#line)
    ↓
编译器 (cc1/clang -cc1) ← 处理纯 C++ 代码
```

### 宏的分类

| 类型 | 定义方式 | 示例 |
|------|---------|------|
| **对象宏 (Object-like)** | `#define NAME value` | `#define PI 3.14` |
| **函数宏 (Function-like)** | `#define NAME(args) replacement` | `#define SQUARE(x) ((x) * (x))` |

### 关键运算符

#### 1. `#` 字符串化运算符

将宏参数转换为**C 字符串字面量**。

```cpp
#define STR(x) #x

STR(hello world)  // 展开为 "hello world"
```

#### 2. `##` 标识符拼接运算符

将两个 TOKEN 拼接成一个新的标识符。

```cpp
#define CONCAT(a, b) a ## b

CONCAT(my, Var)   // 展开为 myVar
```

> [!warning] `##` 的陷阱
> 如果拼接结果不是合法的 C++ 标识符（如 `123` 和 `abc` 拼成 `123abc`），会导致编译错误。

#### 3. `__VA_ARGS__` 变参宏 (C99/C++11)

接收可变数量的参数。

```cpp
#define LOG(fmt, ...) printf("[LOG] " fmt "\n", ##__VA_ARGS__)

LOG("Value: %d", 42);   // 展开为 printf("[LOG] Value: %d\n", 42);
```

> [!note] `##__VA_ARGS__` 的妙用（GCC/Clang 扩展，MSVC 也支持）
> 当 `__VA_ARGS__` 为空时，前面的逗号会被自动删除，避免 `printf(fmt, )` 这种语法错误。
> 标准 C++20 引入了 `__VA_OPT__(,)` 来替代这一扩展。

### 宏展开规则（重要）

预处理器的展开遵循两条**蓝皮书规则**：

1. **字符串化（#x）不展开参数**：如果参数被 `#` 修饰，参数本身不会被进一步宏展开。
2. **拼接（a##b）不展开参数**：如果参数参与了 `##`，参数本身不会被进一步宏展开。

**绕过方法**：使用一个**间接层（辅助宏）**。

```cpp
#define A 42

// 错误：STR(A) 直接展开为 "A"，因为 # 阻止了 A 的进一步展开
#define STR(x) #x
STR(A)  // "A"

// 正确：通过辅助宏 STR_HELPER 先展开 A，再字符串化
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
STR(A)  // "42"
```

同样的道理适用于 `##`：

```cpp
#define A Foo
#define B Bar

// 错误：CONCAT(A, B) 直接拼接为 AB（未展开的 TOKEN）
#define CONCAT(a, b) a ## b
CONCAT(A, B)  // AB（错误）

// 正确：
#define CONCAT_HELPER(a, b) a ## b
#define CONCAT(a, b) CONCAT_HELPER(a, b)
CONCAT(A, B)  // FooBar（正确）
```

> [!important] 间接层是宏编程的**核心技巧**
> 几乎所有复杂的宏展开都需要至少一层间接来绕过预处理器的限制。

---

## How：从基础到引擎级代码生成

### 1. 基础用法：安全地写函数宏

#### 1.1 参数必须加括号

```cpp
// 错误：SQUARE(1 + 2) 展开为 1 + 2 * 1 + 2 = 5
#define SQUARE(x) x * x

// 正确：
#define SQUARE(x) ((x) * (x))
```

#### 1.2 多语句宏的两种写法

```cpp
// 方式一：do { ... } while(0)
#define SAFE_DELETE(ptr) do { \
    delete (ptr);             \
    (ptr) = nullptr;          \
} while(0)

// 方式二：GNU 扩展 ({ ... }) —— 仅限 GCC/Clang
#define MAX(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;       \
})
```

> [!warning] 不要用 `if (...) { ... }` 包多语句
> `if (cond) SAFE_DELETE(p); else ...` 会因为分号问题导致 `else` 悬空错误。`do { ... } while(0)` 是标准且安全的选择。

### 2. 用宏生成枚举与字符串映射（X-Macros）

X-Macro（Cross-Macros）是宏编程中最具威力的技术之一。它的核心思想是：

> 将数据定义在一个 `.def` 或 `.inc` 文件中，然后在不同上下文中**重复包含**该文件，每次传入不同的宏来展开。

#### 2.1 定义数据文件

创建 `Color.def`：

```cpp
// Color.def
COLOR(Red,   0xFF0000)
COLOR(Green, 0x00FF00)
COLOR(Blue,  0x0000FF)
```

#### 2.2 生成枚举定义

```cpp
enum class Color : uint32_t
{
    #define COLOR(name, value) name = value,
    #include "Color.def"
    #undef COLOR
};
```

展开后等价于：

```cpp
enum class Color : uint32_t
{
    Red = 0xFF0000,
    Green = 0x00FF00,
    Blue = 0x0000FF,
};
```

#### 2.3 生成 ToString 函数

```cpp
inline const char* ColorToString(Color c)
{
    switch (c)
    {
        #define COLOR(name, value) case Color::name: return #name;
        #include "Color.def"
        #undef COLOR
        default: return "Unknown";
    }
}
```

展开后等价于：

```cpp
inline const char* ColorToString(Color c)
{
    switch (c)
    {
        case Color::Red:   return "Red";
        case Color::Green: return "Green";
        case Color::Blue:  return "Blue";
        default: return "Unknown";
    }
}
```

> [!tip] 扩展：从字符串反查枚举
> 同样的 `Color.def` 还可以用来生成 `std::unordered_map<std::string, Color>` 的初始化代码。枚举、序列化、反序列化、UI 下拉框——**一处定义，多处生成**。

### 3. 用宏自动生成类反射信息

这是游戏引擎中最“神奇”的用法。下面实现一个**简化版**的类反射系统，展示宏如何直接生成类的元数据和字段注册代码。

#### 3.1 反射系统的数据结构

```cpp
#include <vector>
#include <string>
#include <cstddef>

struct FieldInfo
{
    const char* name;
    size_t offset;
    size_t size;
};

struct ClassInfo
{
    const char* name;
    std::vector<FieldInfo> fields;
};
```

#### 3.2 定义类反射宏

```cpp
// 在类体内展开
#define REFLECT_CLASS(ClassName)                                    \
    static inline ClassInfo s_ClassInfo{#ClassName, {}};            \
    static const ClassInfo* GetClassInfo() { return &s_ClassInfo; }

// 在类体内注册一个字段
#define PROPERTY(Type, FieldName)                                   \
    struct _Reg_##FieldName {                                       \
        _Reg_##FieldName() {                                        \
            s_ClassInfo.fields.push_back({                          \
                #FieldName,                                         \
                offsetof(ClassName, FieldName),                     \
                sizeof(Type)                                        \
            });                                                     \
        }                                                           \
    } _regInstance_##FieldName;
```

> [!note] `#ClassName` 直接将类名字符串化；`offsetof` 计算字段偏移；构造函数内的静态全局注册确保主函数执行前已收集好信息。

#### 3.3 在业务类中使用

```cpp
class Actor
{
public:
    REFLECT_CLASS(Actor)

    PROPERTY(float, Health)
    float Health = 100.0f;

    PROPERTY(std::string, Name)
    std::string Name;
};
```

#### 3.4 运行时查看反射信息

```cpp
#include <iostream>

int main()
{
    const ClassInfo* info = Actor::GetClassInfo();
    std::cout << "Class: " << info->name << "\n";
    for (const auto& f : info->fields)
    {
        std::cout << "  Field: " << f.name
                  << ", offset: " << f.offset
                  << ", size: " << f.size << "\n";
    }
    return 0;
}
```

输出示例：

```
Class: Actor
  Field: Health, offset: 40, size: 4
  Field: Name, offset: 48, size: 32
```

> [!example] 引擎里的真实形态
> - Unreal Engine：`UCLASS()` / `UPROPERTY()` / `GENERATED_BODY()`
> - 自研引擎：通常简化版只用一个 `REFLECT()` 和一个 `PROPERTY()` 宏，配合代码生成工具（如 Python 脚本或 Clang LibTooling）做更复杂的解析。

### 4. 变参宏在日志系统中的应用

```cpp
#include <cstdio>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_ERROR 2

#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_DEBUG(fmt, ...)                                         \
    do {                                                            \
        if (CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG)                   \
            std::printf("[DEBUG] %s:%d " fmt "\n",                 \
                        __FILE__, __LINE__, ##__VA_ARGS__);        \
    } while(0)

#define LOG_ERROR(fmt, ...)                                         \
    do {                                                            \
        std::printf("[ERROR] %s:%d " fmt "\n",                     \
                    __FILE__, __LINE__, ##__VA_ARGS__);            \
    } while(0)
```

使用：

```cpp
LOG_DEBUG("Actor spawned at (%f, %f, %f)", 1.0f, 2.0f, 3.0f);
// 展开为：
// printf("[DEBUG] main.cpp:15 Actor spawned at (%f, %f, %f)\n", 1.0f, 2.0f, 3.0f);
```

### 5. 条件编译与跨平台封装

```cpp
#ifdef _WIN32
    #define PLATFORM_WINDOWS 1
    #define DLLEXPORT __declspec(dllexport)
    #define DLLIMPORT __declspec(dllimport)
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define DLLEXPORT __attribute__((visibility("default")))
    #define DLLIMPORT
#elif defined(__APPLE__)
    #define PLATFORM_MAC 1
    #define DLLEXPORT __attribute__((visibility("default")))
    #define DLLIMPORT
#endif

// 统一导出宏
#ifdef MYLIB_EXPORTS
    #define MYLIB_API DLLEXPORT
#else
    #define MYLIB_API DLLIMPORT
#endif
```

---

## 常见陷阱与最佳实践

### 陷阱 1：宏参数被多次求值

```cpp
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int x = 5;
int m = MAX(x++, 10);  // x++ 被执行了两次！
```

**对策**：使用 GNU 扩展 `({ ... })` 或改用 `inline` 函数 / `std::max`。

### 陷阱 2：优先级问题

```cpp
#define DOUBLE(x) x + x
int a = DOUBLE(3) * 2;  // 3 + 3 * 2 = 9，不是 12
```

**对策**：宏体整体和每个参数都用括号包裹：`#define DOUBLE(x) ((x) + (x))`。

### 陷阱 3：分号问题与 `if/else`

```cpp
#define SET_ZERO(x) (x) = 0;

if (cond)
    SET_ZERO(a);  // 这里多一个分号
else
    SET_ZERO(b);  // else 悬空错误
```

**对策**：使用 `do { ... } while(0)`。

### 陷阱 4：命名冲突

宏是纯文本替换，可能意外覆盖函数名或局部变量。

**对策**：
- 宏名使用**全大写**或特定前缀（如 `MY_`）
- 内部辅助宏用下划线开头（如 `_CONCAT_IMPL`）
- 用 `#undef` 及时清理局部宏

### 陷阱 5：调试困难

编译器报错通常指向展开后的代码行，难以追踪到原始宏定义。

**对策**：
- 使用 `gcc -E` / `clang -E` 查看预处理结果
- 保持宏逻辑简单，复杂逻辑交给模板或代码生成器
- 为宏编写单元测试，覆盖各种展开场景

---

## 进阶：宏的有限递归与计数器

预处理器**不支持真正的递归**。但通过重复包含自身文件，可以实现**有限次数的重复展开**。Boost.Preprocessor 库就是基于这一原理构建的。

```cpp
// 重复展开示例（简化版）
#define REPEAT_0(m, d)
#define REPEAT_1(m, d) m(0, d)
#define REPEAT_2(m, d) REPEAT_1(m, d) m(1, d)
#define REPEAT_3(m, d) REPEAT_2(m, d) m(2, d)

#define REPEAT_IMPL(count, m, d) REPEAT_##count(m, d)
#define REPEAT(count, m, d) REPEAT_IMPL(count, m, d)
```

在引擎中，这种技术常用于：
- 生成固定数量的函数重载
- 展开变参模板前的参数包（C++11 之前）
- 生成 SIMD 向量的分量访问（`.x`, `.y`, `.z`, `.w`）

> [!tip] 现代替代方案
> C++11 以后，**变参模板（variadic templates）**和 **`constexpr`/`consteval`** 已经能完成大部分需要“重复代码生成”的任务。宏的使用场景正在逐步收窄，但在**标识符生成**和**跨编译器抽象**方面仍不可替代。

---

## 总结

| 能力 | 宏 | 模板/constexpr |
|------|-----|---------------|
| 生成新标识符 | ✅ | ❌（C++20 前） |
| 字符串化 | ✅ | ❌ |
| 条件编译 | ✅ | 部分支持（`if constexpr`） |
| 类型安全 | ❌ | ✅ |
| 调试友好 | ❌ | ✅ |
| 编译期计算 | 否（纯替换） | ✅ |

> [!quote] 核心信条
> - **简单任务**：用 `inline` 函数和 `constexpr` 变量替代宏
> - **代码生成**：用 X-Macro 和 `.def` 文件统一管理重复定义
> - **反射系统**：用宏包裹样板代码，保持业务代码整洁
> - **平台抽象**：用条件编译宏隔离平台差异
> - **日志断言**：用变参宏提供统一的调用接口和上下文信息

---

> 最后更新：2026-04-13
