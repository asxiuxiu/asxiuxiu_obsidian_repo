---
title: 返回值优化与 guaranteed elision
date: 2026-06-13
tags:
  - C++
  - RVO
  - NRVO
  - guaranteed elision
  - 返回值优化
  - C++17
  - 移动语义
  - 性能优化
aliases:
  - RVO
  - NRVO
  - guaranteed elision
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 返回值优化与 guaranteed elision

> [!info] 一句话概括
> **RVO / NRVO** 让编译器把函数返回的对象**直接构造在调用方指定的内存里**，省掉一次拷贝或移动；C++17 起，对「纯右值」的这种省略成为**语言规则（guaranteed elision）**，不再只是编译器恩赐的优化。

---

## 问题 0：函数返回一个大对象时，直觉上会发生什么？

假设你写了一个 3D 数学类型 `Vec3`，里面只有三个 `float`，看起来很轻。但如果它内部管理着动态数组，或者干脆是一个 4×4 的 `Mat4`，返回它时你是不是会担心："函数里创建一个对象，调用方再接收一次，这中间是不是要拷贝两次？"

在没有优化的世界里，这个直觉是对的：

1. 函数内部先构造一个局部对象。
2. 函数返回时，把这个局部对象**拷贝**到调用方准备的临时空间里。
3. 调用方再用这个临时对象**拷贝**初始化最终变量。

对于[[Notes/C++编程/构造、析构与拷贝控制/拷贝构造函数与拷贝赋值运算符#问题 1：拷贝构造和拷贝赋值，到底在哪些场景下被调用？|拷贝构造]]会触发深拷贝的类型来说，这意味着两次堆内存申请和两次数据复制，性能直接爆炸。

但 C++ 早就想到了这件事。从 C++98 开始，编译器就可以做一种叫**返回值优化（Return Value Optimization，RVO）**的省略；C++11 又扩展出**具名返回值优化（Named Return Value Optimization，NRVO）**；到了 C++17，更激进的**guaranteed elision**把一部分省略写进了语言标准，即使关闭优化、即使类型根本没有拷贝/移动构造函数，也能正常工作。

理解这三层机制，是你后续优化 `Vec3` / `Mat4` 返回语义的前提。

---

## 问题 1：RVO 是什么？编译器怎么"省略"拷贝？

**RVO** 针对的是函数返回一个**无名临时对象**（也就是 C++ 里的 **prvalue，纯右值**）的场景。它的核心思想很简单：既然这个临时对象迟早要被拿到调用方手里，不如一开始就在调用方指定的内存位置上把它构造出来，省掉中间的拷贝。

> [!abstract]
> **RVO（Return Value Optimization）**：当函数按值返回一个 prvalue 时，编译器可以直接在调用方的目标内存里构造该对象，跳过从函数内部到调用方的拷贝/移动。

用一个带追踪的类来看最直观：

```cpp
// flags: -O0 -g -std=c++17
#include <iostream>

struct Tracer {
    Tracer()                  { std::cout << "default ctor\n"; }
    Tracer(const Tracer&)     { std::cout << "copy ctor\n"; }
    Tracer(Tracer&&) noexcept { std::cout << "move ctor\n"; }
    ~Tracer()                 { std::cout << "dtor\n"; }
};

Tracer make_prvalue() {
    return Tracer{};  // 返回一个 prvalue（纯右值）
}

int main() {
    Tracer t = make_prvalue();  // C++17 起 guaranteed elision
    return 0;
}
```

如果你用 C++17 编译这段代码，即使加 `-O0`，输出也只有：

```text
default ctor
dtor
```

也就是说，**既没有调用拷贝构造，也没有调用移动构造**。编译器把 `Tracer{}` 直接构造在了 `t` 的内存里。

在 C++17 之前，这种优化叫 RVO，是编译器**可选**的优化；C++17 之后，对 prvalue 的这条路径变成了**强制**的，也就是 guaranteed elision。

---

## 问题 2：具名局部变量也能省略吗？NRVO 是什么？

RVO 处理的是 `return Tracer{};` 这种"返回临时对象"的情况。但工程代码里更常见的是先声明一个局部变量，再返回它：

```cpp
Tracer make_named() {
    Tracer local;
    // ... 可能还有一些赋值/计算 ...
    return local;  // 返回一个具名局部变量
}
```

这种场景下，编译器也可以做类似的省略，叫 **NRVO（Named Return Value Optimization，具名返回值优化）**。它的目标同样是把 `local` 直接构造在调用方的目标内存里，避免返回时的拷贝或移动。

**NRVO** 的目标和 RVO 类似：把具名局部变量直接构造在调用方内存里，避免返回时的拷贝或移动。但两者有一个关键区别：

| 优化 | 返回对象 | C++17 是否强制 | 优化失败时的兜底 |
|------|---------|---------------|----------------|
| **RVO** | prvalue（`return T{};`） | ✅ guaranteed elision | 无（语言层面已省略） |
| **NRVO** | 具名局部变量（`return local;`） | ❌ 仍是编译器可选优化 | 通常走移动构造；若无移动则走拷贝构造 |

---

## 问题 3：C++17 的 guaranteed elision 到底改了什么？

要理解 C++17 的变革，需要先明白**C++17 之前的 prvalue 语义**。

在 C++11 / C++14 里，一个 prvalue（比如 `Tracer{}`）会被先**物化（materialize）**成一个临时对象，这个临时对象有地址、有生命周期，然后再被拷贝或移动到最终目标里。RVO 只是允许编译器**跳过**这个临时对象的构造和拷贝，但概念上仍然存在。

C++17 做了一个大胆的改动：**prvalue 不再被自动物化**。它只是一个"将要构造对象的初始化器"，只有在真正需要地址的时候才会变成 glvalue。结果就是：

```cpp
Tracer t = make_prvalue();
```

在 C++17 语义下，`make_prvalue()` 返回的 prvalue 不是临时对象，而是直接告诉编译器"在 `t` 的地址上构造一个 `Tracer`"，因此**根本不需要拷贝/移动构造函数存在**。

> [!note]
> **guaranteed elision** 的核心收益：即使类型删除了拷贝构造和移动构造，按值返回 prvalue 依然合法。

```cpp
// flags: -O0 -g -std=c++17
#include <iostream>

struct NoCopyNoMove {
    NoCopyNoMove()                            { std::cout << "ctor\n"; }
    NoCopyNoMove(const NoCopyNoMove&)         = delete;
    NoCopyNoMove(NoCopyNoMove&&)              = delete;
    NoCopyNoMove& operator=(const NoCopyNoMove&) = delete;
    NoCopyNoMove& operator=(NoCopyNoMove&&)   = delete;
    ~NoCopyNoMove()                           { std::cout << "dtor\n"; }
};

NoCopyNoMove factory() {
    return NoCopyNoMove{};  // ✅ C++17：合法，直接在调用方内存构造
}

int main() {
    NoCopyNoMove x = factory();
    return 0;
}
```

这段代码在 C++17 之前会编译失败，因为返回的临时对象需要被移动/拷贝到 `x`；C++17 之后， guaranteed elision 让它直接成立。

---

## 问题 4：什么时候返回值优化会失效？

知道了 RVO / NRVO 的强大，也要清楚它们的边界。以下几种情况，优化会失效或无法触发：

### 4.1 返回的不是同类型局部变量

函数参数、全局变量、成员变量、解引用指针后的对象，或者局部变量类型与返回类型不一致，都无法触发 NRVO，通常只能走拷贝或移动。

### 4.2 多个返回路径返回不同对象

```cpp
Tracer choose(bool flag) {
    Tracer a;
    Tracer b;
    return flag ? a : b;  // NRVO 通常失效：编译器无法确定构造 a 还是 b
}
```

这里编译器没法提前知道该在调用方内存里构造 `a` 还是 `b`，所以 NRVO 通常失效。但别慌：C++11 起的**隐式移动**会兜底，把 `a` 或 `b` 移动出去（要求移动构造存在且通常应为 `noexcept`）。

### 4.3 类型不匹配

返回类型和局部变量类型不同，NRVO 无法触发（C++20 起对派生类→基类的返回做了一些放宽）。

### 4.4 写了 `return std::move(local);`

这是最常见的反模式：

```cpp
Tracer worse() {
    Tracer local;
    return std::move(local);  // ❌ 阻止 NRVO，强制走移动构造
}
```

`std::move` 把 `local` 变成了 xvalue（将亡值），表达式不再是"返回具名局部变量"的形式，NRVO 无法应用。结果你不但没加速，反而多了一次移动构造。

> [!warning]
> **不要对返回值使用 `std::move`**。现代 C++ 会自己决定是 NRVO 还是隐式移动，你的 `std::move` 只会帮倒忙。

---
## 问题 5：移动语义与 RVO 是什么关系？

RVO / NRVO 的目标都是"零开销"——对象只构造一次。但当 NRVO 无法触发时，现代 C++ 还有第二道防线：**移动语义**。

在 C++11 之前，如果 NRVO 失败，只能回退到**拷贝构造**。C++11 引入右值引用和移动构造后，返回一个即将销毁的局部变量时，编译器可以隐式调用**移动构造**，把资源"偷"出来，而不是深拷贝。

这也解释了为什么移动构造函数通常要声明 `noexcept`：如果移动可能抛异常，容器或编译器为了保证异常安全，会**回退到拷贝**。关于移动语义的完整讨论见 [[Notes/C++编程/值类别与引用语义/值类别与移动语义#二、右值引用与移动语义|右值引用与移动语义]]。

| 机制 | 触发条件 | 开销 | 是否可手动干预 |
|------|---------|------|---------------|
| **RVO / guaranteed elision** | 返回 prvalue | 零 | ❌ 不要写 `std::move` |
| **NRVO** | 返回同类型具名局部变量 | 零 | ❌ 保持 `return local;` |
| **隐式移动** | NRVO 失败且对象为右值 | O(1) | ❌ 不要写 `std::move` |
| **拷贝构造** | 以上皆不适用 | O(n) | ⚠️ 通常是设计信号 |

---

## 问题 6：如何用代码验证这些行为？

最有效的方法是给类加上显式的拷贝/移动构造函数，并打印日志。下面这个例子把 RVO、NRVO、NRVO 失败、错误使用 `std::move` 四种场景放在了一起：

```cpp
// flags: -O0 -g -std=c++17
#include <iostream>

struct Probe {
    Probe()                           { std::cout << "  default ctor\n"; }
    Probe(const Probe&)               { std::cout << "  copy ctor\n"; }
    Probe(Probe&&) noexcept           { std::cout << "  move ctor\n"; }
    ~Probe()                          { std::cout << "  dtor\n"; }
};

Probe rvo() {
    return Probe{};  // prvalue，C++17 guaranteed elision
}

Probe nrvo() {
    Probe p;
    return p;        // 具名局部变量，依赖 NRVO
}

Probe nrvo_fail(bool flag) {
    Probe a;
    Probe b;
    return flag ? a : b;  // NRVO 通常失效，隐式移动兜底
}

Probe anti_pattern() {
    Probe p;
    return std::move(p);  // 阻止 NRVO，强制移动
}

int main() {
    std::cout << "rvo():\n";
    Probe x1 = rvo();

    std::cout << "nrvo():\n";
    Probe x2 = nrvo();

    std::cout << "nrvo_fail(true):\n";
    Probe x3 = nrvo_fail(true);

    std::cout << "anti_pattern():\n";
    Probe x4 = anti_pattern();

    return 0;
}
```

在 GCC/Clang 使用 `-O0 -std=c++17` 编译，典型输出会揭示四条规则：

- `rvo()` 只触发一次默认构造 → guaranteed elision 生效。
- `nrvo()` 也只触发一次默认构造 → 当前编译器对这个简单场景做了 NRVO。
- `nrvo_fail(true)` 两个默认构造 + 一个移动构造 → NRVO 失效，隐式移动兜底。
- `anti_pattern()` 默认构造 + 移动构造 → 你亲手阻止了 NRVO。

---

## 总结

- **RVO** 处理 `return T{};` 这种返回 prvalue 的场景；C++17 起成为** guaranteed elision**，是语言规则，不再依赖编译器优化。
- **NRVO** 处理 `return local;` 这种返回具名局部变量的场景，仍属编译器可选优化，但在主流编译器（GCC/Clang/MSVC）的较高优化级别下通常有效。
- 返回值优化失效时，现代 C++ 会优先使用**隐式移动**兜底，最后再回退到拷贝构造。
- **不要对返回值写 `std::move`**，它会阻止 NRVO，导致不必要的移动构造。
- 验证时给类加上显式拷贝/移动构造函数并打印日志，用 `-O0` 与 `-O2` 对比，能清晰看到 RVO/NRVO/移动/拷贝的差异。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 数学类型（`Vec3`、`Mat4`、`Quat`）的返回与算术运算 | 这些类型虽然体积小，但每帧被创建和返回的次数极高。利用 RVO / NRVO 可以让 `operator+`、变换函数链返回时零拷贝；若优化失败，移动语义（或 trivially copyable 的按值传递）兜底，避免深拷贝 |
| **UE** | `FVector`、`FMatrix`、`FQuat` 等值类型的函数接口 | UE 的数学库大量使用按值返回，并依赖编译器 RVO/NRVO 保持性能。源码中常见 `return FVector(...);` 的写法，而不是先声明局部变量再 `std::move` 返回，正是为了避免破坏 guaranteed elision 和 NRVO |

> [!note] 关键取舍
> 对于轻量级值类型（如 12 字节的 `Vec3`），返回值优化省掉的不一定是"堆内存拷贝"，而是**寄存器/栈上的构造次数**和**调用约定开销**；对于管理外部资源的大型对象（如网格数据、GPU 句柄），RVO/NRVO 省掉的则是昂贵的深拷贝或资源创建。无论哪种情况，编写返回代码时都应遵循同一个原则：**直接返回局部变量或 prvalue，绝不写 `return std::move(x);`**。

---

> 相关笔记：
> - [[Notes/C++编程/值类别与引用语义/值类别与移动语义#二、右值引用与移动语义|右值引用与移动语义]] — 理解右值引用、`std::move`、隐式移动的底层机制
> - [[Notes/C++编程/构造、析构与拷贝控制/拷贝构造函数与拷贝赋值运算符#问题 1：拷贝构造和拷贝赋值，到底在哪些场景下被调用？|拷贝构造与拷贝赋值运算符]] — 理解拷贝构造在返回值场景中的角色
> - [[Notes/C++编程/值类别与引用语义/noexcept 关键字|noexcept 与异常规格]] — 理解为什么 `noexcept` 移动构造会影响容器和返回值的优化选择
