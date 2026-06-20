---
title: Lambda 表达式与闭包
date: 2026-06-13
tags:
  - C++
  - Lambda
  - 闭包
  - 可调用对象
  - C++14
aliases:
  - Lambda
  - 闭包
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# Lambda 表达式与闭包

> [!info] 一句话概括
> **Lambda 表达式** 是 C++11 引入的匿名函数写法，它能在定义处「捕获」周围变量，形成一个可调用对象。这个对象连同捕获的变量一起，被称为**闭包（closure）**。

---

## 问题 0：没有 Lambda 的时候，我们怎么写回调？

普通函数指针没有状态，函数对象能携带状态却需要定义类。Lambda 让你 inline 地定义一个带状态的函数对象。

> [!abstract]
> **Lambda 表达式** 是一种语法糖，编译器会把它翻译成一个**匿名类**，这个类重载了 `operator()`。Lambda 捕获的变量会变成这个匿名类的成员变量。

---

## 问题 1：Lambda 的基本语法是什么？

Lambda 由四部分组成：

```cpp
[捕获列表](参数列表) -> 返回类型 { 函数体 }
```

例如：

```cpp
// flags: -O0 -g
#include <iostream>

int main() {
    int player_hp = 100;

    auto on_frame_end = [player_hp]() {
        std::cout << "frame end, player hp = " << player_hp << "\n";
    };

    on_frame_end();  // 输出 frame end, player hp = 100
    return 0;
}
```

`[player_hp]` 是**捕获列表**，把当前作用域里的 `player_hp` 复制一份放到 Lambda 内部。`auto` 是因为 Lambda 的类型由编译器自动生成，没有名字。

---

## 问题 2：捕获模式有哪些？

### 值捕获

```cpp
// flags: -O0 -g
#include <iostream>

int main() {
    int hp = 100;
    auto cb = [hp]() {
        std::cout << hp << "\n";
    };
    hp = 50;
    cb();  // 输出 100，Lambda 内部是 hp 的拷贝
    return 0;
}
```

### 引用捕获

```cpp
// flags: -O0 -g
#include <iostream>

int main() {
    int hp = 100;
    auto cb = [&hp]() {
        std::cout << hp << "\n";
    };
    hp = 50;
    cb();  // 输出 50，Lambda 内部保存的是引用
    return 0;
}
```

| 写法 | 含义 |
| :-- | :-- |
| `[]` | 不捕获 |
| `[x]` | `x` 按值捕获 |
| `[&x]` | `x` 按引用捕获 |
| `[=]` | 所有用到的变量按值捕获 |
| `[&]` | 所有用到的变量按引用捕获 |
| `[=, &x]` | 默认按值，但 `x` 按引用 |
| `[&, x]` | 默认按引用，但 `x` 按值 |

> [!warning]
> `[=]` 和 `[&]` 让读者不清楚 Lambda 依赖哪些外部状态。复杂 Lambda 应显式列出捕获变量。

---

## 问题 3：什么是初始化捕获？

C++14 引入**初始化捕获**，允许用任意表达式初始化闭包成员：

```cpp
// flags: -O0 -g
#include <iostream>
#include <memory>

int main() {
    auto ptr = std::make_unique<int>(42);

    auto cb = [p = std::move(ptr)]() {
        std::cout << *p << "\n";
    };

    cb();  // 输出 42
    return 0;
}
```

`[p = std::move(ptr)]` 在 Lambda 内部创建成员 `p`，并用 `std::move(ptr)` 初始化。这弥补了普通值捕获只能复制、无法移动的缺陷，是把 `unique_ptr` 等只移类型捕获进 Lambda 的唯一方式。

---

## 问题 4：闭包类型是什么？

每个 Lambda 表达式都有一个**唯一的闭包类型**，由编译器自动生成。即使两个 Lambda 的签名完全相同，它们的类型也不同：

```cpp
// flags: -O0 -g
#include <type_traits>

int main() {
    auto a = [](int x) { return x + 1; };
    auto b = [](int x) { return x + 1; };
    static_assert(!std::is_same_v<decltype(a), decltype(b)>);
    return 0;
}
```

> [!abstract]
> **闭包类型（Closure Type）** 是编译器为每个 Lambda 生成的匿名类。它重载了 `operator()`，并保存捕获的变量。每个 Lambda 的闭包类型都是独一无二的。

因为闭包类型各不相同，把多个不同可调用对象统一存起来需要**类型擦除**。最常用的是 `std::function`（参见 [[std-function 的实现与开销|std::function 的实现与开销]]）。

---

## 问题 5：什么是泛型 Lambda？

C++14 允许 Lambda 参数使用 `auto`：

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>

int main() {
    auto add = [](auto a, auto b) { return a + b; };

    std::cout << add(1, 2) << "\n";
    std::cout << add(1.5, 2.5) << "\n";
    std::cout << add(std::string("a"), "b") << "\n";
    return 0;
}
```

编译器会把它翻译成带有模板 `operator()` 的闭包类。泛型 Lambda 在写通用比较器时特别方便：

```cpp
std::sort(v.begin(), v.end(), [](auto a, auto b) { return a > b; });
```

> [!tip]
> 泛型 Lambda 的 `operator()` 默认是 `const`。如需修改捕获的变量，按引用捕获或加 `mutable`。

---

## 问题 6：捕获 `this` 有什么陷阱？

在类成员函数中使用 Lambda 时，`[=]` 和 `[&]` 会隐式捕获 `this` 指针：

```cpp
// flags: -O0 -g
#include <iostream>

class Player {
    int hp_ = 100;
public:
    auto make_callback() {
        return [=]() {
            // 实际捕获的是 this 指针，不是 hp_ 的拷贝
            std::cout << "hp = " << hp_ << "\n";
        };
    }
};

int main() {
    Player p;
    auto cb = p.make_callback();
    cb();  // ✅ Player 对象还在
    return 0;
}
```

如果 Lambda 在对象销毁后执行，通过 `this` 访问 `hp_` 就是悬垂指针访问。

C++17 引入 `*this` 捕获，按值复制整个对象：

```cpp
// flags: -O0 -g
#include <iostream>

class Player {
    int hp_ = 100;
public:
    auto make_callback() {
        return [*this]() {
            std::cout << "hp = " << hp_ << "\n";
        };
    }
};

int main() {
    Player p;
    auto cb = p.make_callback();
    cb();  // 输出 hp = 100，闭包内是对象拷贝
    return 0;
}
```

> [!abstract]
> C++17 的 **`*this` 捕获** 把当前对象按值复制进 Lambda。与隐式 `this` 指针捕获不同，它不会在对象销毁后产生悬垂引用，但要求对象可拷贝。

> [!warning]
> 引擎中 Lambda 常被注册为帧结束回调、异步加载回调或延迟事件。如果回调执行时对象已销毁，捕获 `this` 会导致崩溃。最安全的做法是只捕获**真正需要的数据副本**。

---

## 总结

- **Lambda 表达式** 是紧凑的匿名函数对象，编译器会生成闭包类。
- **值捕获**复制外部变量，**引用捕获**保存引用；引用捕获要注意生命周期。
- **初始化捕获（C++14）** 支持移动语义，是把只移类型放进 Lambda 的唯一方式。
- 每个 Lambda 的**闭包类型**唯一，多个签名相同的 Lambda 类型也不同。
- **泛型 Lambda（C++14）** 让 `operator()` 变成模板函数，适合通用算法。
- 类成员函数中的 `[=]`/`[&]` 隐式捕获 `this` 指针；C++17 的 `[*this]` 按值复制对象。
- 引擎回调 Lambda 必须谨慎处理生命周期，避免对象销毁后执行悬垂引用。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
| :-- | :-- | :-- |
| **SelfGameEngine** | 委托系统、事件系统、任务调度 | Lambda 绑定回调；初始化捕获移动所有权对象；捕获 `this` 时使用 `*this` 或数据副本 |
| **UE** | `DECLARE_DELEGATE`、异步加载回调 | `TFunction` 是 `std::function` 的替代，支持移动语义和小对象优化 |


> [!note] 关键取舍
> Lambda 让回调代码变得简洁，但「简洁」不等于「安全」。引擎中最常见的 Lambda bug 是**生命周期错误**：捕获了栈上引用、`this` 指针但对象提前销毁、或错误复制只移类型。写 Lambda 时先问：这个闭包会比它捕获的变量活得更长吗？如果会，就用值捕获或初始化捕获。

---

> 相关笔记：
> - [[Notes/C++编程/值类别与引用语义/值类别与移动语义|值类别与移动语义]] — 理解初始化捕获中 `std::move` 的本质
> - [[std-function 的实现与开销|std::function 的实现与开销]] — 理解如何把不同类型的 Lambda 统一存储（类型擦除）
> - [[Notes/C++编程/对象内存模型与底层机制/成员函数指针的底层表示|成员函数指针的底层表示]] — 委托系统中绑定成员函数回调的基础
> - [[Notes/C++编程/资源管理与对象生存期/对象生存期与 RAII|对象生存期与 RAII]] — 理解 Lambda 捕获变量的生命周期边界
