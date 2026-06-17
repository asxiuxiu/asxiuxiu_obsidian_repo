---
title: 结构体与类：成员、访问控制与 this 指针
date: 2026-06-17
tags:
  - C++
  - 类与对象
  - 访问控制
  - this 指针
aliases:
  - struct 与 class 的差异
  - this 指针的本质
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 结构体与类：成员、访问控制与 this 指针

> [!info] 一句话概括
> `class` 和 `struct` 在 C++ 中几乎是同一个东西，区别只在于**默认访问权限**；真正让类变得强大的是**访问控制**带来的封装能力，而 `this` 指针则是编译器悄悄帮你传递的"当前对象地址"，让成员函数知道自己在操作哪个实例。

---

## 问题 0：如果我只是把数据打包，用 C 风格的 `struct` 就够了，为什么 C++ 还要引入 `class`？

在引擎里，我们经常需要把一组相关的数据放在一起。比如描述一个三维向量：

```cpp
// flags: -O0 -g
#include <iostream>

struct Vec3 {
    float x;
    float y;
    float z;
};

int main() {
    Vec3 v = {1.0f, 2.0f, 3.0f};
    std::cout << v.x << ", " << v.y << ", " << v.z << "\n";
    return 0;
}
```

这种写法在 C 和 C++ 里都能跑，成员完全公开，谁都能读写。一开始这当然没问题——简单、直观、零开销。

但问题会随着项目规模迅速浮现：

- 任何代码都可以把 `v.x` 改成非法值，比如 `NaN` 或者一个巨大到离谱的数；
- 你根本无法保证"这个 `Vec3` 已经被归一化"这样的不变量；
- 多人协作时，总有人直接访问内部状态，导致你改一个字段就要改几十个文件。

换句话说，**C 风格结构体把内部细节完全暴露在外**，当数据需要维护规则、需要跟函数行为绑定时，它就开始碍事了。

这就引出了下一个问题：C++ 的 `class` 到底加了什么？

---

## 问题 1：`class` 和 `struct` 在语法上到底有什么不同？访问控制又解决了什么？

在 C++ 里，`class` 和 `struct` 唯一的语法差异是**默认访问权限**：

- `struct` 的成员默认是 `public`；
- `class` 的成员默认是 `private`。

其他所有特性——成员变量、成员函数、继承、虚函数、构造函数——两者完全等价。

下面两段代码在语义上是等价的：

```cpp
// flags: -O0 -g
#include <iostream>

struct Vec3Struct {
// 默认 public
    float x, y, z;
};

class Vec3Class {
// 默认 private
    float x, y, z;
public:
    void set(float ix, float iy, float iz) {
        x = ix; y = iy; z = iz;
    }
    void print() const {
        std::cout << x << ", " << y << ", " << z << "\n";
    }
};

int main() {
    Vec3Struct vs;
    vs.x = 1.0f;  // OK，struct 默认公开

    Vec3Class vc;
    // vc.x = 1.0f;  // 编译错误：x 是 private
    vc.set(1.0f, 2.0f, 3.0f);
    vc.print();
    return 0;
}
```

`private` 意味着：**类外部的代码不能直接碰这些成员，只能通过类提供的公开接口来访问**。这就把"数据怎么存"和"数据怎么用"分离开了。

带来的好处很明显：

- 你可以在 `set()` 里加校验，拒绝非法输入；
- 你可以把内部表示从三个 `float` 改成 `float[3]`，只要接口不变，外部代码无需修改；
- 你可以保证某些不变量始终成立。

这就是**封装**——不是"把数据藏起来不让人看"，而是"把实现细节和对外承诺分开，让你以后能安全地改实现"。

但封装也带来了一个实现层面的问题：成员函数怎么知道它正在操作的是哪个对象？

---

## 问题 2：成员函数没有显式传入对象参数，它是怎么找到 `x`、`y`、`z` 的？

答案藏在 `this` 指针里。

`this` 是一个指向当前对象的指针。编译器在调用成员函数时，会**悄悄地把对象地址作为第一个参数传进去**。你写在类里的成员函数：

```cpp
void set(float ix, float iy, float iz) {
    x = ix; y = iy; z = iz;
}
```

在编译器眼里大概长这样（伪代码）：

```cpp
void set(Vec3Class* this, float ix, float iy, float iz) {
    this->x = ix;
    this->y = iy;
    this->z = iz;
}
```

你可以在成员函数里显式写 `this->x`，也可以省略——编译器会自动补上。

```cpp
// flags: -O0 -g
#include <iostream>

class Counter {
    int value = 0;
public:
    void increment() {
        ++this->value;   // 显式写法
    }
    int get() const {
        return value;    // 编译器等价于 return this->value;
    }
};

int main() {
    Counter a, b;
    a.increment();
    a.increment();
    b.increment();
    std::cout << "a: " << a.get() << "\n";  // 2
    std::cout << "b: " << b.get() << "\n";  // 1
    return 0;
}
```

同一个成员函数 `increment()` 被 `a` 和 `b` 分别调用，却操作了不同的 `value`，靠的就是 `this` 指向不同的对象。

`this` 的存在也说明了一个重要事实：**成员函数并不是"属于对象"的代码**。代码只有一份，存在代码段里；对象里只存数据。调用时通过 `this` 把代码和数据绑定在一起。

---

## 问题 3：`struct` 和 `class` 到底该用哪个？访问控制有没有被滥用的时候？

很多人会纠结："既然两者功能一样，那我到底该用 `struct` 还是 `class`？"

约定大于语法。C++ 社区普遍的惯例是：

- **用 `struct` 表示"纯数据聚合"**：成员公开，没有复杂的不变量需要维护，比如 `Vec3`、`Vertex`、`Config`；
- **用 `class` 表示"封装了行为的对象"**：有内部状态需要保护，有公开接口和实现细节之分，比如 `FileHandle`、`RenderContext`。

这个约定不是强制的，但能让读代码的人一眼看出你的设计意图。

不过访问控制也可能被误用。常见的陷阱有两个：

**陷阱 1：所有成员都 `private`，但给每个成员都写 `getXxx` / `setXxx`**

如果 `setXxx` 里没有任何校验、没有任何副作用，那它跟直接公开成员没有本质区别，只是让代码变啰嗦了。访问控制的价值在于"封装行为"，而不在于"形式上藏起来"。

**陷阱 2：把需要外部协作才能构造的对象所有成员都设为 `private`**

有时候你确实需要外部代码帮你初始化对象。完全封闭会导致不得不写一堆无意义的访问函数。更好的做法通常是控制构造函数和写权限，而不是一味地 `private`。

```cpp
// flags: -O0 -g
#include <iostream>

// 纯数据：用 struct，公开成员
struct Vertex {
    float x, y, z;
    float u, v;
};

// 有内部不变量：用 class，封装实现
class Rectangle {
    float width_;
    float height_;
public:
    Rectangle(float w, float h) : width_(w), height_(h) {}
    float area() const { return width_ * height_; }
    // 只允许通过接口修改，保证 width/height 非负
    void setWidth(float w) { width_ = w > 0 ? w : 0; }
    void setHeight(float h) { height_ = h > 0 ? h : 0; }
};

int main() {
    Vertex v = {1.0f, 2.0f, 3.0f, 0.0f, 0.0f};  // 自由初始化
    Rectangle r(3.0f, 4.0f);
    std::cout << "area = " << r.area() << "\n";  // 12
    return 0;
}
```

---

## 总结

- `struct` 和 `class` 在 C++ 中的语法差异只有一条：默认访问权限不同。功能上完全等价。
- 访问控制（`public` / `private`）让类可以把实现细节藏起来，只暴露稳定的接口，这是封装的基础。
- `this` 指针是编译器隐式传递的当前对象地址，成员函数通过它找到自己要操作的数据。
- 选择 `struct` 还是 `class` 应该遵循约定：`struct` 表示公开的数据聚合，`class` 表示需要封装行为的对象。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | POD 类型与复杂类的分界 | `Vec3`、`Color` 等数学类型通常用 `struct` 公开成员，保证与 SIMD 和 GPU 数据布局兼容；`RenderDevice`、`ResourceHandle` 等用 `class` 封装内部状态 |
| **UE** | `USTRUCT` 与 `UCLASS` 的语义区分 | `USTRUCT` 多用于纯数据传输（如网络序列化、蓝图数据），默认成员公开；`UCLASS` 用于有行为的对象，配合反射系统管理属性和接口 |

> [!note] 关键取舍
> 引擎中的数学类型之所以常用 `struct` 而不是 `class`，不是因为性能差异，而是因为它们通常是"内存布局敏感"的纯数据：需要和着色器常量缓冲区、GPU 顶点格式、SIMD 寄存器一一对应。把这些成员设为 `public` 反而是一种设计意图的表达——"我就是一块透明的数据"。

---

> 相关笔记：
> - [[Notes/C++编程/类与对象入门/构造函数与析构函数基础|构造函数与析构函数基础]]
> - [[Notes/C++编程/类与对象入门/类的作用域与友元|类的作用域与友元]]
> - [[Notes/C++编程/对象内存模型与底层机制/对象内存布局：从 struct 到 class|对象内存布局：从 struct 到 class]]
