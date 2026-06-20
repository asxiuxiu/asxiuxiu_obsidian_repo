---
title: const 正确性与返回值语义
date: 2026-06-20
tags:
  - C++
  - const
  - 返回值语义
  - 接口设计
aliases:
  - const 正确性
  - const 返回值
---

> [[Notes/C++编程/索引|← 返回 C++编程索引]]

# const 正确性与返回值语义

> [!info] 一句话概括
> `const` 不是优化提示，而是接口契约；返回值加不加 `const`、传 `T&` 还是 `const T&`，直接决定了调用者能对你的对象做什么。

---

## 问题 0：如果没有 const，接口契约会变成什么样？

假设我们正在写一个二维向量类：

```cpp
// flags: -O0 -g
#include <iostream>

struct Vec2 {
    float x, y;

    Vec2 operator+(const Vec2& rhs) const {
        return {x + rhs.x, y + rhs.y};
    }
};

int main() {
    const Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};

    Vec2 c = a + b;   // 可以：a 是 const，但 operator+ 是 const 成员函数
    c.x = 10.0f;      // 可以：c 不是 const

    // a.x = 0.0f;    // 编译错误：a 是 const
    return 0;
}
```

如果把代码里所有的 `const` 都删掉，会发生什么？

- `a + b` 会编译失败，因为非 const 成员函数不能作用于 const 对象。
- 我们失去了一个重要信息：**这个函数到底会不会修改对象？**

在没有 `const` 的世界里，判断一个函数是否安全地只读对象，只能靠函数名猜、靠文档读、靠运行时调试。`const` 的价值在于：把「我不会改」这句话写进类型系统，让编译器替你监督调用者和实现者双方。

---

## 问题 1：const 修饰变量和参数，到底约束了谁？

`const` 放在类型前面，意思是「这个对象不可修改」。位置不同，约束对象不同：

```cpp
// flags: -O0 -g
#include <iostream>

void read_only(const int* ptr) {
    // *ptr = 10;   // 错误：不能通过 ptr 修改指向的 int
    std::cout << *ptr << '\n';
}

void cannot_reseat(int* const ptr) {
    *ptr = 20;       // 可以：ptr 指向的 int 不是 const
    // ptr = nullptr; // 错误：ptr 本身不能重新指向别处
}

int main() {
    int a = 1;
    read_only(&a);
    cannot_reseat(&a);
    std::cout << a << '\n';   // 20
    return 0;
}
```

关键区分：

- `const T*`：指向常量对象的指针。指针本身可变，但不能通过它修改对象。
- `T* const`：常量指针。指针本身不可变，但可以通过它修改对象。
- `const T&`：对常量对象的引用。引用不能重新绑定，也不能通过它修改对象。

作为函数参数时，`const T&` 是引擎里最常见的只读约定。它同时完成了两件事：**避免拷贝** 和 **承诺只读**。

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>

void print_name(const std::string& name) {   // 不会改 name，也不会拷贝
    std::cout << name << '\n';
}

int main() {
    std::string s = "player_one";
    print_name(s);                 // 绑定到变量
    print_name("temporary");       // 临时对象也能绑定到 const&
    return 0;
}
```

调用者看到这个签名，就知道可以把大字符串放心传进去，不必担心被修改或产生拷贝开销。

---

## 问题 2：const 成员函数是什么意思？为什么它不能随便调用普通成员函数？

成员函数后面加 `const`，表示这个函数不会修改对象的逻辑状态。编译器会把这个函数的隐式 `this` 指针从 `T*` 变成 `const T*`。

```cpp
// flags: -O0 -g
#include <iostream>

class Counter {
public:
    explicit Counter(int n) : value_(n) {}

    int get() const {       // this 是 const Counter*
        return value_;
    }

    void inc() {            // this 是 Counter*
        ++value_;
    }

private:
    int value_;
};

int main() {
    const Counter c{5};
    std::cout << c.get() << '\n';   // 可以：get 是 const
    // c.inc();                     // 错误：inc 不是 const，不能作用于 const 对象
    return 0;
}
```

`const` 成员函数里不能调用非 `const` 成员函数，因为后者可能会修改对象。这是一个层层传递的契约：如果你承诺了「我不会改」，那么你调用的所有成员函数也必须承诺「不会改」。

这种约束有时会让人觉得麻烦，但它正是 `const` 正确性的核心价值：**把「只读」从口头约定变成可静态检查的类型约束**。

---

## 问题 3：const 返回值有什么用？它是不是越多越好？

返回值加 `const` 曾经是 C++ 教材里的一个常见建议，用来防止无意义的赋值：

```cpp
// flags: -O0 -g
#include <iostream>

struct Vec2 {
    float x, y;
};

const Vec2 operator+(const Vec2& lhs, const Vec2& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

int main() {
    Vec2 a{1, 2}, b{3, 4};
    // (a + b) = Vec2{5, 6};   // 编译错误：临时对象不能赋值
    Vec2 c = a + b;            // 可以
    std::cout << c.x << '\n';
    return 0;
}
```

返回 `const Vec2` 能阻止 `(a + b) = c` 这种明显错误的代码。但现代 C++ 里，这个做法有一个副作用：**它会阻止移动语义**。

```cpp
// flags: -O0 -g
#include <iostream>
#include <vector>

struct BigBuffer {
    std::vector<int> data;
    BigBuffer() : data(1000) {}
};

BigBuffer make_big() {
    return BigBuffer{};   // prvalue，C++17 起直接构造到 v，无需拷贝/移动
}

int main() {
    BigBuffer v = make_big();   // 没有拷贝，没有移动
    std::cout << v.data.size() << '\n';
    return 0;
}
```

如果 `make_big` 返回 `const Vec2`，调用方无法把它当成右值移动到目标对象里，因为 `const` 对象不能移动。因此，现代 C++ 的常规建议是：

- **普通值类型返回值不加 `const`**。依赖 RVO/NRVO 和移动语义获得最佳性能。
- **返回引用时，根据是否需要修改决定加不加 `const`**。`T&` 允许修改，`const T&` 只读。
- **返回 `const` 值主要用于某些特殊语义场景**，比如重载运算符防止对临时对象赋值，但要警惕对移动的抑制。

---

## 问题 4：返回引用时，怎么才能避免悬垂？

返回引用最大的风险与参数传递一样：**引用必须绑定到生命周期足够的对象**。

安全返回引用的常见场景：

1. **返回成员变量的引用**：对象本身还活着，成员就还活着。
2. **返回容器元素的引用**：容器本身还活着，元素引用就有效（但要注意扩容后迭代器/引用失效）。
3. **返回通过参数传入的对象的引用**：对象由调用者管理生命周期。

绝对不要返回局部变量的引用：

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>

const std::string& make_label() {
    std::string label = "enemy_001";   // 局部对象
    return label;                       // 悬垂引用！
}

int main() {
    const std::string& s = make_label();
    std::cout << s << '\n';   // 未定义行为
    return 0;
}
```

在引擎里，一个常见的安全模式是：getter 返回 `const T&`，setter 接收 `const T&` 或按值移动；如果返回内部缓冲区的指针/引用，必须在文档里明确生命周期约束。

---

## 问题 5：mutable 是 const 的「后门」吗？什么时候该用？

`mutable` 允许在 `const` 成员函数里修改被修饰的成员。它通常用于「逻辑上不变，但实现上需要变化」的场景：

- **缓存**：第一次调用 `get_area()` 计算并缓存结果，后续直接返回缓存。
- **同步原语**：`std::mutex` 在 const 函数里加锁，逻辑状态没变，但锁状态变了。
- **引用计数/调试计数**：对象在逻辑上没变，但内部计数器需要更新。

```cpp
// flags: -O0 -g
#include <iostream>
#include <cmath>

class Circle {
public:
    explicit Circle(float r) : radius_(r), area_cached_(false) {}

    float radius() const { return radius_; }

    float area() const {
        if (!area_cached_) {
            area_ = 3.14159f * radius_ * radius_;
            area_cached_ = true;
        }
        return area_;
    }

private:
    float radius_;
    mutable float area_;
    mutable bool area_cached_;
};

int main() {
    const Circle c{2.0f};
    std::cout << c.area() << '\n';   // 12.5664
    std::cout << c.area() << '\n';   // 再次调用直接返回缓存
    return 0;
}
```

`mutable` 不是任意突破 `const` 的借口。如果一个成员被 `mutable` 修饰，你必须能向阅读代码的人解释：为什么这个字段的变化不会改变对象的「逻辑状态」。如果解释不了，说明不该用 `mutable`，而应该把函数改成非 `const`。

---

## 问题 6：const_cast 能用来干什么？为什么引擎里要慎用？

`const_cast` 可以去掉 `const` 限定。它有少数合法用途，比如调用一个旧的、没有 `const` 重载的 C 接口时：

```cpp
// flags: -O0 -g
#include <iostream>
#include <string>

void legacy_api(char* buf) {
    // 模拟一个旧的 C 接口：确信它不会修改 buf
    std::cout << buf << '\n';
}

void call_legacy(const std::string& s) {
    legacy_api(const_cast<char*>(s.c_str()));
}

int main() {
    std::string msg = "hello legacy";
    call_legacy(msg);
    return 0;
}
```

但用 `const_cast` 去修改一个真正 `const` 的对象，是 **未定义行为**：

```cpp
// flags: -O0 -g
#include <iostream>

int main() {
    const int x = 10;
    int* p = const_cast<int*>(&x);
    // *p = 20;   // 未定义行为！不要取消注释这行
    std::cout << "x = " << x << ", *p = " << *p << '\n';
    return 0;
}
```

引擎代码里应尽量避免 `const_cast`。它通常意味着接口设计有漏洞，或者在绕过类型系统做危险操作。如果发现自己频繁需要 `const_cast`，应该回头检查：是不是某些函数该加 `const` 重载，或者是不是不该在 const 上下文里调用那个函数。

---

## 总结

- `const` 是接口契约，不是可选的优化提示。它把「只读」承诺写进类型系统，由编译器强制执行。
- `const T&` 参数同时表达「不拷贝」和「不修改」，是引擎里只读访问大对象的标准选择。
- `const` 成员函数把 `this` 变成 `const T*`，因此不能调用非 `const` 成员函数。
- 返回值通常不加 `const`，以免抑制移动语义；返回引用时要确保生命周期安全，避免返回局部对象引用。
- `mutable` 用于逻辑上不变、实现上可变的成员（缓存、锁、调试计数），不应滥用。
- `const_cast` 能去 const 限定，但修改真正的 const 对象是未定义行为；引擎里应尽量避免。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 接口契约与线程安全 | getter 返回 `const T&`；需要缓存的组件使用 `mutable` 缓存字段；资源句柄在 const 接口中通过 `mutable std::mutex` 保证线程安全 |
| **UE** | `UObject` 与容器接口 | `const UObject*` 表示只读对象引用；`TArray` 的 `operator[]` 提供 const 与非 const 重载；大量 `const` 成员函数支持蓝图反射层安全读取属性 |

> [!note] 关键取舍
> SelfGameEngine 和 UE 都把 `const` 正确性视为代码可读性的基础设施。区别在于：UE 由于历史包袱和宏生成代码，偶尔需要 `const_cast` 兼容旧接口；SelfGameEngine 更强调「const 正确性从零开始」，把 `mutable` 严格限定在缓存与同步原语两类场景。返回值语义上，两者都遵循现代 C++ 建议：**值返回不加 const，引用返回明确生命周期**。

---

> 相关笔记：
> - [[Notes/C++编程/参数传递与返回值语义/引用、指针与参数传递|引用、指针与参数传递]]
> - [[Notes/C++编程/类与对象入门/结构体与类：成员、访问控制与 this 指针#访问控制|访问控制]]
> - [[Notes/C++编程/对象内存模型与底层机制/对象内存布局：从 struct 到 class|对象内存布局：从 struct 到 class]]
> - [[Notes/C++编程/值类别与引用语义/返回值优化与 guaranteed elision|返回值优化与 guaranteed elision]]
