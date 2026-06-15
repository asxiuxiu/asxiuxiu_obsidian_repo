---
title: 内存对齐规则与 SIMD 对齐
date: 2026-06-13
tags:
  - C++
  - 内存对齐
  - SIMD
  - alignas
  - alignof
  - padding
aliases:
  - 内存对齐
  - SIMD 对齐
  - alignas
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 内存对齐规则与 SIMD 对齐

> [!info] 一句话概括
> **对齐（alignment）**是 CPU 访问内存时要求的起始地址约束；C++ 用 `alignof` 查询、`alignas` 控制，而 SIMD 指令往往要求 16/32 字节对齐，否则只能使用速度更慢的未对齐加载指令。

---

## 问题 0：为什么结构体大小不等于成员大小之和？

写出下面这个结构体，你猜它占多少字节？

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

struct Messy {
    char c;   // 1 字节
    int i;    // 4 字节
    char d;   // 1 字节
};

int main() {
    std::cout << "sizeof(Messy) = " << sizeof(Messy) << "\n";
    std::cout << "offsetof(Messy, c) = " << offsetof(Messy, c) << "\n";
    std::cout << "offsetof(Messy, i) = " << offsetof(Messy, i) << "\n";
    std::cout << "offsetof(Messy, d) = " << offsetof(Messy, d) << "\n";
    return 0;
}
```

在大多数平台上，`sizeof(Messy)` 是 12，而不是 6。原因是编译器在 `c` 后面插入了 3 个填充字节，让 `i` 从 4 字节边界开始；最后又在 `d` 后面补了 3 个字节，让结构体整体大小成为其最大对齐要求的倍数。

---

## 问题 1：什么是自然对齐？

**自然对齐**指：一个类型的对象必须存放在其 `alignof` 值的整数倍地址上。例如 `int` 通常要求 4 字节对齐，`double` 要求 8 字节对齐，指针要求 8 字节对齐（64 位平台）。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

int main() {
    std::cout << "alignof(char)   = " << alignof(char) << "\n";
    std::cout << "alignof(short)  = " << alignof(short) << "\n";
    std::cout << "alignof(int)    = " << alignof(int) << "\n";
    std::cout << "alignof(double) = " << alignof(double) << "\n";
    std::cout << "alignof(void*)  = " << alignof(void*) << "\n";

    struct S {
        char c;
        double d;
    };
    std::cout << "alignof(S)      = " << alignof(S) << "\n";
    std::cout << "sizeof(S)       = " << sizeof(S) << "\n";
    return 0;
}
```

结构体的对齐要求等于其成员中最大的对齐要求；结构体的大小必须是该对齐要求的整数倍。这就是为什么 `S` 通常占 16 字节：`double` 对齐 8，编译器在 `c` 后补 7 字节，整体大小再向上取整到 8 的倍数。

---

## 问题 2：`#pragma pack` 能做什么？

有时候你希望牺牲对齐换取更紧凑的内存布局，比如网络协议包头或文件格式头。`#pragma pack` 可以强制降低结构体的对齐要求。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

#pragma pack(push, 1)
struct Packed {
    char c;
    int i;
    char d;
};
#pragma pack(pop)

struct Normal {
    char c;
    int i;
    char d;
};

int main() {
    std::cout << "sizeof(Packed) = " << sizeof(Packed) << "\n";
    std::cout << "sizeof(Normal) = " << sizeof(Normal) << "\n";
    std::cout << "alignof(Packed) = " << alignof(Packed) << "\n";
    std::cout << "alignof(Normal) = " << alignof(Normal) << "\n";
    return 0;
}
```

> [!warning]
> `#pragma pack` 把 `int` 的对齐要求从 4 降到 1，虽然节省了空间，但读取未对齐的 `int` 可能需要 CPU 做两次内存访问，甚至在某些架构（如 ARM）上触发总线错误。引擎中通常只在序列化/反序列化或硬件寄存器映射时使用它。

---

## 问题 3：怎么显式控制对齐？

C++11 引入 `alignas` 关键字，可以要求某个类型或变量按更大的边界对齐。例如 `alignas(64)` 会把对象放在 64 字节边界上，正好占满一条缓存行。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

struct alignas(64) CacheLineBlock {
    int data[16]; // 64 字节
};

struct alignas(16) Vec4 {
    float x, y, z, w;
};

int main() {
    CacheLineBlock block;
    Vec4 v;

    std::cout << "alignof(CacheLineBlock) = " << alignof(CacheLineBlock) << "\n";
    std::cout << "alignof(Vec4)           = " << alignof(Vec4) << "\n";
    std::cout << "block address % 64 = " << (reinterpret_cast<uintptr_t>(&block) % 64) << "\n";
    std::cout << "v address % 16   = " << (reinterpret_cast<uintptr_t>(&v) % 16) << "\n";
    return 0;
}
```

> [!tip]
> `alignas(N)` 指定的对齐值必须是 2 的幂，且不能小于类型的自然对齐。如果你想让结构体按缓存行对齐以减少 false sharing，通常用 `alignas(64)`。

---

## 问题 4：过度对齐的对象怎么分配？

如果一个类型的对齐要求超过了 `alignof(std::max_align_t)`（通常是 16），它就是**过度对齐（over-aligned）**类型。C++17 之前，`new T` 不保证能正确分配过度对齐的对象；C++17 起，标准 `operator new` 会尊重类型的对齐要求。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

struct alignas(64) BigAligned {
    int value = 42;
};

int main() {
    auto p = new BigAligned;
    std::cout << "alignof(BigAligned) = " << alignof(BigAligned) << "\n";
    std::cout << "address % 64 = " << (reinterpret_cast<uintptr_t>(p) % 64) << "\n";
    std::cout << "value = " << p->value << "\n";
    delete p;
    return 0;
}
```

> [!abstract]
> 在 C++17 之前，过度对齐类型通常要用平台相关函数分配，如 POSIX 的 `posix_memalign`、C11 的 `aligned_alloc`，或者 Windows 的 `_aligned_malloc`。C++17 的 `std::aligned_alloc` 和改进了的 `operator new` 让这件事跨平台了很多。

---

## 问题 5：为什么 SIMD 需要 16/32 字节对齐？

SIMD（单指令多数据）指令一次处理多个数据。以 SSE 为例，`_mm_load_ps` 一次性加载 4 个 `float`（128 位）。如果这 4 个 `float` 的起始地址不是 16 的倍数，CPU 就不得不做额外的拆分加载；某些旧 CPU 甚至会直接异常。

```cpp
// flags: -std=c++20 -Wall -O2 -msse2
#include <immintrin.h>
#include <iostream>

int main() {
    alignas(16) float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    __m128 v = _mm_load_ps(data);    // 16 字节对齐加载
    v = _mm_add_ps(v, v);            // 每个分量乘以 2
    _mm_store_ps(data, v);           // 存回

    std::cout << "result: ";
    for (float x : data) std::cout << x << " ";
    std::cout << "\n";

    // 未对齐版本：能运行，但可能更慢
    float unaligned[5] = {0, 1, 2, 3, 4};
    __m128 u = _mm_loadu_ps(unaligned + 1);
    u = _mm_add_ps(u, u);
    _mm_storeu_ps(unaligned + 1, u);
    return 0;
}
```

> [!note]
> AVX 把向量宽度提升到 256 位，因此 AVX 指令通常要求 32 字节对齐；AVX-512 则要求 64 字节对齐。现代 CPU 的未对齐加载已经比较快，但在数据密集循环中，手动保证对齐仍然是获得峰值吞吐的关键。

---

## 问题 6：怎么通过重排成员减少 padding？

把成员按大小从大到小排列，通常能减少结构体内部的填充字节。

```cpp
// flags: -std=c++20 -Wall -O2
#include <iostream>

struct BadOrder {
    char c;     // 1
    double d;   // 8
    int i;      // 4
    char pad;   // 编译器可能补到 24
};

struct GoodOrder {
    double d;   // 8
    int i;      // 4
    char c;     // 1
    char pad;   // 编译器补 1 字节到 16
};

int main() {
    std::cout << "sizeof(BadOrder)  = " << sizeof(BadOrder) << "\n";
    std::cout << "sizeof(GoodOrder) = " << sizeof(GoodOrder) << "\n";
    std::cout << "alignof(BadOrder) = " << alignof(BadOrder) << "\n";
    std::cout << "alignof(GoodOrder)= " << alignof(GoodOrder) << "\n";
    return 0;
}
```

> [!tip]
> 重排成员是「面向数据设计」的基本操作。在 ECS 组件或粒子系统中，把高频访问的字段放在一起、按对齐要求排序，能同时减少 padding 和提高缓存利用率。

---

## 总结

- 对齐是类型对象存放地址的约束，`alignof` 返回类型的对齐要求。
- 结构体内部会因对齐要求产生 **padding**；结构体大小是其最大对齐要求的整数倍。
- `#pragma pack` 可以强制紧凑布局，但可能牺牲访问速度或在某些平台上引发错误。
- `alignas` 显式提升对齐，常用于缓存行对齐、SIMD 类型、锁-free 数据结构。
- 过度对齐类型在 C++17 后由 `operator new` 正确处理；更早的标准需要平台相关函数。
- SIMD 指令（SSE/AVX）对 16/32 字节对齐敏感，未对齐访问可能更慢或在旧硬件上出错。
- 按成员大小降序排列结构体成员，是减少 padding 最简单有效的技巧。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 数学类型 SIMD 对齐 | `Vec4`、`Mat4` 等类型使用 `alignas(16)` 保证 SSE/AVX 友好；SoA 组件数组按 16/32 字节对齐以支持向量化遍历 |
| **UE** | ` FVector`、`FMatrix`、Mass 组件 | UE 数学类型通常按 SIMD 宽度对齐；Mass 框架对组件数组做对齐分配，配合向量化系统查询 |
| **现代 CPU 渲染/物理** | 批量计算 | 粒子位置数组、骨骼矩阵数组等通常按 16/32 字节对齐，GPU/CPU 向量化代码才能用对齐加载指令 |

> [!note] 关键取舍
> 对齐会浪费少量内存，但能换来 SIMD 和缓存友好性。引擎中的做法是：对「会被批量 SIMD 处理」的类型（数学向量、颜色、粒子属性）强制对齐；对普通配置数据保持自然对齐，避免不必要的内存膨胀。

---

> 相关笔记：
> - [[Notes/C++编程/对象内存模型与底层机制/对象内存布局：从 struct 到 class|对象内存布局]] — 结构体填充与成员排布的底层细节
> - [[Notes/C++编程/标准库原理与引擎替代方案/SoA、AoS 与 AOSOA|SoA/AoS]] — 数据布局如何配合对齐与 SIMD
