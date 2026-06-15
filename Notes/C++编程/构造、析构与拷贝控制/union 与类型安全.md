---
title: union 与类型安全
date: 2026-06-13
tags:
  - C++
  - union
  - 类型安全
  - Variant
  - 类型双关
  - 对象生存期
aliases:
  - Union
  - Tagged Union
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# union 与类型安全

> [!info] 一句话概括
> **union** 让多个成员共享同一块内存，能节省空间，却也把「当前存的是什么类型」这个关键信息交给了程序员手动维护——一旦维护出错，就是未定义行为。理解它的边界，是手写 `Variant` 这类类型安全容器的第一步。

---

## 问题 0：为什么需要 union？

假设你在写一个事件系统，每个事件可能是整数、浮点数或字符串。如果用一个结构体同时容纳三种可能：

```cpp
struct Event {
    int i;
    double d;
    char s[32];
    // 总大小 ≈ 4 + 8 + 32 = 44 字节以上
};
```

任何时刻只有一个字段有效，其他字段都在浪费内存。如果事件数量很大，这种浪费就很可观。

union 的解决方案是：**所有成员共享同一块内存，大小取最大成员**。

```cpp
// flags: -O0 -g
#include <iostream>

union EventUnion {
    int i;
    double d;
    char s[32];
};

int main() {
    std::cout << "sizeof(EventUnion) = " << sizeof(EventUnion) << "\n";
    std::cout << "sizeof(double)     = " << sizeof(double) << "\n";
    return 0;
}
```

在典型 64 位平台上，`sizeof(EventUnion)` 约等于 32（`char s[32]` 是最大的成员），比同时存放三个字段小得多。

> [!abstract]
> **union** 是一种特殊的类类型，所有成员从同一地址开始存放。同一时刻只有一个成员处于「活跃」状态，读写非活跃成员是未定义行为（C++ 严格别名规则）。

---

## 问题 1：union 怎么用？陷阱在哪？

### 基本用法

```cpp
union U {
    int i;
    double d;
};

U u;
u.i = 42;        // 现在活跃成员是 i
std::cout << u.i << "\n";  // ✅ 安全
// std::cout << u.d << "\n";  // ❌ 未定义行为：d 不是活跃成员
```

### 没有默认的「当前类型」信息

union 本身不记录当前哪个成员是活跃的。你必须自己维护：

```cpp
struct TaggedUnion {
    enum class Type { Int, Double } type;
    union {
        int i;
        double d;
    } data;
};
```

每次访问前都要检查 `type`，否则很容易出错。

### 构造函数与析构函数的限制

传统 C++ 的 union 成员不能是带有非平凡构造/析构的类型。例如：

```cpp
union Bad {
    int i;
    std::string s;  // ❌ C++03：union 不能含非 POD 类型
};
```

C++11 放宽了限制，允许 union 含有非平凡构造/析构的成员，但你需要**显式调用构造函数和析构函数**来管理对象生存期：

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>
#include <new>

union U {
    int i;
    std::string s;

    U() {}   // 需要自定义构造/析构
    ~U() {}
};

int main() {
    U u;
    new (&u.s) std::string("hello");  // placement new 构造 string
    std::cout << u.s << "\n";
    u.s.~basic_string();              // 显式析构
    return 0;
}
```

> [!warning]
> 如果你在一个 union 成员上 placement new 了一个 `std::string`，就必须在切换成员或销毁 union 前显式调用它的析构函数。否则就是泄漏或未定义行为。

---

## 问题 2：怎么让 union 变安全？

手动维护 type tag 和 placement new 很容易出错。C++17 提供了 `std::variant`，把「类型标签 + union + 析构管理」封装起来：

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>
#include <variant>

int main() {
    std::variant<int, double, std::string> v;
    v = "hello";  // 当前活跃成员是 string

    if (std::holds_alternative<std::string>(v)) {
        std::cout << std::get<std::string>(v) << "\n";
    }

    v = 42;  // 自动析构旧的 string，构造新的 int
    std::cout << std::get<int>(v) << "\n";
    return 0;
}
```

`std::variant` 自动处理：
- 类型标签的维护
- 活跃成员的构造/析构
- 访问错误类型时抛 `std::bad_variant_access`

Day 13 的练习就是让你手写一个简化版 `Variant`，理解其底层机制。

---

## 问题 3：写简化版 `Variant` 需要哪些概念？

1. **union 存储**：用 `union` 或 `alignas` + `char buffer[sizeof(T)]` 存放多种类型。
2. **类型索引**：用一个整数标记当前活跃成员是第几个类型。
3. **placement new**：在原始内存上构造对应类型的对象。
4. **显式析构**：切换类型或销毁 `Variant` 时，调用当前活跃成员的析构函数。
5. **析构安全**：如果 `Variant` 当前没有活跃成员（例如默认构造时），不能随意析构。

```cpp
// 简化版骨架
template<typename T1, typename T2, typename T3>
class Variant {
    union Storage {
        T1 t1;
        T2 t2;
        T3 t3;
        // 需要自定义构造/析构，因为成员可能有非平凡构造
        Storage() {}
        ~Storage() {}
    };

    Storage data_;
    unsigned char index_ = 0;  // 0 表示无活跃成员
};
```

> [!tip]
> 练习不要求你做出和 `std::variant` 一样完备的接口。重点是理解：**union 节省内存，但类型安全必须靠额外的类型标签和手动生存期管理来保障**。

---

## 总结

- **union** 让多个成员共享内存，大小取最大成员，适合表示「多选一」的数据。
- union **不记录当前活跃成员**，读写非活跃成员是未定义行为。
- 含非平凡类型的 union 需要手动用 placement new 构造、显式调用析构函数。
- `std::variant` 是类型安全的 union，自动管理类型标签和对象生存期。
- 手写 `Variant` 的关键是：union 存储 + 类型索引 + placement new + 显式析构。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 事件系统、消息队列、配置值 | 用简化 `Variant` 表示多种事件负载，避免虚函数和 RTTI 开销，同时保持类型安全 |
| **UE** | `TVariant`、`TUnion` | UE 有类似的类型安全联合类型，用于蓝图变量、属性系统；理解 union 底层有助于阅读这些实现 |

> [!note] 关键取舍
> union 是 C++ 里最高效的多态表达之一：没有 vptr、没有堆分配、内存紧凑。但这份效率的代价是类型安全完全由程序员负责。引擎中只有在性能敏感且类型集合固定的地方才会使用自定义 Variant，否则更安全的 `std::variant` 或接口抽象是更好的选择。

---

> 相关笔记：
> - [[Notes/C++编程/资源管理与对象生存期/对象生存期与 RAII|对象生存期与 RAII]] — union 成员的生存期管理本质
> - [[Notes/C++编程/资源管理与对象生存期/原始内存操作与对象生命周期的边界|原始内存操作与对象生命周期的边界]] — placement new 与显式析构的边界
> - [[Notes/C++编程/对象内存模型与底层机制/对象内存布局：从 struct 到 class|对象内存布局]] — union 的内存排布规则
