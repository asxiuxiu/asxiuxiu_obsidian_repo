---
title: 缓存行、false sharing 与内存布局
date: 2026-06-13
tags:
  - C++
  - 并发
  - 缓存行
  - false sharing
  - MESI
  - 内存布局
aliases:
  - 缓存行
  - false sharing
  - 伪共享
  - MESI 协议
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 缓存行、false sharing 与内存布局

> [!info] 一句话概括
> **False sharing（伪共享）**发生在多线程修改位于同一缓存行内的不同变量时：虽然逻辑上没有共享数据，但 CPU 缓存一致性协议会让两个核心反复无效化对方的缓存行，把并行写操作串行化，严重拖慢性能。

---

## 问题 0：为什么多线程修改「不同的变量」也会变慢？

先看一段看起来很安全的代码：两个线程各自对一个独立的计数器做自增。

```cpp
// flags: -std=c++20 -Wall -O2 -pthread
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

struct Shared {
    std::atomic<int> a{0};
    std::atomic<int> b{0};
};

struct Padded {
    alignas(64) std::atomic<int> a{0};
    alignas(64) std::atomic<int> b{0};
};

template<typename T>
long long bench(T& obj, int iterations) {
    auto t0 = std::chrono::steady_clock::now();
    std::thread t1([&] {
        for (int i = 0; i < iterations; ++i) obj.a.fetch_add(1, std::memory_order_relaxed);
    });
    std::thread t2([&] {
        for (int i = 0; i < iterations; ++i) obj.b.fetch_add(1, std::memory_order_relaxed);
    });
    t1.join();
    t2.join();
    auto t_end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t0).count();
}

int main() {
    constexpr int it = 10'000'000;

    Shared s;
    Padded p;

    long long t_shared = bench(s, it);
    long long t_padded = bench(p, it);

    std::cout << "shared (false sharing): " << t_shared << " ms\n";
    std::cout << "padded (separate cache lines): " << t_padded << " ms\n";
    std::cout << "a=" << s.a << " b=" << s.b << "\n";
    std::cout << "pa=" << p.a << " pb=" << p.b << "\n";
    return 0;
}
```

在大多数现代 CPU 上，`Shared` 版本的耗时会是 `Padded` 版本的几倍甚至十几倍。问题是：`a` 和 `b` 明明是不同变量，为什么还会互相拖累？

---

## 问题 1：CPU 缓存和缓存行到底是什么？

CPU 访问主内存的速度远远低于执行计算的速度。为了弥补这个差距，CPU 在核心附近设置了多级缓存（L1、L2、L3）。缓存和内存之间的数据交换不是按单个字节进行的，而是按**缓存行（cache line）**——通常一次搬运 64 字节。

> [!abstract]
> **缓存行**是 CPU 缓存和主内存之间传输数据的最小单位，常见大小为 64 字节。当你读取一个 `int` 时，整个包含该 `int` 的 64 字节都会被加载到缓存中。后续访问同一块数据就可以直接从缓存读取，而不是访问慢得多的主内存。

在上面的 `Shared` 结构体中，`std::atomic<int> a` 和 `std::atomic<int> b` 都只有 4 字节，但它们很可能落在同一个 64 字节缓存行里。于是两个线程虽然修改不同变量，却不断读写同一条缓存行。

---

## 问题 2：MESI 协议如何导致伪共享？

现代 CPU 使用 **MESI 协议** 维护多核之间缓存的一致性。每个缓存行在任意时刻处于以下四种状态之一：

- **M（Modified）**：当前核心的缓存行已被修改，且是内存中唯一正确的副本。
- **E（Exclusive）**：当前核心独占该缓存行，未被修改。
- **S（Shared）**：多个核心共享该缓存行，内容一致且与内存一致。
- **I（Invalid）**：该缓存行已失效，需要重新从内存或其他核心加载。

当两个核心同时读取 `a` 和 `b` 时，包含它们的缓存行进入 **Shared** 状态。一旦某个核心修改了自己变量所在的缓存行，为了让其他核心看到最新值，硬件必须把这个缓存行置为 **Modified**，并把其他核心手中的副本标记为 **Invalid**。另一个核心随后修改自己的变量时，又得重新加载整条缓存行。

```cpp
// flags: -std=c++20 -Wall -O2 -pthread
#include <atomic>
#include <iostream>
#include <thread>

struct Shared {
    std::atomic<int> a{0};
    std::atomic<int> b{0};
};

int main() {
    Shared s;

    // 两个线程反复让同一条缓存行在 M/E/S/I 之间切换
    std::thread t1([&] {
        for (int i = 0; i < 1'000'000; ++i) {
            s.a.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread t2([&] {
        for (int i = 0; i < 1'000'000; ++i) {
            s.b.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();
    std::cout << "a=" << s.a << " b=" << s.b << "\n";
    return 0;
}
```

> [!warning]
> 即使使用了 `memory_order_relaxed`，原子操作本身的原子性有保证，但缓存行级别的同步开销依然存在。False sharing 是**硬件缓存一致性**带来的性能问题，不是内存序能解决的。

---

## 问题 3：怎么避免 false sharing？

最直接的方法是让每个线程频繁写的变量落在**不同的缓存行**上。常见手段有三种：

1. **按缓存行对齐**：用 `alignas(64)` 把变量放在独立的缓存行起点。
2. **padding 填充**：在结构体中插入足够字节，保证下一个变量从新的缓存行开始。
3. **线程本地聚合**：每个线程先在局部变量上累加，最后一次性写回全局结果。

```cpp
// flags: -std=c++20 -Wall -O2 -pthread
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// 方案 1：让原子变量独占一条缓存行
struct alignas(64) PaddedAtomic {
    std::atomic<int> value{0};
};

int main() {
    constexpr int iterations = 10'000'000;

    PaddedAtomic a;
    PaddedAtomic b;

    auto t0 = std::chrono::steady_clock::now();
    std::thread t1([&] {
        for (int i = 0; i < iterations; ++i) {
            a.value.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread t2([&] {
        for (int i = 0; i < iterations; ++i) {
            b.value.fetch_add(1, std::memory_order_relaxed);
        }
    });
    t1.join();
    t2.join();
    auto t_end = std::chrono::steady_clock::now();

    std::cout << "a=" << a.value << " b=" << b.value << "\n";
    std::cout << "elapsed="
              << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t0).count()
              << " ms\n";
    return 0;
}
```

> [!tip]
> `alignas(64)` 只是告诉编译器「这个对象的起始地址是 64 的倍数」，它并不能阻止同一个结构体内的两个 `alignas(64)` 成员落在同一条缓存行——事实上它们会分别起始于不同的缓存行。注意：如果你的类型大小超过 64 字节，变量可能跨越两条缓存行，这时需要额外的布局分析。

---

## 问题 4：False sharing 和并发数据结构有什么关系？

在引擎的并行系统中，false sharing 经常出现在这些场景：

- **任务计数器**：多个工作线程各自更新一个全局计数器数组，如果计数器挨在一起，就会互相拖慢。
- **锁/原子标志**：多个锁对象放在相邻字段，高竞争时会在缓存行层面打架。
- **ECS 组件数组**：如果多个 System 并行读写不同 Component 但共享缓存行，性能会大打折扣。

```cpp
// flags: -std=c++20 -Wall -O2 -pthread
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// 计数器包装，不加额外对齐 → 多个计数器会落在同一条缓存行
struct Counter {
    std::atomic<int> value{0};
};

struct BadCounters {
    Counter counters[8];
};

// 每个计数器独占一条缓存行
struct alignas(64) AlignedCounter {
    std::atomic<int> value{0};
};

struct GoodCounters {
    AlignedCounter counters[8];
};

template<typename T>
long long bench(T& counters, int n) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&, i] {
            for (int j = 0; j < n; ++j) {
                counters.counters[i].value.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();
    auto t_end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t0).count();
}

int main() {
    constexpr int n = 1'000'000;
    BadCounters bad;
    GoodCounters good;
    std::cout << "bad:  " << bench(bad, n) << " ms\n";
    std::cout << "good: " << bench(good, n) << " ms\n";
    return 0;
}
```

> [!note]
> `alignas(64)` 放在数组元素前会让整个数组按 64 字节对齐，但数组内部相邻元素仍然可能共享缓存行。如果每个元素都需要独立缓存行，应该单独分配每个计数器，或者使用 `struct PerThreadCounter { alignas(64) std::atomic<int> value; };` 这样的包装类型。

---

## 总结

- CPU 缓存以**缓存行**（通常 64 字节）为单位和内存交换数据。
- **False sharing** 指多个线程修改同一缓存行内的不同变量，导致缓存一致性协议反复无效化对方缓存行。
- MESI 协议定义了缓存行的 Modified/Exclusive/Shared/Invalid 四种状态，是 false sharing 的硬件根源。
- 避免 false sharing 的方法包括：`alignas(64)` 对齐、padding 填充、线程本地聚合后再写回。
- 在并发数据结构和 ECS 布局中，缓存行意识能显著提升多核扩展性。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 任务系统计数器、线程池状态 | 每个工作线程的完成计数器单独按缓存行对齐；并行 System 的组件数组布局避免热点缓存行 |
| **UE** | 任务图（Task Graph）、Mass 框架 | 任务依赖计数器和 ECS 批量更新时会对齐到缓存行，减少多线程同步开销 |
| **现代 CPU 优化** | 无锁队列、工作窃取 | 队列的 head/tail 指针若在同一缓存行会成为瓶颈，工业级实现通常把它们分到不同缓存行 |

> [!note] 关键取舍
> 按缓存行对齐会浪费内存（每个小变量占 64 字节），所以只在**高并发写**的字段上使用。对于读多写少或单线程访问的数据，盲目对齐反而降低缓存利用率。理解 false sharing 是为了在「内存紧凑」和「并发效率」之间做正确决策。

---

> 相关笔记：
> - [[Notes/C++编程/并发与内存模型/C++11 内存模型与 happens-before|内存模型]] — 并发编程的同步关系与数据竞争
> - [[Notes/C++编程/并发与内存模型/原子操作与无锁编程基础|原子操作]] — 原子变量与无锁算法基础
> - [[Notes/C++编程/标准库原理与引擎替代方案/SoA、AoS 与 AOSOA|SoA/AoS]] — 数据布局对缓存和 SIMD 的影响
