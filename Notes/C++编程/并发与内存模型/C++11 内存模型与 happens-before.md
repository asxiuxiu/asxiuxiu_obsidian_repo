---
title: C++11 内存模型与 happens-before
date: 2026-06-13
tags:
  - C++
  - 并发
  - 内存模型
  - happens-before
aliases:
  - 内存模型
  - happens-before
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# C++11 内存模型与 happens-before

> [!info] 一句话概括
> C++11 内存模型是一套**关于多线程程序如何读写内存的正式规则**；happens-before 则是这套规则里用来判断"一个线程的写入对另一个线程是否可见"的核心关系。

---

## 问题 0：为什么需要内存模型？没有它我们会遇到什么麻烦？

想象你写了一个非常简单的程序：一个线程把 `ready` 设为 `true`，另一个线程一直在读 `ready`，读到 `true` 之后去读 `data`：

```cpp
// flags: -O0 -g
#include <iostream>
#include <thread>

int data = 0;
bool ready = false;

void producer() {
    data = 42;
    ready = true;
}

void consumer() {
    while (!ready) { }
    std::cout << data << "\n";
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

在单线程世界里，我们会天然觉得：先写 `data`，再写 `ready`，所以 consumer 看到 `ready == true` 时，`data` 一定是 `42`。

但多线程编译器不这么想。编译器和 CPU 都觉得："这两个写入之间没有依赖，我可以重排顺序。" 于是 `ready = true` 可能先于 `data = 42` 执行。consumer 看到 `ready` 为真，读到 `data` 却可能是 `0`。

更糟的是，因为每个 CPU 核心有自己的缓存，即使写入顺序就是 `data` 先、`ready` 后，consumer 所在核心也可能先看到 `ready` 更新，后看到 `data` 更新。

所以问题来了：**在没有统一规则的情况下，我们根本无法判断一个多线程程序会不会出问题。** 这就是 C++11 引入内存模型的原因。

---

## 问题 1：什么是内存模型？

**内存模型（Memory Model）**是编程语言对"多线程环境下，变量读写如何被排序、如何被其他线程看到"的正式约定。

它回答的不是"内存怎么分配"那种低级问题，而是：

- 一个线程对变量的写入，什么时候对另一个线程可见？
- 编译器和 CPU 能不能重排我的代码？
- 如果多个线程同时读写同一个变量，什么情况下算"正确"，什么情况下是"数据竞争"？

你可以把内存模型理解成一份**多线程程序的可见性合同**：它规定了写操作和读操作之间的"可见顺序"，让所有开发者、编译器、CPU 都按同一套规则办事。

> [!abstract]
> **C++11 内存模型**定义了：
> 1. 程序中的操作之间的**先后顺序关系**（sequenced-before）；
> 2. 线程之间的**同步关系**（synchronizes-with）；
> 3. 由前两者推导出的**happens-before 关系**；
> 4. 以及违反规则时的**数据竞争（data race）**定义。

---

## 问题 2：什么是数据竞争？

**数据竞争**是指：两个或多个线程同时访问同一个内存位置，其中至少一个是写操作，并且这些访问之间没有任何同步。

```cpp
// flags: -O0 -g
#include <thread>

int counter = 0;

void worker() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;  // 读 + 改 + 写，三步
    }
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    // counter 几乎不可能是 200000
    return 0;
}
```

`++counter` 看起来只有一行，但底层是三步：读 counter 到寄存器、寄存器加一、写回内存。两个线程的三步可能交错执行，导致一次增量被另一次覆盖。

> [!warning]
> 数据竞争是**未定义行为（Undefined Behavior）**。这意味着程序可能输出错误结果、崩溃，甚至在不同编译器/CPU 上表现完全不同。不要依赖"看起来对"的运气。

---

## 问题 3：什么是 sequenced-before？

在单线程里，语句是按书写顺序执行的——对吧？其实也不完全对，编译器可以在不改变**可观察行为**的前提下重排指令。C++11 引入了 **sequenced-before** 来精确定义"单线程内两个操作的先后关系"。

**sequenced-before** 说的是：如果表达式 A 的计算在表达式 B 的计算之前发生，并且 A 的结果会影响 B，那么 A sequenced-before B。

```cpp
// flags: -O0 -g
int main() {
    int a = 1;
    int b = a + 2;  // a = 1 sequenced-before b = 3
    return 0;
}
```

但要注意，对于没有依赖的两个独立语句，编译器仍然可能重排：

```cpp
// flags: -O0 -g
int main() {
    int x = 1;
    int y = 2;
    // x = 1 sequenced-before y = 2 吗？不一定，没有数据依赖时可能重排
    return 0;
}
```

> [!tip]
> sequenced-before 是单线程内的"本地顺序"。它保证同一线程内的操作对自己而言有确定语义，但不保证对另一个线程可见。

---

## 问题 4：什么是 synchronizes-with？

单线程有 sequenced-before，多线程之间靠什么建立顺序？靠 **synchronizes-with**。

**synchronizes-with** 描述的是：一个线程中的某个释放操作（release）和另一个线程中的某个获取操作（acquire）之间建立的同步关系。

最典型的例子是锁：线程 A 释放锁，线程 B 随后获取同一把锁。那么 A 在释放锁之前的所有写入，对 B 在获取锁之后都可见。

```cpp
// flags: -O0 -g
#include <mutex>
#include <thread>
#include <iostream>

int data = 0;
std::mutex mtx;
bool ready = false;

void producer() {
    std::lock_guard<std::mutex> lock(mtx);
    data = 42;
    ready = true;  // 在锁保护下写
}

void consumer() {
    while (true) {
        std::lock_guard<std::mutex> lock(mtx);
        if (ready) {
            std::cout << data << "\n";  // 保证看到 42
            return;
        }
    }
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

这里，`producer` 释放锁之前的 `data = 42` 和 `ready = true`，与 `consumer` 获取锁之后建立了 synchronizes-with。所以 consumer 获取锁之后读 `data`，一定能看到 `42`。

---

## 问题 5：什么是 happens-before？

现在我们可以回答最开始的问题了：怎么判断"一个写入对另一个读取可见"？

**happens-before** 是一个推导关系。如果 A happens-before B，那么 A 的效果对 B 可见。

它有三种构成方式：

1. **同一线程内**：如果 A sequenced-before B，那么 A happens-before B。
2. **线程之间**：如果 A synchronizes-with B，那么 A happens-before B。
3. **传递性**：如果 A happens-before B，且 B happens-before C，那么 A happens-before C。

```cpp
// flags: -O0 -g
int main() {
    // 同线程
    int a = 1;      // A
    int b = a + 1;  // B, A sequenced-before B, 所以 A happens-before B

    // 跨线程（通过锁）—— 概念示意
    // thread1: data = 42; lock.release();   // A, release
    // thread2: lock.acquire(); read data;   // B, acquire
    // A synchronizes-with B, 所以 A happens-before B
    return 0;
}
```

> [!info]
> happens-before 不是"物理时间上的先后"。它只保证**可见性**和**顺序性**：如果 A happens-before B，那么 A 的结果对 B 可见，且 A 不会在 B 之后发生。

---

## 问题 6：为什么需要这些抽象？

你可能会问：为什么不直接说"按代码顺序执行"？

因为现代计算机为了性能，到处都是重排：

- 编译器重排：在不影响单线程语义的前提下调整指令顺序。
- CPU 乱序执行：处理器可能先执行后面的独立指令。
- 缓存一致性延迟：不同核心看到内存更新的时间不同。

如果没有 sequenced-before、synchronizes-with、happens-before 这些精确定义，我们就无法判断：

- 编译器重排后，我的多线程代码语义变了吗？
- CPU 缓存延迟会不会导致另一个线程看到旧值？
- 锁和原子操作到底保证了什么？

这些抽象把"肉眼可见的代码顺序"和"实际执行时的顺序"分开，让我们可以在更高的层次上推理程序正确性。

---

## 总结

- **内存模型**是多线程程序的"可见性合同"，规定了读写操作如何被排序和观察。
- **数据竞争**是未定义行为，发生在无同步的并发读写中。
- **sequenced-before** 描述单线程内的操作顺序。
- **synchronizes-with** 描述线程间通过同步机制（如锁、原子操作的 acquire-release）建立的顺序。
- **happens-before** 由前两者推导而来，是判断"写入是否对读取可见"的核心工具。
- 这些抽象的存在，是因为编译器和 CPU 会重排指令，我们需要一种不依赖物理时间的推理方式。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 任务系统、命令缓冲提交 | 主线程向工作线程投递任务时，需要保证任务数据构造完成后再被读取；happens-before 是任务队列正确性的理论基础 |
| **UE** | 多线程渲染、异步加载 | `FRHICommandList` 提交命令到渲染线程时，依赖 release-acquire 语义保证 GPU 资源状态对渲染线程可见；数据竞争会导致随机崩溃或画面错误 |

> [!note] 关键取舍
> 工业引擎很少直接裸用 `std::atomic` 的底层语义，而是封装成 `SpinLock`、`Mutex`、`TaskGraph` 等原语。但**这些封装底层仍然依赖 C++11 内存模型提供的 happens-before 保证**。理解了内存模型，才能在使用锁和无锁结构时知道"为什么这样写是对的"。

---

> 相关笔记：
> - [[Notes/C++编程/并发与内存模型/原子操作与无锁编程基础#std::atomic 是什么|原子操作与无锁编程基础]] — 学习 std::atomic 提供的原子操作和 CAS
> - [[Notes/C++编程/并发与内存模型/内存序：relaxed、acquire、release、seq_cst#什么是内存序|内存序：relaxed、acquire、release、seq_cst]] — 理解 acquire、release、relaxed 的具体语义
