---
title: C++20 Concepts 与约束式泛型
date: 2026-06-13
tags:
  - C++
  - C++20
  - Concepts
  - requires
  - 泛型编程
aliases:
  - C++20 Concepts
  - Concepts
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# C++20 Concepts 与约束式泛型

> [!info] 一句话概括
> **Concepts** 是 C++20 引入的一种「约束（constraint）」机制：它让你用声明式语法描述「一个类型必须满足什么条件」，然后在模板参数上直接贴上这些条件。编译器会在编译期检查约束，不满足时给出清晰、可读的报错，而不必再绕一圈 SFINAE。

---

## 问题 0：SFINAE 已经能做事了，为什么还需要 Concepts？

在 [[SFINAE：替换失败不是错误|SFINAE：替换失败不是错误]] 里我们看到，`std::enable_if` 能让模板只在某些类型上生效。它很强大，但有几个让人头疼的问题：

### 1. 报错难读

```cpp
// flags: -std=c++20 -Wall -O2
#include <type_traits>

template<typename T,
         typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
T add(T a, T b) {
    return a + b;
}

int main() {
    // add(3.14, 2.71);  // ❌ 错误信息里全是 enable_if 和 std::void_t 的内部细节
    return 0;
}
```

用户只想知道「这个函数不接受浮点数」，但编译器吐出的错误往往要先翻几十行模板实例化堆栈。

### 2. 条件分散、可读性差

当约束变复杂时，模板参数列表会被 `enable_if` 撑得又臭又长，函数签名本身的意图被淹没在一堆元编程噪声里。

### 3. 重载规则晦涩

多个 `enable_if` 重载如果条件有重叠，容易出现「二义性调用」或「没有可用候选」，调试起来很费神。

> [!abstract]
> **Concepts** 的出现不是为了取代模板，而是给模板加上显式的「前置条件」。你可以把它理解为模板的「类型契约」：调用方一看就知道这个泛型接口对类型有什么要求，编译器也能基于这些契约给出更直接的错误。

---

## 问题 1：Concept 到底是什么？

一个 **concept** 是一个编译期布尔谓词，但它专门用于约束模板参数。语法上像一个带返回类型的函数模板，返回类型固定为 `bool`，用 `concept` 关键字声明。

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>
#include <iostream>

// 定义一个 concept：类型 T 必须是整数类型
template<typename T>
concept Integral = std::is_integral_v<T>;

// 使用 concept 约束模板参数
template<Integral T>
T add(T a, T b) {
    return a + b;
}

int main() {
    std::cout << add(2, 3) << "\n";   // ✅
    // add(2.5, 3.5);                  // ❌ double 不满足 Integral
    return 0;
}
```

> [!note]
> `concept` 不能是递归的，也不能在运行期调用；它纯粹是编译期的类型谓词。`Integral = std::is_integral_v<T>` 这种写法把已有的 type trait 包装成 concept，是最常见的起步方式。

---

## 问题 2：`requires` 子句有哪几种写法？

C++20 提供了多种方式把 concept 应用到模板上，灵活度很高。

### 写法一：直接写在模板参数列表里

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template<Number T>
T multiply(T a, T b) {
    return a * b;
}
```

### 写法二：写在 `requires` 子句里

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
  requires std::integral<T>
T mod(T a, T b) {
    return a % b;
}
```

### 写法三：写在函数签名尾部（简洁约束）

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
T square(T x) requires std::integral<T> || std::floating_point<T> {
    return x * x;
}
```

### 写法四：组合多个 concept

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>
#include <iostream>

template<typename T>
concept Sized = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

template<typename T>
concept Indexable = requires(T t) {
    { t[0] } -> std::same_as<typename T::value_type&>;
};

template<typename T>
  requires Sized<T> && Indexable<T>
void dump(const T& container) {
    for (std::size_t i = 0; i < container.size(); ++i) {
        std::cout << container[i] << "\n";
    }
}
```

> [!tip]
> `requires` 表达式本身也是一种 concept 的声明方式。`requires(T t) { ... }` 会在花括号里逐项检查表达式是否合法，并可以指定表达式的返回类型约束。它是把「这个类型能做什么」描述得最直观的语法。

---

## 问题 3：`requires` 表达式的内部长什么样？

一个 `requires` 表达式由「参数列表」和「需求序列」组成。需求序列可以包含三种需求：

### 简单需求

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
concept HasFoo = requires(T t) {
    t.foo();  // 只要求 t.foo() 能编译，不关心返回值
};
```

### 复合需求

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
concept FooReturnsInt = requires(T t) {
    { t.foo() } -> std::same_as<int>;  // foo() 必须返回 int
};
```

### 类型需求

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>

template<typename T>
concept HasValueType = requires {
    typename T::value_type;  // 要求 T 有嵌套类型 value_type
};
```

把它们组合起来，就能描述相当复杂的接口契约：

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>
#include <iterator>

template<typename T>
concept SortableContainer = requires(T t) {
    typename T::value_type;
    { t.begin() } -> std::same_as<typename T::iterator>;
    { t.end() }   -> std::same_as<typename T::iterator>;
    requires std::random_access_iterator<typename T::iterator>;
    requires std::totally_ordered<typename T::value_type>;
};
```

> [!note]
> `requires` 表达式内部还能再写 `requires ...;`，这叫**嵌套需求**。它用于表达「在满足前面条件的基础上，还要满足这个额外约束」。

---

## 问题 4：Concepts 和 `enable_if` 的对比

| 维度 | `std::enable_if` | C++20 Concepts |
|------|------------------|----------------|
| 可读性 | 条件藏在模板参数或返回类型里 | 条件显式写在 `template<Concept T>` 或 `requires` 子句里 |
| 错误信息 | 常出现深长的模板实例化堆栈 | 编译器直接提示「约束未满足」 |
| 重载决议 | 通过 SFINAE 排除候选 | 约束参与重载决议，语义更直接 |
| 组合能力 | 用 `&&`、`\|\|` 组合 trait | 用 concept 逻辑运算、`requires` 表达式组合 |
| 学习曲线 | 需要理解 SFINAE 和替换规则 | 语法更直观，但仍需理解类型系统 |

> [!warning]
> 表格中的 `\|` 在 Markdown 表格里需要转义，写成 `\|\|`。这是 wikilink 和表格共存的规范要求。

### 同一个函数用两种写法对比

```cpp
// flags: -std=c++20 -Wall -O2
#include <type_traits>
#include <concepts>

// enable_if 版本
template<typename T,
         typename std::enable_if_t<std::is_integral_v<T>, int> = 0>
T old_half(T x) {
    return x / 2;
}

// concept 版本
template<std::integral T>
T new_half(T x) {
    return x / 2;
}
```

> [!note]
> C++20 标准库已经提供了很多常用 concept，比如 `std::integral`、`std::floating_point`、`std::copy_constructible`、`std::random_access_iterator` 等。优先使用标准 concept，可以减少自己定义的工作量。

---

## 问题 5：Concepts 怎样参与重载决议？

Concepts 不仅让错误信息更好，也让**基于约束的重载**更清晰。

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>
#include <iostream>
#include <vector>

// 通用版本：可拷贝但非整数的类型
template<typename T>
  requires std::copy_constructible<T> && (!std::integral<T>)
void process(const T& x) {
    std::cout << "generic copy\n";
}

// 特化版本：整数类型
template<std::integral T>
void process(const T& x) {
    std::cout << "integral\n";
}

int main() {
    process(42);                 // integral
    process(std::vector<int>()); // generic copy
    return 0;
}
```

> [!note]
> 如果两个重载的约束有重叠且没有谁明确包含谁，编译器会报二义性。上面的例子把通用版本的条件写成 `copy_constructible && !integral`，确保两个重载互斥。这比「指望编译器自动选择更具体的 concept」更稳健。

---

## 问题 6：手写一个泛型 `max` 需要哪些 concept？

这是 70 天计划中 Day 19 的练习目标。先用 concept 写出来：

```cpp
// flags: -std=c++20 -Wall -O2
#include <concepts>
#include <iostream>

template<typename T>
  requires std::totally_ordered<T>
constexpr const T& my_max(const T& a, const T& b) {
    return a < b ? b : a;
}

int main() {
    std::cout << my_max(3, 5) << "\n";     // ✅
    // my_max(nullptr, nullptr);             // ❌ nullptr_t 不可比较
    return 0;
}
```

> [!note]
> `std::totally_ordered<T>` 是 C++20 标准 concept，要求类型 `T` 支持 `==`、`!=`、`<`、`>`、`<=`、`>=`，并且这些比较语义一致。对于自定义类型，通常需要重载这些运算符才能满足 concept。

---

## 总结

- **Concepts** 是 C++20 对模板参数的显式约束机制，让泛型接口的需求一目了然。
- `requires` 子句有三种常见位置：模板参数列表、函数签名前、函数签名尾。
- `requires` 表达式能描述简单需求、复合需求、类型需求和嵌套需求。
- 与 `enable_if` 相比，Concepts 的错误信息更友好、重载决议更直观、代码可读性更高。
- 手写 `max` 这类泛型函数时，可以用 `std::totally_ordered` 等标准 concept 直接约束参数。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 自定义容器与算法接口 | 用 `Concept` 约束 `Array::sort` 要求迭代器随机访问、`Hash` 要求类型可哈希；接口契约从注释变成编译期检查 |
| **UE** | 模板容器和反射接口 | UE 5.x 开始逐步引入 concept 化表达，用于约束 `TFunction` 的可调用对象、`TArray` 的元素类型要求 |
| **Bevy/现代 ECS** | 系统和组件查询 | Rust 的 trait bounds 已经证明约束式泛型的价值；C++ ECS 可以用 concept 表达「组件必须可复制」「系统查询必须可并行」等规则 |

> [!note] 关键取舍
> 如果项目只需要 C++17 或更早标准，仍然可以用 SFINAE 和 `enable_if` 实现相同功能。但即使无法立即使用 C++20，学习 Concepts 也有价值：它迫使你把「类型必须满足什么条件」想清楚，这种思维模式写出的 SFINAE 代码也会更干净。

---

> 相关笔记：
> - [[SFINAE：替换失败不是错误|SFINAE：替换失败不是错误]] — Concepts 出现之前的编译期约束手段
> - [[Notes/C++编程/模板机制与泛型编程/类模板基础与模板参数推导|类模板基础与模板参数推导]] — 理解模板参数推导是写好 concept 的前提
> - [[Notes/C++编程/类型系统与类型推导/type_traits 原理与应用|type_traits 原理与应用]] — concept 底层仍依赖 type trait
> - [[Notes/C++编程/标准库原理与引擎替代方案/迭代器分类与算法复杂度保证|迭代器分类与算法复杂度保证]] — 标准库迭代器 concept 的工业背景
