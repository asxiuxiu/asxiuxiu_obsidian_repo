---
title: 内存序：relaxed、acquire、release、seq_cst
date: 2026-06-13
tags:
  - C++
  - 并发
  - 内存序
  - memory_order
  - acquire-release
aliases:
  - 内存序
  - memory order
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 内存序：relaxed、acquire、release、seq_cst

> [!info] 一句话概括
> **内存序（Memory Order）** 决定原子操作周围的普通读写指令能否被重排；它是在"性能"和"跨线程可见性保证"之间做取舍的工具。

---

## 问题 0：原子操作不是已经保证原子性了吗？为什么还要内存序？

这是一个常见的误解。`std::atomic` 确实保证"读或写操作本身不可分割"，但它**不自动保证操作顺序**。

考虑这个例子：

```cpp
// flags: -O0 -g
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;                 // 普通写
    ready.store(true);         // 原子写
}

void consumer() {
    while (!ready.load()) { }  // 原子读
    std::cout << data << "\n"; // 普通读
}
```

如果 `ready.store(true)` 和 `ready.load()` 只保证原子性，不保证顺序，那么 `data = 42` 仍然可能被重排到 `ready.store(true)` 之后。consumer 看到 `ready == true` 时，`data` 可能还是 `0`。

内存序就是用来控制这种重排行为的。

---

## 问题 1：什么是内存序？

**内存序（Memory Order）** 是原子操作附带的一个"同步强度"参数。它告诉编译器和 CPU：

- 这个原子操作之前的普通读写，能不能重排到它之后？
- 这个原子操作之后的普通读写，能不能重排到它之前？
- 它是否要和所有其他线程的原子操作保持全局一致顺序？

你可以把它理解为一份**同步合同**：
- 合同越严格，保证越强，性能代价越大。
- 合同越宽松，性能越好，但同步保证越少。

C++11 提供了六种内存序，其中最常用的四种是：

- `memory_order_relaxed`
- `memory_order_acquire`
- `memory_order_release`
- `memory_order_seq_cst`

---

## 问题 2：memory_order_seq_cst —— 最强的默认保证

如果你不指定内存序，`std::atomic` 默认使用 `memory_order_seq_cst`（sequentially consistent，顺序一致）。

它提供两个保证：

1. **所有线程以相同的顺序看到所有 `seq_cst` 操作**。
2. 在调用者线程中，**`seq_cst` 操作之前的代码不会重排到它之后，之后的代码不会重排到它之前**。

```cpp
// flags: -O0 -g
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;
    ready.store(true);  // 默认 seq_cst
}

void consumer() {
    while (!ready.load()) { }  // 默认 seq_cst
    std::cout << data << "\n";  // 保证看到 42
}
```

> [!abstract]
> **seq_cst 的语义**：所有线程对 seq_cst 原子操作的观察顺序一致。它是最直观、最容易推理的内存序，就像所有线程按一个全局顺序执行一样。

但强大的保证有代价：编译器和 CPU 不能自由重排 seq_cst 操作，在多核系统上可能需要插入内存屏障（memory fence），性能比 relaxed 差。

---

## 问题 3：release 和 acquire 如何配对同步？

如果理解了"锁的释放-获取"语义，release-acquire 就很容易懂。

- **`memory_order_release`**：用于写操作（store）。它保证**此操作之前的所有普通读写不会重排到它之后**。
- **`memory_order_acquire`**：用于读操作（load）。它保证**此操作之后的所有普通读写不会重排到它之前**。

当线程 A 对原子变量 X 执行 release store，线程 B 对同一个 X 执行 acquire load 并读到该值时，就建立了 synchronizes-with 关系：A 在 release 之前的所有写入，对 B 在 acquire 之后的读取可见。

```cpp
// flags: -O0 -g
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;
    ready.store(true, std::memory_order_release);
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) { }
    std::cout << data << "\n";  // 保证看到 42
}
```

这就是上一篇笔记讲的 **synchronizes-with** 在原子操作中的具体实现。

---

## 问题 4：memory_order_relaxed —— 什么时候可以"放松"？

**`memory_order_relaxed`** 只保证操作本身的原子性，**不保证任何顺序或可见性**。

它适用于"只关心计数，不关心与其他变量顺序"的场景。

```cpp
// flags: -O0 -g
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};

void worker() {
    for (int i = 0; i < 100000; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}
```

这里我们只想统计两个线程各执行了多少次，最后总和正确就行。`counter` 的递增不需要和其他变量同步，用 relaxed 性能最好。

> [!warning]
> 使用 relaxed 时必须非常小心：它不能保证 `counter` 的修改顺序被其他线程以相同顺序观察到，也不能阻止 surrounding 的普通读写被重排。如果用它来实现标志位同步，会出数据竞争。

---

## 问题 5：四种内存序怎么选？

下面这张表只用于总结已经讲过的概念：

| 内存序 | 核心保证 | 典型使用场景 | 性能 |
|--------|---------|-------------|------|
| `seq_cst` | 全局一致顺序， strongest | 默认使用；多线程状态标志、简单同步 | 最低 |
| `release` | 前序读写不后移 | 与 acquire 配对，发布数据 | 中等 |
| `acquire` | 后续读写不前移 | 与 release 配对，消费数据 | 中等 |
| `relaxed` | 仅原子性 | 独立计数器、统计量 | 最高 |

> [!tip]
> 选内存序的原则：**先用 seq_cst 保证正确，测试发现瓶颈后再考虑放宽到 acquire-release 或 relaxed。** 过早优化内存序是并发 bug 的主要来源。

---

## 总结

- **内存序**控制原子操作周围的指令重排，是性能和可见性之间的权衡。
- **`memory_order_seq_cst`** 提供最强保证，所有线程看到一致的全局顺序，默认且最安全。
- **`memory_order_release` + `memory_order_acquire`** 配对使用，实现单向同步：release 前写入对 acquire 后读取可见。
- **`memory_order_relaxed`** 只保证原子性，用于独立计数等不需要跨变量同步的场景。
- 选择内存序时，先保证正确性，再考虑性能。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 命令缓冲提交、并行 System 读写屏障 | 渲染线程向 GPU 提交命令缓冲前用 release 保证资源准备完毕；消费线程用 acquire 读取；独立计数器用 relaxed 统计帧时间 |
| **UE** | RHI 命令提交、任务图依赖、引用计数 | `FRHICommandList` 使用 memory barrier 保证命令顺序；引用计数通常可用 relaxed；任务图依赖边使用 acquire-release 保证前置任务结果可见 |

> [!note] 关键取舍
> 工业引擎中，**seq_cst 是默认且推荐的选择**。只有在 profiling 证明某条原子操作是热点，且能严格证明 relaxed/acquire-release 足够正确时，才会手动放宽。无锁编程的第一原则是"先对，再快"。

---

> 相关笔记：
> - [[Notes/C++编程/并发与内存模型/C++11 内存模型与 happens-before#什么是 synchronizes-with|C++11 内存模型与 happens-before]] — 理解 release-acquire 背后的 synchronizes-with 关系
> - [[Notes/C++编程/并发与内存模型/原子操作与无锁编程基础#什么是 CAS|原子操作与无锁编程基础]] — 学习 CAS 循环和无锁数据结构基础
