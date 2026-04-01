---
title: C++ decltype 关键字
date: 2026-03-20
tags:
  - C++
  - 类型推导
  - 模板
aliases:
  - decltype
---

# C++ decltype 关键字

> [!info] 概述
> `decltype` 是 C++11 引入的**编译期类型推导**关键字。它在不求值表达式的前提下，推导出表达式的类型，常用于泛型编程、模板和返回类型推导。

---

## 一、基本语法

```cpp
decltype(expression)  // 得到 expression 的类型
```

`decltype` 不会执行表达式，只在编译期分析类型：

```cpp
int x = 42;
double y = 3.14;

decltype(x) a = 0;      // a 的类型是 int
decltype(y) b = 0.0;    // b 的类型是 double
decltype(x + y) c;      // c 的类型是 double（整数+浮点的结果类型）
```

---

## 二、推导规则

`decltype` 的推导结果取决于表达式的种类：

### 2.1 变量名（无括号）

直接推导声明时的类型，**保留 `const`、`&` 等限定符**：

```cpp
int x = 0;
const int cx = 0;
int& rx = x;
const int& crx = x;

decltype(x)   → int
decltype(cx)  → const int
decltype(rx)  → int&
decltype(crx) → const int&
```

### 2.2 加括号的表达式

`(x)` 是一个左值表达式，结果变为**引用类型**：

```cpp
int x = 0;

decltype(x)   → int      // 变量名，得到声明类型
decltype((x)) → int&     // 加括号，变成左值表达式，得到引用
```

> [!warning] 常见陷阱
> `decltype(x)` 和 `decltype((x))` 结果不同！加括号会改变推导结果。
> ```cpp
> decltype(auto) f1() { int x = 0; return x; }    // 返回 int
> decltype(auto) f2() { int x = 0; return (x); }  // 返回 int&（悬空引用！）
> ```

### 2.3 函数调用表达式

推导为函数的**返回类型**：

```cpp
int foo();
int& bar();
const double baz();

decltype(foo())  → int
decltype(bar())  → int&
decltype(baz())  → const double
```

### 2.4 总结规则

| 表达式种类 | 推导结果 | 示例 |
|-----------|---------|------|
| 变量名（无括号） | 声明类型（保留限定符） | `decltype(x)` → `int` |
| 左值表达式 | `T&` | `decltype((x))` → `int&` |
| 将亡值（xvalue） | `T&&` | `decltype(std::move(x))` → `int&&` |
| 纯右值（prvalue） | `T` | `decltype(x + 1)` → `int` |

---

## 三、`decltype` vs `auto`

两者都做类型推导，但规则不同：

```cpp
int x = 0;
const int& cr = x;

auto a = cr;         // a 的类型是 int（auto 忽略顶层 const 和引用）
decltype(cr) b = x; // b 的类型是 const int&（decltype 完整保留）
```

> [!note] 核心区别
> - `auto`：推导时**去掉** `const`、`&`（顶层），类似模板参数推导
> - `decltype`：**完整保留**表达式的类型，包括 `const`、`&`

---

## 四、常见用途

### 4.1 泛型函数的返回类型（C++11）

C++11 中，返回类型依赖参数类型时，用尾置返回类型：

```cpp
// 返回类型取决于 a + b 的结果
template<typename T, typename U>
auto add(T a, U b) -> decltype(a + b) {
    return a + b;
}
```

C++14 起可以直接用 `auto` 推导，无需 `decltype`：

```cpp
template<typename T, typename U>
auto add(T a, U b) {  // C++14：直接推导
    return a + b;
}
```

### 4.2 `decltype(auto)`：完整保留返回类型

`auto` 推导会丢弃引用和 `const`，`decltype(auto)` 则完整保留：

```cpp
int x = 0;
int& getRef() { return x; }

auto f1()          { return getRef(); }  // 返回 int（引用被丢弃）
decltype(auto) f2(){ return getRef(); }  // 返回 int&（完整保留）
```

转发函数中特别有用：

```cpp
template<typename F, typename... Args>
decltype(auto) forwardCall(F&& f, Args&&... args) {
    return std::forward<F>(f)(std::forward<Args>(args)...);
    // 完整保留 f 的返回类型（值/引用/const 都不丢失）
}
```

### 4.3 获取匿名类型（如 lambda）

Lambda 的类型是匿名的，无法直接写出，用 `decltype` 推导：

```cpp
auto deleter = [](FILE* f) { if (f) fclose(f); };

// 无法写出 lambda 的真实类型，用 decltype 推导
std::unique_ptr<FILE, decltype(deleter)> file(
    fopen("test.txt", "r"), deleter
);
```

详见 [[C++ 对象生存期与 RAII#4.4 自定义删除器]]。

### 4.4 配合 `std::declval` 在不构造对象的情况下推导类型

`std::declval<T>()` 生成一个 `T` 类型的"假"对象（不实际构造），配合 `decltype` 推导成员函数返回类型：

```cpp
#include <utility>

struct Foo {
    int bar() const;
};

// 推导 Foo::bar() 的返回类型，无需构造 Foo 对象
using ReturnType = decltype(std::declval<Foo>().bar());  // int
```

---

## 五、实际场景速查

| 场景 | 写法 |
|------|------|
| 与变量类型保持一致 | `decltype(var) newVar = ...` |
| 推导函数返回类型（C++11） | `auto f() -> decltype(expr)` |
| 完整转发返回类型（C++14） | `decltype(auto) f() { return ...; }` |
| lambda 作为模板参数 | `unique_ptr<T, decltype(lambda)>` |
| 不构造对象推导成员类型 | `decltype(std::declval<T>().member())` |

---

## 相关笔记

- [[C++ 对象生存期与 RAII]]
- [[C++ explicit 关键字]]
- [[C++ inline 关键字]]
