---
title: std::function 的实现与开销
date: 2026-06-13
tags:
  - C++
  - std::function
  - 类型擦除
  - 可调用对象
  - 小对象优化
  - 虚函数
aliases:
  - std::function
  - 类型擦除
  - Type Erasure
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# std::function 的实现与开销

> [!info] 一句话概括
> **std::function** 是 C++ 标准库提供的一个「类型擦除」容器：它能把形态各异的可调用对象（函数指针、Lambda、函数对象、成员函数绑定结果）装进同一个类型里。代价是额外的内存间接层和一次虚函数级别的调用开销。

---

## 问题 0：为什么需要类型擦除？

想象你要写一个回调注册接口。用户可能想注册：

- 一个普通函数 `void on_click();`
- 一个 Lambda `[](int x){ ... }`
- 一个重载了 `operator()` 的自定义对象
- 一个成员函数绑定 `std::bind(&Foo::bar, &foo)`

这些可调用对象的**类型完全不同**。如果你用模板参数保存回调：

```cpp
template<typename F>
void register_callback(F&& f);
```

那么每注册一种回调类型，编译器都会实例化出一份不同的代码。这在很多场景下没问题，但如果你希望：

1. 把回调统一放进一个容器（比如 `std::vector<Callback>`）。
2. 在类成员里保存一个回调，但不想把整个类也变成模板。
3. 在编译单元边界、插件接口或事件系统中传递回调。

你就需要一个**统一的类型**来表示这些千差万别的可调用对象。这就是**类型擦除（Type Erasure）**要解决的问题：擦掉具体类型，只保留统一的调用接口。

> [!abstract]
> **类型擦除**是一种编程技术，它把不同类型的对象包装进同一个接口类型中，使得调用方无需知道对象的真实类型。`std::function` 是 C++ 标准库中最常见的类型擦除容器之一。

---

## 问题 1：没有 `std::function` 时，我们能用什么？

### 方案 A：函数指针

```cpp
using Callback = void(*)(int);
std::vector<Callback> callbacks;
```

函数指针非常轻量：通常就是一个地址，调用时直接跳转。但它有两大局限：

1. **不能携带状态**。纯函数指针只能指向全局函数或静态成员函数，无法保存 `this` 指针或 Lambda 捕获的变量。
2. **不能指向非静态成员函数**。因为非静态成员函数需要一个对象来调用，而普通函数指针没有地方存这个对象。

### 方案 B：模板

```cpp
template<typename F>
void register_callback(F&& f);
```

模板能处理任何可调用对象，但会把类型信息「泄漏」到接口外面。只要你想把回调存进一个固定类型的成员变量或容器，模板就会让类的签名一起变，破坏了接口的稳定性。

### 方案 C：虚函数

```cpp
struct ICallback {
    virtual void invoke(int) = 0;
    virtual ~ICallback() = default;
};
```

这是类型擦除的经典雏形：基类定义统一接口，每个具体回调继承并重写。但它也有代价——每次调用都要走虚表间接跳转，而且需要堆分配来保存派生对象（除非你自己做内联缓冲）。

> `std::function` 本质上就是「虚函数 + 可选的内联缓冲」的工业级封装，只是它把派生类的定义藏在了实现细节里。

---

## 问题 2：`std::function` 里面到底存了什么？

`std::function<R(Args...)>` 的内存布局可以粗糙地理解成下面这个结构：

```cpp
// 概念性示意，不是 libstdc++ 的真实源码
template<typename R, typename... Args>
class function {
    // 1. 一个函数指针：负责「真正调用」被包装对象
    R (*invoke_)(const void* storage, Args... args);

    // 2. 一个函数指针或函数指针组：负责拷贝/移动/销毁
    void (*manager_)(op, const void* src, void* dst);

    // 3. 一块缓冲，用来放被包装对象本身
    alignas(max_align_t) char buffer_[N];
};
```

三个关键部分：

| 成员 | 作用 |
|------|------|
| `invoke_` | 调用被包装对象。因为真实类型被擦除了，只能通过这个函数指针间接调用。 |
| `manager_` | 管理对象生命周期：构造、拷贝、移动、销毁。 |
| `buffer_` | 一块固定大小的内联存储。小对象可以直接放在这里，避免堆分配。 |

这里有两点值得注意：

1. **调用要通过函数指针转接**。无论底层是函数指针、Lambda 还是函数对象，最终都变成一次函数指针调用。这相当于一次虚函数调用的开销（间接跳转 + 可能的分支预测失败）。
2. **小对象优化（SOO, Small Object Optimization）**。如果 Lambda 捕获的变量很少（比如只捕获一个 `int` 或一个指针），整个闭包对象可以塞进 `buffer_`，不用 `new`。 libstdc++ 的 `buffer_` 大小通常是 16 字节或 24 字节，libc++ 也类似，具体数值不保证跨平台一致。

---

## 问题 3：小对象优化（SOO）到底怎么做？

小对象优化的核心判断是：**被包装对象能不能放进 `std::function` 内部的固定缓冲里？**

```cpp
// flags: -O0 -g
#include <iostream>
#include <functional>

int main() {
    int a = 10;

    // 只捕获一个 int，闭包很小，通常能放进内部缓冲
    std::function<void()> f1 = [a]() { std::cout << a << "\n"; };

    // 捕获一个很大的数组，闭包很大，只能堆分配
    int big[1024] = {};
    std::function<void()> f2 = [big]() { (void)big; };

    std::cout << "sizeof(f1) = " << sizeof(f1) << "\n";
    std::cout << "sizeof(f2) = " << sizeof(f2) << "\n";  // 大小相同，因为 buffer 是固定大小
    return 0;
}
```

无论闭包大小如何，`sizeof(std::function)` 都是固定的。区别在于：

- 小对象：`buffer_` 直接存放闭包对象，没有堆分配。
- 大对象：在堆上 `new` 一个闭包对象，`buffer_` 里只放一个指针。

> [!warning]
> 不要把「小对象优化」理解为「不分配内存」。`std::function` 对象本身仍然占用固定大小的栈内存或类成员内存，而且它内部的管理函数指针等元数据也会带来额外空间开销。SOO 只是避免了大对象的**额外堆分配**。

---

## 问题 4：虚函数调用开销到底有多大？

`std::function` 不是通过 C++ 的 `virtual` 函数实现类型擦除的，但它的调用路径和虚函数非常像：

1. 通过存储的 `invoke_` 函数指针找到实际调用代码。
2. 把 `buffer_`（或堆指针）的地址传过去。
3. 在内部把 `void*` 转换回真实闭包类型，再执行调用。

这意味着：

- **无法内联**。编译器在调用点看不到真实函数体，也就不能把调用优化掉。
- **间接跳转**。处理器分支预测器可能失效，造成流水线停顿。
- **额外传参**。需要把存储缓冲的地址作为隐藏参数传递。

在大多数游戏循环或事件分发场景中，这种开销完全可以接受。但在每帧调用成千上万次的超热点路径上（比如 ECS 系统的每个实体每帧都走 `std::function`），累积起来就不可忽视了。

---

## 问题 5：与函数指针和 Lambda 的对比

| 特性 | 函数指针 | Lambda（闭包类型） | `std::function` |
|------|---------|------------------|-----------------|
| 能否携带状态 | ❌ 不能 | ✅ 捕获变量 | ✅ 捕获变量 |
| 统一类型 | ✅ `void(*)(int)` | ❌ 每次类型不同 | ✅ `std::function<void(int)>` |
| 调用开销 | 一次间接跳转 | 可内联，零额外开销 | 一次间接跳转 + 类型擦除转接 |
| 内存布局 | 一个指针 | 捕获变量按布局存放 | 固定大小，可能含内部缓冲和堆指针 |
| 生命周期管理 | 无 | 自动（栈/RAII） | 通过 manager_ 自动管理 |

> [!tip]
> 如果你的回调类型在编译期就能确定，并且不需要统一容器存储，优先用 Lambda 或函数对象模板。只有当真正需要「类型无关的统一接口」时，才付出 `std::function` 的类型擦除代价。

---

## 问题 6：手写一个极简类型擦除 Function

理解了内部结构，就可以动手写一个简化版 `Function`，用于 Day 24 的练习预热。

```cpp
// flags: -O0 -g
#include <iostream>
#include <utility>

// 简化版 Function，只支持 copyable 的小对象，不做 SOO 大小判断
template<typename Signature>
class Function;

template<typename R, typename... Args>
class Function<R(Args...)> {
    struct CallableBase {
        virtual R invoke(Args... args) = 0;
        virtual ~CallableBase() = default;
    };

    template<typename F>
    struct Callable : CallableBase {
        F f_;
        Callable(F f) : f_(std::move(f)) {}
        R invoke(Args... args) override { return f_(args...); }
    };

    CallableBase* impl_ = nullptr;

public:
    Function() = default;

    template<typename F>
    Function(F f) : impl_(new Callable<F>(std::move(f))) {}

    R operator()(Args... args) const {
        return impl_->invoke(args...);
    }

    ~Function() { delete impl_; }
};

int main() {
    int x = 5;
    Function<int(int)> f = [x](int y) { return x + y; };
    std::cout << f(3) << "\n";  // 输出 8
    return 0;
}
```

这个极简版本已经展示了类型擦除的核心：

1. 用基类指针 `CallableBase*` 隐藏具体类型。
2. 用虚函数 `invoke` 做统一调用。
3. 用模板 `Callable<F>` 把任意可调用对象包起来。

> [!warning]
> 上面的代码省略了拷贝构造、移动构造、空状态检查、小对象优化和异常安全，只是帮助理解原理。真正工业级的实现还要处理 `noexcept`、内存对齐、`std::move_only_function`、SBO 大小选择等复杂问题。

---

## 问题 7：为什么「成员函数指针」相关知识在这里有用？

`std::function` 最常见的用途之一是把成员函数绑定成回调。但成员函数指针本身不能直接调用，因为它缺少 `this`：

```cpp
struct Button {
    void on_click(int x);
};

Button b;
std::function<void(int)> cb = std::bind(&Button::on_click, &b, std::placeholders::_1);
```

`std::bind` 会生成一个闭包对象，把 `&b` 存起来，把成员函数指针也存起来，调用时再通过成员函数指针的底层机制转发。想要了解成员函数指针为什么比普通函数指针大、为什么调用时需要 `this`，可以参考 [[Notes/C++编程/对象内存模型与底层机制/成员函数指针的底层表示|成员函数指针的底层表示]]。

---

## 问题 8：什么时候该用 `std::function`，什么时候不该用？

### 适合用 `std::function` 的场景

- 事件系统、委托系统、信号槽：回调类型多样，需要统一存储和分发。
- 插件接口或跨模块边界：类型在编译期无法确定。
- 测试或脚本桥接：需要运行时替换行为。

### 不建议用 `std::function` 的场景

- 高频调用的数学/渲染循环：优先用函数指针或模板。
- 内存敏感的内联代码：`std::function` 通常比普通函数指针大得多（libstdc++ 上约 32 字节）。
- 只需要「无状态回调」的地方：直接用函数指针更轻量。

---

## 总结

- `std::function` 是标准库提供的**类型擦除**容器，能把不同可调用对象装进统一类型。
- 内部通过函数指针（或虚函数式机制）间接调用，带来一次间接跳转开销。
- 采用**小对象优化（SOO）**：小闭包放在内部缓冲，避免堆分配；大闭包退化为堆分配。
- 与函数指针相比，它能携带状态；与裸 Lambda 相比，它牺牲了内联性和内存紧凑性。
- 成员函数绑定、生命周期管理、拷贝语义等细节，是手写 `Function` 类和引擎委托系统的前置知识。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 事件系统、脚本回调、行为树条件节点 | 手写 `Function<R(Args...)>` 作为 `std::function` 的轻量替代；利用 SOO 减少帧分配器压力；高频路径改用函数指针或模板 |
| **UE** | `TFunction`、`TFunctionRef`、`TDelegate` | `TFunction` 内部实现与 `std::function` 类似，提供类型擦除和 SOO；`TFunctionRef` 不拥有对象，仅做引用，避免拷贝和分配；`DECLARE_DELEGATE` 系列宏生成强类型委托，避免运行时类型误用 |
| **Bevy** | ECS 系统回调、事件读写 | Rust 没有 `std::function` 的等价物，系统函数通常是普通函数或闭包 trait；通过 `System` trait 和类型擦除实现统一调度，性能敏感路径使用普通函数指针或模板系统 |

> [!note] 关键取舍
> 工业级引擎通常不会在高频路径直接使用 `std::function`，而是提供分层方案：
> - `TFunctionRef` / 裸函数指针：只引用、不拥有，零分配。
> - 手写 SOO Function：拥有对象，但避免堆分配和标准库 ABI 依赖。
> - `std::function` / `TFunction`：通用场景，牺牲性能换取灵活性。
> 理解 `std::function` 的内部实现，才能在做这些取舍时有依据。

---

> 相关笔记：
> - [[Notes/C++编程/对象内存模型与底层机制/虚函数与多态本质|虚函数与多态本质]] — 类型擦除的底层派发机制与虚表思想
> - [[Notes/C++编程/对象内存模型与底层机制/成员函数指针的底层表示|成员函数指针的底层表示]] — 成员函数绑定与委托实现基础
> - [[Lambda 表达式与闭包|Lambda 表达式与闭包]] — `std::function` 最常包装的可调用对象
