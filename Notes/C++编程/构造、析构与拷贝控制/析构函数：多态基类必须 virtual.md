---
title: 析构函数：多态基类必须 virtual
date: 2026-06-28
tags:
  - C++
  - 析构函数
  - virtual
  - 多态
  - 异常安全
aliases:
  - 虚析构函数
  - 多态基类析构
  - 析构函数与异常
---

> [[Notes/C++编程/索引|← 返回 C++编程索引]]

# 析构函数：多态基类必须 virtual

> [!info] 一句话概括
> 当类被设计为多态基类时，析构函数必须声明为 `virtual`；否则通过基类指针 `delete` 派生类对象会跳过派生类析构，导致资源泄漏和未定义行为。

---

## 问题 0：析构函数不就是「清理资源」吗？为什么还要 `virtual`？

假设我正在写一个资源管理类。`Texture` 是基类，`DXTexture` 和 `VKTexture` 是具体实现。为了统一接口，我想让调用方只关心 `Texture*`：

```cpp
// flags: -O0 -g
#include <iostream>

class Texture {
public:
    Texture() { std::cout << "Texture 构造\n"; }
    ~Texture() { std::cout << "Texture 析构\n"; }
};

class DXTexture : public Texture {
    int* dxHandle;
public:
    DXTexture() : dxHandle(new int[1024]) {
        std::cout << "DXTexture 构造\n";
    }
    ~DXTexture() {
        delete[] dxHandle;
        std::cout << "DXTexture 析构\n";
    }
};

int main() {
    Texture* t = new DXTexture();
    delete t;
    return 0;
}
```

运行这段代码，输出只有：

```
Texture 构造
DXTexture 构造
Texture 析构
```

`DXTexture` 的析构函数没有执行！这意味着 `dxHandle` 指向的 1024 个 `int` 被泄漏了。问题出在 `delete t;` 这里：`t` 是 `Texture*` 类型，而 `Texture` 的析构函数不是 `virtual`，所以编译器直接按静态类型调用了 `Texture::~Texture()`，完全没有去查派生类的析构函数。

这就是多态基类最常见的陷阱之一：**基类指针删除派生类对象时，如果析构不是 virtual，只会部分析构。** 在引擎里，这种 bug 可能表现为 GPU 句柄泄漏、内存池节点未归还、引用计数不递减，排查起来非常困难。

---

## 问题 1：`virtual` 析构是怎么让派生类析构也被调用的？

把基类析构改成 `virtual` 后，再看同样的代码：

```cpp
// flags: -O0 -g
#include <iostream>

class Texture {
public:
    Texture() { std::cout << "Texture 构造\n"; }
    virtual ~Texture() { std::cout << "Texture 析构\n"; }
};

class DXTexture : public Texture {
    int* dxHandle;
public:
    DXTexture() : dxHandle(new int[1024]) {
        std::cout << "DXTexture 构造\n";
    }
    ~DXTexture() {
        delete[] dxHandle;
        std::cout << "DXTexture 析构\n";
    }
};

int main() {
    Texture* t = new DXTexture();
    delete t;
    return 0;
}
```

输出变成：

```
Texture 构造
DXTexture 构造
DXTexture 析构
Texture 析构
```

这一次，`delete t;` 先调用了 `DXTexture::~DXTexture()`，再自动调用 `Texture::~Texture()`。`dxHandle` 被正确释放。

底层机制和普通的虚函数一样：当类里有 virtual 函数时，对象会多一个 **vptr（虚指针）**，指向该类的 **vtable（虚函数表）**。析构函数也是 vtable 中的一个 slot。`delete` 基类指针时，运行时会通过 vptr 找到实际类型对应的析构函数，而不是按指针的静态类型直接调用。

```cpp
// flags: -O0 -g
#include <iostream>

class Base {
public:
    virtual ~Base() {}
};

class Derived : public Base {
public:
    ~Derived() {}
};

int main() {
    std::cout << "sizeof(Base) = " << sizeof(Base) << '\n';
    std::cout << "sizeof(Derived) = " << sizeof(Derived) << '\n';
    return 0;
}
```

在 64 位环境下，这两个 `sizeof` 通常都是 8——就是一个 vptr 的大小。virtual 析构的代价和普通虚函数一样：每个对象多一个指针，每次析构多两次间接访问。

> 注意：派生类析构执行完后，编译器会自动插入对基类析构的调用。这个调用不是通过 vtable 完成的，而是直接静态调用，避免无限递归。

---

## 问题 2：所有类的析构函数都应该加 `virtual` 吗？

不是。virtual 析构有明确的适用场景：**只有当一个类被设计为多态基类时，才需要 virtual 析构。**

如果一个类不会被继承、或者不打算通过基类指针删除派生类对象，加 virtual 只会带来不必要的开销：每个对象多一个 vptr，破坏与 C 结构体的布局兼容性，也破坏 trivially destructible 的性质。

```cpp
// flags: -O0 -g
#include <iostream>

class Vec3 {
public:
    float x, y, z;
    ~Vec3() = default;  // 不需要 virtual
};

class Texture {
public:
    virtual ~Texture() = default;  // 多态基类，需要 virtual
};

int main() {
    std::cout << "sizeof(Vec3) = " << sizeof(Vec3) << '\n';
    std::cout << "sizeof(Texture) = " << sizeof(Texture) << '\n';
    return 0;
}
```

输出可能是：

```
sizeof(Vec3) = 12
sizeof(Texture) = 8
```

`Vec3` 是 3 个 float，12 字节，没有 vptr。`Texture` 虽然没有成员变量，但有一个 vptr，所以是 8 字节。如果给 `Vec3` 加 virtual 析构，它就不能再被当作纯数据块直接 `memcpy` 或传给 GPU 了。

引擎里有一个常见的折中：如果你希望一个类可以被继承，但又不希望它通过基类指针被 `delete`，可以把析构函数声明为 `protected`。这样派生类仍然可以正常析构，但外部代码无法 `delete Base*`：

```cpp
// flags: -O0 -g
#include <iostream>

class Resource {
protected:
    ~Resource() { std::cout << "Resource 析构\n"; }
};

class Mesh : public Resource {
public:
    ~Mesh() { std::cout << "Mesh 析构\n"; }
};

int main() {
    // Resource* r = new Mesh(); delete r;  // 编译错误：~Resource 是 protected
    Mesh m;  // OK：Mesh 析构时会调用 Resource::~Resource()
    return 0;
}
```

这个模式适合「接口类」或「抽象混入类」：允许继承和组合，但不允许通过基类指针管理生命周期。

---

## 问题 3：析构函数里抛出异常会发生什么？

析构函数有一个特殊约束：**它不应该抛出异常。** 如果析构函数在执行过程中抛出异常，程序会直接调用 `std::terminate()`，而不是继续正常的异常传播。

```cpp
// flags: -O0 -g
#include <iostream>
#include <stdexcept>

class Bomb {
public:
    ~Bomb() {
        std::cout << "Bomb 析构，准备抛异常\n";
        throw std::runtime_error("boom");
    }
};

int main() {
    try {
        Bomb b;
    } catch (...) {
        std::cout << "捕获异常\n";
    }
    return 0;
}
```

运行这段代码，输出 "Bomb 析构，准备抛异常" 后程序会直接终止，不会进入 `catch` 块。

为什么会这样？想象一个更复杂的场景：栈展开过程中，某个对象的析构函数抛出了异常，而另一个异常正在被传播。C++ 无法同时处理两个活跃异常，所以只能调用 `terminate()`。这是语言层面的硬性规定。

在引擎里，这个规则通常被强化为：**析构函数必须是 `noexcept` 的。** 即使引擎禁用了异常，析构函数也不应该调用可能失败的清理操作；如果清理可能失败（比如文件关闭返回错误码），应该把错误记录到日志或设置为「待处理」状态，而不是让析构函数失败。

```cpp
// flags: -O0 -g
#include <iostream>
#include <stdexcept>

class SafeFile {
    bool closeFailed = false;
public:
    ~SafeFile() noexcept {
        // 假设这里关闭文件句柄
        // 如果失败，只记录状态，不抛异常
        closeFailed = false;
        std::cout << "SafeFile 安全关闭\n";
    }
    bool didCloseFail() const { return closeFailed; }
};

int main() {
    SafeFile f;
    return 0;
}
```

---

## 问题 4：纯虚析构是什么？它也需要实现吗？

有时候我们希望一个类是**抽象基类**——不能直接实例化，只能被继承。常见的做法是声明一个纯虚函数：

```cpp
class IRenderTarget {
public:
    virtual void bind() = 0;
    virtual ~IRenderTarget() = default;
};
```

如果没有 `virtual ~IRenderTarget() = default;`，派生类通过 `IRenderTarget*` 删除时仍然会遇到问题。所以即使基类里已经有其他纯虚函数，析构函数也最好声明为 virtual。

一个稍微不同的写法是**纯虚析构**：

```cpp
// flags: -O0 -g
#include <iostream>

class AbstractBase {
public:
    virtual ~AbstractBase() = 0;  // 纯虚析构
};

AbstractBase::~AbstractBase() {
    std::cout << "AbstractBase 析构实现\n";
}

class Derived : public AbstractBase {
public:
    ~Derived() { std::cout << "Derived 析构\n"; }
};

int main() {
    AbstractBase* p = new Derived();
    delete p;
    return 0;
}
```

纯虚析构让类变成抽象类，不能直接创建对象；但它**仍然必须有函数体**，因为派生类析构完成后会静态调用基类析构。如果忘了提供实现，链接时会报错。

这种写法在现代 C++ 中比较少见，因为通常会用 `= default` 的 virtual 析构配合其他纯虚函数。知道纯虚析构的存在，主要是为了看懂一些老代码或特定风格的设计。

---

## 总结

- 多态基类的析构函数必须声明为 `virtual`，否则通过基类指针 `delete` 派生类对象会跳过派生类析构，造成资源泄漏。
- virtual 析构的底层和普通虚函数一样，依赖 vptr/vtable 实现动态派发。
- 不是所有类都需要 virtual 析构。非多态类、POD 类、final 类加 virtual 只会带来不必要的 vptr 开销。
- 析构函数不应抛出异常；现代 C++ 默认所有析构函数都是 `noexcept`，引擎代码应严格遵守。
- 纯虚析构可以让类成为抽象类，但仍需提供函数体，供派生类析构后调用。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 资源基类生命周期 | `IResource`、`IRenderTarget` 等基类声明 `virtual ~IResource() = default`，确保派生资源正确释放 |
| **SelfGameEngine** | 禁用通过基类删除 | `protected` 析构用于接口混入类，防止外部误用 `delete Base*` |
| **UE** | `UObject` 析构链 | `UObject` 析构为 virtual，GC 在回收时通过对象类型调用正确的析构路径，并触发 `BeginDestroy`/`FinishDestroy` 阶段 |
| **UE** | 编辑器对象销毁 | 蓝图/C++ 派生的 UObject 通过基类指针销毁时，virtual 析构保证派生类自定义清理逻辑执行 |

> [!note] 关键取舍
> 在 SelfGameEngine 这种强调数据布局的引擎里，**只有真正参与多态生命周期的类才加 virtual 析构**。ECS 组件、数学类型、SoA 数据会刻意避免 vptr。UE 则因为反射和编辑器需要大量多态基类，virtual 析构是 `UObject` 派生类的默认要求。判断标准始终只有一个：这个类是否会被通过基类指针删除？

---

> 相关笔记：
> - [[Notes/C++编程/对象内存模型与底层机制/虚函数与多态本质|虚函数与多态本质]]
> - [[Notes/C++编程/构造、析构与拷贝控制/构造函数：默认、显式、委托与继承构造|构造函数：默认、显式、委托与继承构造]]
> - [[Notes/C++编程/构造、析构与拷贝控制/默认与删除的函数|默认与删除的函数（= default / = delete）]]
> - [[Notes/C++编程/构造、析构与拷贝控制/对象构造与析构的顺序|对象构造与析构的顺序]]
> - [[Notes/C++编程/资源管理与对象生存期/智能指针与所有权模型|智能指针与所有权模型]]
