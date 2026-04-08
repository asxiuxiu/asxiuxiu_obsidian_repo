---
title: C++ constexpr 关键字
date: 2026-04-03
tags:
  - cpp
  - constexpr
  - compile-time
aliases:
  - constexpr
---

> [[索引|← 返回 C++编程索引]]

# C++ constexpr 关键字

## Why：为什么要学习 constexpr？

### 问题背景

C++ 中有一些场景**强制要求**编译期已知的常量：

| 场景 | 示例 |
|------|------|
| 数组大小 | `int arr[N];` |
| 模板非类型参数 | `std::array<int, N>` |
| `case` 标签 | `case N:` |
| 枚举值底层 | `enum Foo { A = N };` |
| `static_assert` | `static_assert(N > 0);` |
| 位域宽度 | `int x : N;` |

在 C++11 之前，这些场景通常只能用**宏**或**模板元编程**来解决，代码晦涩难懂。`constexpr` 的出现提供了一种更自然的方式，让普通函数和变量也能参与编译期计算。

### 不用 constexpr 的后果

- 运行时计算带来不必要的开销
- 无法将计算结果用于需要编译期常量的上下文
- 模板元编程代码可读性极差

### 应用场景

- 编译期数学计算（如阶乘、斐波那契数）
- 编译期查找表生成
- 元编程中替代复杂的 SFINAE/模板技巧
- 嵌入式系统中对 ROM 的优化

---

## What：constexpr 是什么？

### 一句话定义

> `constexpr` 是 C++ 的关键字，用于声明**可以在编译期求值**的变量或函数。

### 核心概念：什么叫"能够在编译期使用"？

这是理解 `constexpr` 的关键。要理解它，先区分两个概念：

#### 编译期（Compile-time）vs 运行期（Run-time）

```
源代码 → [编译器] → 可执行文件 → [运行时] → 程序执行结果
         ↑ 编译期在这里发生      ↑ 运行期在这里发生
```

- **编译期**：编译器读取源代码并生成机器码的阶段。此时程序还没有运行。
- **运行期**：用户双击可执行文件，程序真正执行的阶段。

#### "能够在编译期使用"的含义

当一个变量或函数被标记为 `constexpr` 时，意味着：**只要给定的输入是编译期已知的，编译器就有能力在编译阶段就把它的结果算出来**。

这个结果在编译后就已经是一个"写死的"常量值，程序运行时直接读取，**不需要再执行任何计算**。

```cpp
constexpr int square(int x) {
    return x * x;
}

constexpr int a = square(5);  // 编译器在编译时就算出 a = 25
int arr[a];                    // ✅ OK：a 是编译期常量
```

上例中，`square(5)` 的求值发生在编译期，`a` 的值在生成的可执行文件中直接就是 `25`，运行时没有任何乘法运算。

#### 编译期常量的"通行证"作用

`constexpr` 最重要的价值不是"算得快"，而是它提供了一张**编译期常量的通行证**——只有持有这张通行证，你才能进入那些"只允许编译期常量"的场景。

```cpp
const int b = 5;
int arr[b];  // C++98 可能报错，C++11 起通常 OK，但 b 不一定是编译期常量

const int c = get_value();  // 运行期确定
template <int N>
void foo() {}
foo<c>();  // ❌ 错误：c 不是编译期常量

constexpr int d = 5;
foo<d>();  // ✅ OK：d 是编译期常量
```

> **注意**：`const` 只保证变量不可修改，但不保证值在编译期已知；`constexpr` 则**强制要求**编译期可知。

---

## How：如何使用 constexpr？

### 1. constexpr 变量

```cpp
constexpr int max_size = 100;
constexpr double pi = 3.14159;

int buffer[max_size];  // ✅ OK
```

规则：
- 初始化表达式必须是编译期可求值的
- 隐式具有 `const` 属性

### 2. constexpr 函数（C++11 → C++14 → C++17 → C++20 演进）

#### C++11：严格限制

```cpp
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);  // 只能有一条 return 语句
}
```

C++11 的 `constexpr` 函数体几乎只能有一个 `return` 语句。

#### C++14：大幅放宽

```cpp
constexpr int factorial(int n) {
    int result = 1;      // ✅ C++14 起允许变量声明
    for (int i = 1; i <= n; ++i) {
        result *= i;     // ✅ 允许循环
    }
    return result;
}
```

C++14 起，`constexpr` 函数可以包含局部变量、循环、if 语句等，几乎和普通函数一样写。

#### C++17：constexpr if

```cpp
template <typename T>
auto get_value(T t) {
    if constexpr (std::is_pointer_v<T>)      // 编译期条件分支
        return *t;
    else
        return t;
}
```

`if constexpr` 在编译期决定走哪个分支，未被选中的分支甚至不需要编译通过。

#### C++20：constexpr 虚函数、try-catch、new/delete

```cpp
constexpr int foo(int x) {
    if (x < 0) throw std::runtime_error("negative");  // ✅ C++20
    return x;
}
```

C++20 进一步扩展，`constexpr` 函数中允许使用 `try-catch`、`new`/`delete`、虚函数等。

### 3. constexpr 函数的隐式 const（C++11/14 vs C++17）

```cpp
// C++11/C++14：constexpr 成员函数隐式为 const 成员函数
struct Point {
    int x;
    constexpr int getX() const { return x; }  // 必须加 const
};

// C++17 起：constexpr 成员函数不再隐式为 const
struct Point {
    int x;
    constexpr void setX(int v) { x = v; }  // ✅ C++17：可以修改成员
};
```

### 4. 编译期 vs 运行期的"双重身份"

`constexpr` 函数有一个重要特性：**它既可以在编译期执行，也可以在运行期执行**。

```cpp
constexpr int add(int a, int b) {
    return a + b;
}

constexpr int x = add(1, 2);   // 编译期求值
int y = add(get_user_input(), 3);  // 运行期求值（输入不是编译期常量）
```

> 编译器会尽可能在编译期求值，如果输入不是编译期常量，就退化为普通函数在运行期调用。

### 5. 强制编译期求值：`std::is_constant_evaluated()`（C++20）

```cpp
#include <type_traits>

constexpr int safe_sqrt(int x) {
    if (std::is_constant_evaluated()) {
        // 只在编译期走的逻辑
        if (x < 0) return 0;
    }
    return std::sqrt(x);
}
```

### 6. 编译期字符串处理示例

```cpp
constexpr size_t strlen_constexpr(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return len;
}

constexpr auto len = strlen_constexpr("Hello");  // len = 5，编译期确定
```

---

## 常见陷阱

### 陷阱 1：constexpr 变量不能用运行期值初始化

```cpp
int n = 10;
constexpr int m = n;  // ❌ 错误：n 不是编译期常量
const int m2 = n;     // ✅ OK：const 只要求不可修改
```

### 陷阱 2：constexpr 函数内部不能调用非 constexpr 函数（C++11/14/17）

```cpp
int runtime_func(int x) { return x; }

constexpr int bad(int x) {
    return runtime_func(x);  // ❌ 错误
}
```

C++20 起部分场景有所放宽（如 `std::vector` 的 `constexpr` 支持）。

### 陷阱 3：指针的 constexpr

```cpp
constexpr int* p = nullptr;       // ✅ OK
int x = 10;
constexpr int* q = &x;            // ❌ 错误：&x 的地址不是编译期常量

static int y = 20;
constexpr int* r = &y;            // ✅ OK：static 对象的地址是编译期确定的
```

### 陷阱 4：误以为 constexpr 一定在编译期执行

```cpp
constexpr int f(int x) { return x * x; }
int a = f(runtime_value());  // 这里是运行期执行！
```

`constexpr` 表示"**可以**在编译期求值"，不是"**一定**在编译期求值"。

---

## 总结

| 特性 | `const` | `constexpr` |
|------|---------|-------------|
| 不可修改 | ✅ | ✅ |
| 编译期已知 | ❌ 不保证 | ✅ 强制保证 |
| 用于模板参数/数组大小 | ❌ 不一定 | ✅ |
| 适用对象 | 变量 | 变量 + 函数 |

`constexpr` 的本质是向编译器声明：**这个实体是"编译期安全"的**，它的结果可以在编译阶段就被确定下来。这使得我们能够用更自然的方式编写高性能、零开销的 C++ 代码，同时摆脱宏和复杂模板元编程的束缚。