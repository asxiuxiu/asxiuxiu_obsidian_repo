---
title: type_traits 原理与应用
date: 2026-06-13
tags:
  - C++
  - type_traits
  - 模板元编程
  - SFINAE
  - Concepts
aliases:
  - 类型萃取
  - type traits
  - 编译期类型查询
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# type_traits 原理与应用

> [!info] 一句话概括
> **type_traits（类型萃取）**是一套编译期工具，它让模板代码能够在编译阶段「提问」类型的特性（如是不是指针、能不能拷贝），并对类型做「变换」（如去掉 const、得到指针类型），是实现编译期多态和反射代码生成的基础。

---

## 问题 0：编译期怎么知道一个类型的特性？

想象你要写一个泛型容器 `Array<T>`，在扩容迁移元素时，如果 `T` 是 `int` 这种简单类型，可以直接用 `memcpy` 搬内存；但如果 `T` 是带有非平凡析构函数的类，就必须逐个构造/析构。这个判断不能等到运行时再做，因为代码路径必须在编译期就确定。

没有类型萃取时，你会被迫写很多特化版本：

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

void copy_ints(int* dst, int* src, size_t n) {
    std::memcpy(dst, src, n * sizeof(int));
}

// 如果类型一变，就要再写一个函数……
```

> [!abstract]
> **type_traits** 把「类型信息」本身变成编译期可查询的数据。它通常以模板结构体的形式出现，通过**模板特化**让不同类型得到不同的 `value` 或 `type`，从而驱动后续的分支选择。

---

## 问题 1：查询型 type_traits 是怎么实现的？

查询型 trait 回答「这个类型是否具有某个性质」。最常用的是 `std::is_pointer`、`std::is_reference`、`std::is_integral` 等。它们的核心技巧是**偏特化**。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <type_traits>

// 主模板：默认不是指针
template<typename T>
struct IsPointer {
    static constexpr bool value = false;
};

// 偏特化：所有 T* 都是指针
template<typename T>
struct IsPointer<T*> {
    static constexpr bool value = true;
};

// 主模板：默认不是引用
template<typename T>
struct IsReference {
    static constexpr bool value = false;
};

// 偏特化：左值引用
template<typename T>
struct IsReference<T&> {
    static constexpr bool value = true;
};

// 偏特化：右值引用
template<typename T>
struct IsReference<T&&> {
    static constexpr bool value = true;
};

int main() {
    static_assert(IsPointer<int*>::value);
    static_assert(!IsPointer<int>::value);
    static_assert(IsReference<int&>::value);
    static_assert(IsReference<int&&>::value);
    static_assert(!IsReference<int>::value);

    std::cout << "IsPointer<int*> = " << IsPointer<int*>::value << "\n";
    std::cout << "IsReference<int&> = " << IsReference<int&>::value << "\n";
    return 0;
}
```

编译器在匹配模板时，会优先选择更具体的偏特化版本。于是 `int*` 匹配 `IsPointer<T*>`，`int&` 匹配 `IsReference<T&>`，其他类型 fallback 到主模板得到 `false`。

---

## 问题 2：变换型 type_traits 是怎么实现的？

变换型 trait 不回答「是/否」，而是把一种类型变成另一种类型。例如 `std::remove_const<T>` 把 `const int` 变成 `int`，`std::add_pointer<T>` 把 `int` 变成 `int*`。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <type_traits>

// 去掉 const
template<typename T> struct RemoveConst          { using type = T; };
template<typename T> struct RemoveConst<const T> { using type = T; };

// 加上指针
template<typename T> struct AddPointer { using type = T*; };

// 条件选择类型
template<bool B, typename T, typename F>
struct Conditional { using type = T; };

template<typename T, typename F>
struct Conditional<false, T, F> { using type = F; };

int main() {
    static_assert(std::is_same_v<RemoveConst<const int>::type, int>);
    static_assert(std::is_same_v<AddPointer<int>::type, int*>);
    static_assert(std::is_same_v<Conditional<true, int, double>::type, int>);
    static_assert(std::is_same_v<Conditional<false, int, double>::type, double>);

    std::cout << "ok\n";
    return 0;
}
```

> [!tip]
> C++11 引入的 **alias template（别名模板）** 让我们可以少写一层 `typename ... ::type`。标准库中的 `std::remove_const_t<T>`、`std::enable_if_t<B, T>` 都是这种写法的产物。

---

## 问题 3：怎么用 `void_t` 和 `declval` 探测类型的能力？

有时候我们想问的问题不是「你是不是指针」，而是「你是否有某个成员函数」。这种探测不能靠偏特化直接匹配，因为成员函数的名字和签名千变万化。`std::void_t` 和 `std::declval` 就是解决这个问题的工具。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <type_traits>

// 主模板：默认没有 foo
template<typename, typename = std::void_t<>>
struct HasFoo : std::false_type {};

// 偏特化：如果 declval<T>().foo() 这个表达式合法，就匹配上
template<typename T>
struct HasFoo<T, std::void_t<decltype(std::declval<T>().foo())>>
    : std::true_type {};

struct A { void foo(); };
struct B { int foo() const; };
struct C {};

int main() {
    std::cout << HasFoo<A>::value << "\n"; // 1
    std::cout << HasFoo<B>::value << "\n"; // 1
    std::cout << HasFoo<C>::value << "\n"; // 0
    return 0;
}
```

> [!abstract]
> `std::declval<T>()` 让你无需真正构造对象就能得到 `T` 的右值引用，从而写出 `decltype(std::declval<T>().foo())` 这样的探测表达式。`std::void_t<...>` 则把任意类型包「折叠」成 `void`，让偏特化只在所有表达式都合法时才生效，否则触发 SFINAE 被排除。

---

## 问题 4：type_traits 和 SFINAE 是什么关系？

type_traits 提供「类型是否满足条件」的布尔答案，SFINAE 则提供「根据答案启用或禁用某个重载」的机制。两者经常一起出现。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <type_traits>

// 只对整数类型启用
template<typename T>
std::enable_if_t<std::is_integral_v<T>, void>
print_number(T x) {
    std::cout << "integer: " << x << "\n";
}

// 只对浮点类型启用
template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, void>
print_number(T x) {
    std::cout << "float: " << x << "\n";
}

int main() {
    print_number(42);    // integer
    print_number(3.14);  // float
    // print_number("hi"); // ❌ 无匹配重载
    return 0;
}
```

当 `std::is_integral_v<T>` 为 `false` 时，`std::enable_if_t<false, void>` 不存在，模板替换失败；SFINAE 让这次失败安静地排除该重载，而不是报编译错误。

---

## 问题 5：type_traits 和 Concepts 是什么关系？

C++20 引入的 **Concepts** 可以看作 SFINAE + type_traits 的「声明式」替代。它把约束直接写在模板签名里，诊断信息更清晰。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <type_traits>
#include <concepts>

template<typename T>
concept Integral = std::is_integral_v<T>;

template<typename T>
concept FloatingPoint = std::is_floating_point_v<T>;

template<Integral T>
void describe(T x) {
    std::cout << "integral: " << x << "\n";
}

template<FloatingPoint T>
void describe(T x) {
    std::cout << "floating: " << x << "\n";
}

int main() {
    describe(42);    // integral
    describe(3.14f); // floating
    return 0;
}
```

> [!note]
> Concepts 的底层仍然依赖 type_traits 提供谓词，但它把「类型约束」从函数签名的角落里提到了参数列表中，代码可读性和错误信息都更好。在 C++20 项目中，能用 Concepts 的地方通常优先于手写 SFINAE。

---

## 问题 6：引擎里怎么用好 type_traits？

最常见的场景是**根据类型特性选择算法实现**。比如迁移数组元素时，若类型是平凡可拷贝的，就直接 `memcpy`；否则逐个移动构造并析构旧对象。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>
#include <cstring>
#include <type_traits>
#include <utility>

struct Pod { int x; };
struct NonPod {
    int x;
    NonPod() : x(0) {}
    NonPod(NonPod&& o) noexcept : x(std::exchange(o.x, -1)) {
        std::cout << "move\n";
    }
    ~NonPod() {}
};

template<typename T>
void relocate(T* dst, T* src, size_t n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dst, src, n * sizeof(T));
        std::cout << "memcpy path\n";
    } else {
        for (size_t i = 0; i < n; ++i) {
            new (dst + i) T(std::move(src[i]));
            src[i].~T();
        }
        std::cout << "move path\n";
    }
}

int main() {
    Pod pods[4] = { {1}, {2}, {3}, {4} };
    Pod dst1[4];
    relocate(dst1, pods, 4);

    NonPod nps[2];
    NonPod dst2[2];
    relocate(dst2, nps, 2);
    return 0;
}
```

这种「编译期分支」不引入运行时开销，是自定义容器、反射代码生成、序列化系统的核心技巧。

---

## 总结

- type_traits 的用途分两类：**查询**（如 `is_pointer`）和**变换**（如 `remove_const`）。
- 查询型 trait 主要通过**模板偏特化**实现；变换型 trait 主要通过**类型别名**实现。
- `std::void_t` 和 `std::declval` 组合可以探测任意合法表达式，是实现「类型是否有某成员/某能力」的关键技术。
- type_traits 常与 SFINAE 联用做编译期重载选择；在 C++20 中，可以用 Concepts 写出更清晰的约束。
- 在引擎中，type_traits 驱动了 `memcpy` 快速迁移、反射代码生成、组件类型注册等高频场景。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 泛型容器优化 | `Array<T>` 在扩容/缩容时通过 `std::is_trivially_copyable_v<T>` 选择 `memcpy` 或逐个构造/析构；用 `EnableIf` 约束 `emplace` 参数 |
| **UE** | 模板容器与反射代码生成 | `TIsIntegral`、`TIsPointer`、`TIsTriviallyDestructible` 等引擎 trait 支撑 `TArray`、`TMap` 的序列化和内存布局；UHT 生成的反射表依赖类型萃取 |
| **Bevy/现代 ECS** | 组件类型注册 | ECS 在编译期通过 trait 判断组件是否 `Send/Sync`、是否实现某接口，type_traits 是实现零开销抽象的基础 |

> [!note] 关键取舍
> type_traits 让模板代码能在编译期「看见」类型信息，但滥用会让错误信息爆炸。现代 C++ 建议：简单约束优先用 Concepts，复杂探测再用 `void_t` + SFINAE，并且把 trait 封装在命名良好的别名模板后面，不要散落在每个函数签名里。

---

> 相关笔记：
> - [[Notes/C++编程/类型系统与类型推导/SFINAE：替换失败不是错误|SFINAE]] — 用 type_traits 做编译期重载选择
> - [[Notes/C++编程/类型系统与类型推导/C++20 Concepts 与约束式泛型|C++20 Concepts]] — type_traits 的现代声明式替代
> - [[Notes/C++编程/编译期计算与代码生成/constexpr 关键字|constexpr]] — 编译期计算与 type_traits 的互补关系
> - [[Notes/C++编程/模板机制与泛型编程/类模板基础与模板参数推导|类模板基础与模板参数推导]] — 模板特化与推导是 type_traits 的实现基础
