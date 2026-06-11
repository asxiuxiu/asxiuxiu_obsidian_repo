---
title: Rule of Three, Five and Zero
date: 2026-06-11
tags:
  - C++
  - 拷贝控制
  - 移动语义
  - 设计原则
aliases:
  - Rule of Three
  - Rule of Five
  - Rule of Zero
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# Rule of Three, Five and Zero

> [!info] 一句话概括
> 当一个类管理着外部资源（内存、文件、GPU 句柄等），拷贝构造、拷贝赋值和析构三者必须**统一处理**——这叫 Rule of Three；C++11 加入移动语义后扩展到五个；而最佳实践是让类的成员替你管理资源，自己一个都不写——这叫 Rule of Zero。

---

## 问题 0：为什么管理资源的类，拷贝控制函数总是「要么全写，要么全删」？

假设你写了一个管理动态数组的类，只写了析构函数来释放内存：

```cpp
// flags: -O0 -g
#include <iostream>
#include <cstring>

class PartialArray {
    int* data_;
    size_t size_;
public:
    PartialArray(size_t n) : size_(n), data_(new int[n]{}) {}

    ~PartialArray() {  // 只写了析构
        delete[] data_;
    }
    // 拷贝构造和拷贝赋值由编译器自动生成
};

void sink(PartialArray arr) {  // 传值：触发拷贝构造
    std::cout << "sink received array\n";
}  // arr 析构 → delete[] data_

int main() {
    PartialArray a(10);
    sink(a);  // a 被拷贝到 arr
    // a 析构 → delete[] data_ （第二次释放同一块内存！）
    return 0;
}
```

这段代码会崩溃。你只关心「释放资源」而写了析构，但编译器**自动生成**的拷贝构造执行的是浅拷贝——`arr` 和 `a` 的 `data_` 指向同一块内存。`sink` 结束时 `arr` 先析构释放了内存，`main` 结束时 `a` 又析构释放了一次 → **double-free**。

反过来，如果你只写了拷贝构造做深拷贝，却忘了写析构：

```cpp
class LeakyArray {
    int* data_;
    size_t size_;
public:
    LeakyArray(size_t n) : size_(n), data_(new int[n]{}) {}

    LeakyArray(const LeakyArray& other)
        : size_(other.size_)
        , data_(new int[other.size_])  // 深拷贝，分配新内存
    {
        std::memcpy(data_, other.data_, size_ * sizeof(int));
    }
    // 忘了写析构！编译器生成空析构 → 内存泄漏
};
```

每次 `LeakyArray` 对象销毁时，编译器生成的默认析构函数什么都不做，`new int[]` 分配的内存永远没人释放。

这些案例揭示了一个核心规律：**析构、拷贝构造、拷贝赋值这三个函数，管理的是同一种资源的不同生命周期环节**——
- 析构说「资源怎么销毁」
- 拷贝构造说「资源怎么复制给新对象」
- 拷贝赋值说「资源怎么替换已有对象的旧资源」

三者必须遵循**一致的策略**。如果你自定义了其中一个，却放任编译器自动生成另外两个，几乎必然导致资源管理策略的冲突。

---

## 问题 1：Rule of Three 是什么？

**Rule of Three**：如果一个类需要自定义**析构函数**、**拷贝构造函数**或**拷贝赋值运算符**中的任意一个，它几乎肯定需要同时定义**全部三个**。

这个规则不是 C++ 标准的强制规定，而是三十年来无数内存泄漏和 double-free 事故总结出的**工程铁律**。

看一个符合 Rule of Three 的正确实现：

```cpp
// flags: -O0 -g
#include <iostream>
#include <cstring>

class RuleOfThreeArray {
    int* data_;
    size_t size_;
public:
    explicit RuleOfThreeArray(size_t n) : size_(n), data_(new int[n]{}) {}

    // 1. 析构：释放资源
    ~RuleOfThreeArray() {
        delete[] data_;
    }

    // 2. 拷贝构造：深拷贝资源
    RuleOfThreeArray(const RuleOfThreeArray& other)
        : size_(other.size_)
        , data_(new int[other.size_])
    {
        std::memcpy(data_, other.data_, size_ * sizeof(int));
    }

    // 3. 拷贝赋值：释放旧资源，深拷贝新资源
    RuleOfThreeArray& operator=(const RuleOfThreeArray& other) {
        if (this == &other) return *this;
        delete[] data_;
        size_ = other.size_;
        data_ = new int[size_];
        std::memcpy(data_, other.data_, size_ * sizeof(int));
        return *this;
    }
};

int main() {
    RuleOfThreeArray a(10);
    RuleOfThreeArray b = a;  // 深拷贝：b 有自己独立的内存
    b = a;                   // 安全赋值：先释放 b 的旧内存，再深拷贝
    return 0;
}
```

三个函数都遵循同一个资源管理策略：「每个对象独立拥有一块堆内存」。拷贝时分配新内存并复制内容，析构时释放自己的内存。策略一致，就不会有 double-free 或泄漏。

> [!tip] 自检口诀
> 看到类里有 `new`/`delete`、`fopen`/`fclose`、GPU 资源创建/销毁等**成对出现**的资源操作时，立刻问自己：这个类的拷贝构造、拷贝赋值、析构，三个都写了吗？

---

## 问题 2：C++11 引入移动语义后，为什么变成了 Rule of Five？

C++11 加入了**移动语义**——允许资源从一个对象「转移」到另一个对象，而不是复制。这让「资源所有权转移」成为可能，比如 `std::unique_ptr` 就是把资源从一个指针转移到另一个指针，原指针置空。

对于管理资源的类，移动语义意味着两个新函数：
- **移动构造函数**：`ClassName(ClassName&& other) noexcept`
- **移动赋值运算符**：`ClassName& operator=(ClassName&& other) noexcept`

**Rule of Five**：如果类需要自定义 Rule of Three 中的任何一个，那么在 C++11 环境下，它通常也需要定义**移动构造和移动赋值**。

为什么？因为如果一个类管理着稀缺资源（堆内存、文件句柄、GPU 纹理），**移动比拷贝廉价得多**——拷贝要分配新内存并复制所有字节，移动只是转移几个指针和整数的值。

如果不声明移动操作，C++11 编译器会尝试自动生成，但有一个大坑：**只要你声明了拷贝构造、拷贝赋值或析构中的任意一个，编译器就不会再自动生成移动操作**。这意味着你的类会被「退化」成只能拷贝不能移动，所有本可以转移的场景（如函数返回值、容器 `push_back`）都会执行昂贵的深拷贝。

```cpp
// flags: -O0 -g
#include <iostream>
#include <vector>

class NoMove {
    int* data_;
public:
    explicit NoMove(size_t n) : data_(new int[n]) {}
    ~NoMove() { delete[] data_; }
    NoMove(const NoMove& other) : data_(new int[100]) {
        std::cout << "copy ctor (expensive!)\n";
    }
    // 没有移动构造！因为声明了析构和拷贝构造，编译器不生成默认移动构造
};

class HasMove {
    int* data_;
public:
    explicit HasMove(size_t n) : data_(new int[n]) {}
    ~HasMove() { delete[] data_; }
    HasMove(const HasMove& other) : data_(new int[100]) {
        std::cout << "copy ctor\n";
    }
    HasMove(HasMove&& other) noexcept : data_(other.data_) {
        other.data_ = nullptr;
        std::cout << "move ctor (cheap!)\n";
    }
};

int main() {
    std::vector<NoMove> v1;
    v1.reserve(2);
    NoMove a(100);
    v1.push_back(a);   // 只能拷贝，昂贵

    std::vector<HasMove> v2;
    v2.reserve(2);
    HasMove b(100);
    v2.push_back(std::move(b));  // 移动，廉价
    return 0;
}
```

> [!warning] C++11 的关键变化
> **声明析构、拷贝构造或拷贝赋值中的任意一个 = 编译器不再生成默认移动操作。** 这是 C++11 设计上的一个「惩罚性」规则，目的是保护旧代码的兼容性，但也导致很多手写 Rule of Three 的类在 C++11 下默默失去移动能力。

---

## 问题 3：Rule of Zero 是什么？——现代 C++ 的最佳实践

Rule of Three/Five 告诉我们「资源管理类要手写拷贝控制」。但 Rule of Zero 提出了一个更优雅的思路：**让你的类不直接管理资源，而是由它的成员来管理。这样你就不需要写任何拷贝控制函数。**

```cpp
// flags: -O0 -g
#include <iostream>
#include <vector>
#include <memory>
#include <string>

class RuleOfZeroPlayer {
    std::string name_;                    // std::string 自己管理内存
    std::vector<int> inventory_;          // std::vector 自己管理内存
    std::unique_ptr<class SkillTree> skills_;  // unique_ptr 管理唯一所有权
public:
    explicit RuleOfZeroPlayer(std::string name)
        : name_(std::move(name))
    {}
    // 没有析构、没有拷贝构造、没有拷贝赋值、没有移动、没有移动赋值
    // 编译器自动生成的版本全部正确！
};

int main() {
    RuleOfZeroPlayer p1("Alice");
    RuleOfZeroPlayer p2 = p1;  // ❌ 编译错误：unique_ptr 不可拷贝
    // 但这是因为 unique_ptr 的设计意图，不是 bug
    RuleOfZeroPlayer p3("Bob");
    p3 = p1;  // ❌ 同样编译错误
    return 0;
}
```

等一下，`RuleOfZeroPlayer` 甚至**不能拷贝**——因为 `std::unique_ptr` 删除了拷贝操作，编译器也就不会为 `RuleOfZeroPlayer` 生成拷贝操作。这不是 bug，而是**正确的设计**：`unique_ptr` 表达的是「独占所有权」，本就不该被拷贝。

如果你想让 `RuleOfZeroPlayer` **可拷贝**，只需要把 `unique_ptr` 换成 `std::shared_ptr`，或者自己实现深拷贝逻辑。关键在于：**资源管理的责任在成员层面解决，类的作者不需要操心拷贝控制。**

Rule of Zero 的核心哲学：
- **把资源封装在 RAII 类中**（如智能指针、标准容器、自定义资源句柄）
- **你的业务类只组合这些 RAII 成员**
- **编译器自动生成的拷贝控制函数就是正确的**

> [!note] 为什么 Rule of Zero 比 Rule of Five 更好？
> 手写拷贝控制函数容易出错：忘记自赋值检查、异常安全问题、移动后源对象状态处理……而把资源管理交给经过充分测试的标准库组件或专用 RAII 类，能消除整类 bug。在引擎开发中，这意味着你只需写一次「GPU 纹理句柄」的 RAII 类，然后所有使用它的业务类都自动获得正确的生命周期管理。

---

## 问题 4：在引擎中，怎么表达「不可拷贝」和「只可移动」？

### 显式删除拷贝操作

对于管理不可复制资源（GPU 句柄、文件描述符、线程句柄）的类，最清晰的做法是显式删除拷贝操作：

```cpp
class TextureHandle {
    unsigned int id_;
public:
    explicit TextureHandle(const char* path) : id_(loadTexture(path)) {}
    ~TextureHandle() { glDeleteTextures(1, &id_); }

    TextureHandle(const TextureHandle&) = delete;            // 不可拷贝
    TextureHandle& operator=(const TextureHandle&) = delete; // 不可拷贝

    TextureHandle(TextureHandle&& other) noexcept : id_(other.id_) {
        other.id_ = 0;  // 移动后源对象置空
    }
    TextureHandle& operator=(TextureHandle&& other) noexcept {
        if (this != &other) {
            glDeleteTextures(1, &id_);  // 释放旧资源
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }
};
```

这是一个**移动 Only（Move-Only）**类型：不能拷贝，但可以转移所有权。`std::unique_ptr`、`std::thread`、`std::fstream` 都是这种设计。

### NonCopyable / NonMovable 基类模式

在大型项目中，为了避免每个类都重复写 `= delete`，可以使用基类 mixin：

```cpp
struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

struct NonMovable : NonCopyable {
    NonMovable() = default;
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;
};

// 使用
class TextureHandle : NonCopyable {
    // 自动继承 deleted 拷贝操作
    // 只需定义移动操作和析构
};
```

UE 和许多引擎库都使用了类似模式，确保资源句柄类的拷贝策略统一且不可误改。

---

## 总结

| 规则 | 含义 | 适用场景 |
|------|------|---------|
| **Rule of Three** | 自定义析构/拷贝构造/拷贝赋值中的任意一个，就需要同时定义三个 | C++98/03 代码；管理资源的类 |
| **Rule of Five** | 在 Rule of Three 基础上，加上移动构造和移动赋值 | C++11 及以后；资源可转移的类 |
| **Rule of Zero** | 让类的成员（智能指针、容器、RAII 句柄）管理资源，自己不写任何拷贝控制函数 | **现代 C++ 首选**；组合优于手写 |

- **Rule of Three/Five 的核心逻辑**：析构、拷贝、移动操作管理的是同一种资源，策略必须一致。只自定义其中一个，放任编译器生成其他，必然导致资源管理冲突。
- **Rule of Zero 的核心逻辑**：把资源管理下沉到专门的 RAII 类中，业务类只做组合。这样编译器自动生成的拷贝控制就是正确的，避免了手写错误。
- 在 C++11 中，声明析构/拷贝构造/拷贝赋值中的任意一个会**抑制编译器自动生成移动操作**，导致类退化为只能拷贝——这是手写 Rule of Three 的类迁移到 C++11 时的常见陷阱。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 资源句柄的设计策略 | `Texture`、`Buffer`、`PipelineState` 等 GPU 资源句柄均为 Move-Only 类型（继承 `NonCopyable`），禁止拷贝以避免昂贵的 GPU 资源重复创建；移动操作转移 GPU 句柄 ID 并将源对象置空，保证析构时不会重复释放 |
| **UE** | `UObject` 与 `TUniquePtr` 的所有权模型 | `UObject` 体系完全禁止值拷贝（Rule of Zero 的极端形式：全部交给 GC）；`TUniquePtr` 是 Move-Only 类型，用于编辑器工具中的非 GC 资源管理；`TSharedPtr`/`TWeakObjectPtr` 实现引用计数，替代手动拷贝控制 |

> [!note] 关键取舍
> SelfGameEngine 采用「Rule of Zero + 专用 RAII 句柄」的策略：底层资源句柄（`TextureHandle`、`FileHandle`）遵循 Rule of Five 精确控制生命周期，而上层的游戏对象、组件、系统类遵循 Rule of Zero——它们只组合这些 RAII 句柄，不需要写任何析构或拷贝函数。
> UE 的 `UObject` 则走了另一条路：取消值语义，全部对象由 GC 托管，拷贝只能通过反射序列化完成。这让编辑器获得了极大的灵活性（复制粘贴任意对象），但代价是运行时性能和内存布局的可预测性。

---

> 相关笔记：
> - [[Notes/C++编程/构造、析构与拷贝控制/拷贝构造函数与拷贝赋值运算符|拷贝构造函数与拷贝赋值运算符]] — 深拷贝与浅拷贝的实现细节
> - [[Notes/C++编程/构造、析构与拷贝控制/拷贝并交换惯用法|拷贝并交换惯用法]] — 如何用异常安全的方式实现拷贝赋值
> - [[Notes/C++编程/值类别与引用语义/值类别与移动语义|值类别与移动语义]] — 移动语义的本质与右值引用
> - [[Notes/C++编程/资源管理与对象生存期/对象生存期与 RAII|RAII]] — 资源获取即初始化
