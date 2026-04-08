---
title: C++ noexcept 关键字
date: 2026-03-20
tags:
  - C++
  - 异常处理
  - 性能优化
  - 移动语义
aliases:
  - noexcept
  - 异常规格
---

> [[索引|← 返回 C++编程索引]]

# C++ noexcept 关键字

> [!info] 概述
> `noexcept` 是 C++11 引入的**异常规格说明符**，用于声明函数不会抛出异常。它有两种用法：作为**说明符**（声明函数不抛异常）和作为**运算符**（编译期查询表达式是否不抛异常）。
>
> `noexcept` 与 [[C++ 值类别与移动语义]] 紧密相关——标准库容器依赖它来决定是否使用移动操作。

---

## 一、基本语法

### 1.1 noexcept 说明符

```cpp
void foo() noexcept;          // 保证不抛出任何异常
void bar() noexcept(true);    // 等价写法
void baz() noexcept(false);   // 等价于没有 noexcept（可能抛异常）
```

### 1.2 noexcept 运算符

```cpp
// noexcept(expr) 在编译期返回 bool
// 不执行 expr，只检查其异常规格
static_assert(noexcept(1 + 1));          // true：算术运算不抛异常
static_assert(!noexcept(new int));       // false：new 可能抛 bad_alloc
```

### 1.3 条件 noexcept

```cpp
// 常见于泛型代码：当 T 的操作不抛异常时，自身也不抛
template<typename T>
void swap(T& a, T& b) noexcept(noexcept(T(std::move(a)))) {
    T tmp = std::move(a);
    a = std::move(b);
    b = std::move(tmp);
}
```

---

## 二、违约后果

> [!danger] 违反 noexcept 会直接终止程序
> 如果声明了 `noexcept` 的函数内部抛出了异常，**不会**传播给调用方，而是立即调用 `std::terminate()`，程序终止。
>
> ```cpp
> void bad() noexcept {
>     throw std::runtime_error("oops");  // 直接 terminate，不传播
> }
> ```

这与旧的 `throw()` 规格不同：旧规格在违约时调用 `std::unexpected()`，而 `noexcept` 直接终止。

---

## 三、与移动语义的关系

这是 `noexcept` **最重要的实践意义**。

### 3.1 为什么容器需要 noexcept 保证

`std::vector` 扩容时需要将旧元素迁移到新内存。它面临两个选择：

| 操作 | 异常安全性 | 结论 |
|------|-----------|------|
| **拷贝** | ✅ 旧数组完好，可回滚 | 安全但慢 |
| **移动** | ❌ 旧对象被掏空，无法回滚 | 快但若抛异常则数据丢失 |

因此，`std::vector` 的策略是：

```cpp
// 伪代码：vector 扩容时的决策
if (std::is_nothrow_move_constructible<T>::value) {
    // 移动：O(1)
} else {
    // 拷贝：O(n)，但异常安全
}
```

### 3.2 实际影响

```cpp
class Buffer {
    int* data_;
public:
    // ✅ 有 noexcept：vector 扩容时使用移动，O(1)
    Buffer(Buffer&& other) noexcept
        : data_(other.data_) { other.data_ = nullptr; }

    // ⚠️ 无 noexcept：vector 扩容时退回拷贝，O(n)
    Buffer(Buffer&& other)
        : data_(other.data_) { other.data_ = nullptr; }
};

std::vector<Buffer> v;
v.push_back(buf);  // 有 noexcept → 移动；无 noexcept → 拷贝
```

> [!warning] 移动构造和移动赋值必须声明 noexcept
> 否则标准库容器会放弃使用移动操作，移动语义的性能优势完全失效。

---

## 四、析构函数的隐式 noexcept

> [!note] 析构函数默认是 noexcept
> C++11 起，析构函数（包括编译器生成的）**默认隐式带有 `noexcept`**，无需手写。
>
> ```cpp
> class Foo {
> public:
>     ~Foo();           // 隐式 noexcept(true)
>     ~Foo() noexcept;  // 显式写法，与上面等价
> };
> ```
>
> 在析构函数中抛出异常本身就是危险行为（可能在栈展开期间二次抛异常导致 `terminate`），因此这个默认值是合理的。

---

## 五、何时使用 noexcept

### 应该加的场景

```cpp
// ✅ 移动构造 / 移动赋值
MyClass(MyClass&&) noexcept;
MyClass& operator=(MyClass&&) noexcept;

// ✅ swap 函数
void swap(MyClass& other) noexcept;

// ✅ 确实不会抛异常的简单操作
int size() const noexcept { return size_; }
bool empty() const noexcept { return size_ == 0; }
```

### 不应该随意加的场景

```cpp
// ❌ 内部调用了可能抛异常的函数，却声明 noexcept
void process() noexcept {
    auto p = std::make_unique<Widget>();  // 可能抛 bad_alloc！
    // ...若抛异常，直接 terminate，调用方毫无察觉
}
```

> [!tip] 原则
> `noexcept` 是一种**契约**，不是性能魔法。只在你能真正保证不抛异常时才使用，否则会将异常变成无法捕获的程序终止。

---

## 六、与旧式异常规格对比

| 特性 | `noexcept` (C++11) | `throw()` (C++03，已移除) |
|------|-------------------|--------------------------|
| 违约行为 | `std::terminate()` | `std::unexpected()` |
| 编译器优化 | ✅ 可省略展开表 | ❌ 仍需维护展开表 |
| 标准状态 | 现行标准 | C++17 已移除 |
| 运算符形式 | `noexcept(expr)` | 无 |

---

## 参考

- [[C++ 值类别与移动语义]] — noexcept 与移动语义的关系
- [[C++ 对象生存期与 RAII]] — 析构函数与异常安全
