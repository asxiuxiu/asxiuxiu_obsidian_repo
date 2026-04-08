---
title: C++ explicit 关键字
date: 2026-03-20
tags:
  - C++
  - 类型转换
  - 构造函数
aliases:
  - explicit
---

> [[索引|← 返回 C++编程索引]]

# C++ explicit 关键字

> [!info] 概述
> `explicit` 用于修饰**构造函数**或**转换运算符**，禁止编译器将其用于隐式类型转换，要求调用方必须明确写出转换意图。

---

## 一、问题起源：隐式转换的隐患

C++ 允许编译器使用单参数构造函数进行隐式转换：

```cpp
class MyString {
public:
    MyString(int size);  // 分配 size 个字符的缓冲区
};

void print(MyString s) { /* ... */ }

print(42);  // ⚠️ 编译器隐式调用 MyString(42)，意图模糊
```

`print(42)` 看起来在打印整数，实则构造了一个长度为 42 的字符串对象——这种**静默转换**往往是 bug 的来源。

---

## 二、explicit 的作用

加上 `explicit` 后，隐式转换被禁止：

```cpp
class MyString {
public:
    explicit MyString(int size);
};

print(42);               // ❌ 编译错误：不允许隐式转换
print(MyString(42));     // ✅ 显式构造，意图明确
```

---

## 三、适用范围

### 3.1 单参数构造函数（最常见）

```cpp
class Buffer {
public:
    explicit Buffer(size_t capacity);  // 防止 42 → Buffer 的隐式转换
};

class FileHandle {
public:
    explicit FileHandle(const char* path);  // 打开文件有副作用，不应隐式发生
};
```

### 3.2 多参数构造函数（C++11 起，配合大括号初始化）

C++11 引入了列表初始化，多参数构造函数也可能被隐式调用：

```cpp
class Point {
public:
    explicit Point(int x, int y);
};

void move(Point p) { /* ... */ }

move({1, 2});        // ❌ 加了 explicit 后禁止
move(Point{1, 2});   // ✅ 显式构造
```

### 3.3 转换运算符（C++11 起）

`explicit` 同样可用于 `operator T()`，防止对象被隐式转换为其他类型：

```cpp
class SmartBool {
public:
    explicit operator bool() const { return value_; }
private:
    bool value_;
};

SmartBool b;
if (b) { }         // ✅ if 条件中的隐式 bool 转换被特殊豁免
int x = b;         // ❌ 编译错误：不允许隐式转为 int
bool y = (bool)b;  // ✅ 显式转换
```

> [!note] `explicit operator bool` 的常见用法
> 标准库中的 `std::unique_ptr`、`std::shared_ptr`、`std::optional` 都使用了 `explicit operator bool`，使得 `if (ptr)` 可以工作，但 `int x = ptr` 会报错。

---

## 四、何时加、何时不加

| 场景 | 建议 | 原因 |
|------|------|------|
| RAII 资源管理类 | **加** | 构造有副作用（打开文件、分配内存等） |
| 数值/尺寸参数 | **加** | `Buffer(42)` 的意图不如 `Buffer(size_t)` 隐式调用清晰 |
| 包装/视图类 | 通常**不加** | `std::string_view(str)`、`std::span` 的隐式转换是其设计意图 |
| 类型语义等价 | 通常**不加** | `Celsius(36.6)` 中 `double → Celsius` 转换完全自然 |

> [!tip] 默认策略（C++ Core Guidelines）
> **单参数构造函数默认加 `explicit`**，只有在明确需要隐式转换时才去掉。
> 这与 `const` 的使用原则类似：默认加，有充分理由时才省略。

---

## 五、典型示例对比

```cpp
// ❌ 不加 explicit，可能导致意外行为
class Widget {
public:
    Widget(int id);
};

std::vector<Widget> widgets;
widgets.push_back(42);  // 悄悄构造了 Widget(42)，可能是笔误

// ✅ 加 explicit，编译器帮你抓 bug
class Widget {
public:
    explicit Widget(int id);
};

widgets.push_back(42);           // ❌ 编译错误，及时发现
widgets.push_back(Widget(42));   // ✅ 明确意图
```

---

## 相关笔记

- [[C++类型转换]]
- [[C++ 对象生存期与 RAII]]
