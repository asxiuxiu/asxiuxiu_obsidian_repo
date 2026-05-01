---
title: 内存序——编译器和CPU如何"欺骗"你
date: 2026-05-01
tags:
  - 操作系统
  - 并发
  - 内存序
  - C++
aliases:
  - Memory Order
---

> [[索引|← 返回 并发与多线程索引]]

# 内存序——编译器和CPU如何"欺骗"你

---

## 从一个问题开始：为什么代码顺序会被打乱？

你已经学会了 `std::atomic`——它保证操作是"原子的"，不会被线程交错打断。但原子性只解决了"不可分割"的问题，没有解决"按什么顺序被谁看到"的问题。

想象这样一个场景：

```cpp
int data = 0;
std::atomic<bool> ready{false};

// 生产者线程
void producer() {
    data = 42;                    // 步骤 1：写普通变量
    ready.store(true);            // 步骤 2：写原子变量（通知消费者）
}

// 消费者线程
void consumer() {
    while (!ready.load()) {}      // 步骤 3：等通知
    std::cout << data << "\n";    // 步骤 4：读普通变量
}
```

直觉上，消费者看到 `ready == true` 时，`data` 一定已经是 42 了。因为代码里步骤 1 在步骤 2 之前，步骤 3 在步骤 4 之前。

**但遗憾的是，这个直觉是错的。**

---

## 两个"善意欺骗者"：编译器和 CPU

### 编译器：单线程视角的"聪明"

编译器的优化逻辑很简单："只要单线程执行结果不变，我可以任意重排指令。"

```cpp
// 你写的代码                // 编译器优化后可能的顺序
data = 42;                  ready = true;
ready = true;               data = 42;
```

编译器想："`data` 和 `ready` 没有依赖关系，我先写 `ready`（可能更快，因为 ready 是 atomic 有特殊处理），再写 `data`，单线程结果完全一样。"

——但多线程下，另一个线程可能先看到 `ready = true`，再看到 `data = 0`。

### CPU：物理层面的"偷懒"

即使编译器老老实实按源码顺序生成汇编，CPU 自己也会"偷懒"：

- **Store Buffer**：写入不直接刷到内存，先进入一个缓冲区。因为写内存很慢，CPU 想"我先记下来，继续干别的，等会儿再统一刷"。
- **Load 乱序**：CPU 看到后面有一个读操作和前面的写操作没有依赖，可能提前执行读。
- **指令级并行**：无依赖的指令被并行发射到不同执行单元，谁先完成说不准。

这两个"善意欺骗"在单线程里完全无害，在多线程里就是定时炸弹。

---

## Memory Order：五种"合同严格程度"

C++11 提供了 `memory_order` 参数，让你可以和编译器/CPU签订"可见性合同"——规定操作的顺序约束。从弱到强：

| 合同级别 | 类比 | 保证内容 | 性能 |
|---------|------|---------|------|
| `relaxed` | 口头约定 | 只保证操作本身原子，不保证顺序 | 最快 |
| `acquire` | 单向门（进门后） | 本操作之后的读写不会被重排到本操作之前 | 快 |
| `release` | 单向门（出门前） | 本操作之前的读写不会被重排到本操作之后 | 快 |
| `acq_rel` | 双向门 | 同时 acquire + release | 快 |
| `seq_cst` | 公证合同 | 全局顺序一致，所有线程看到相同的操作顺序 | 最慢 |

> **关键洞察**：x86-64 的内存模型叫做 **TSO（Total Store Order）**，本身就比较强，只允许 StoreLoad 重排。所以 x86 上 `acquire`/`release` 通常不需要额外 CPU 指令（编译器屏障就够了）。但 ARM/POWER 采用**弱内存模型**，几乎允许任何重排——**在 x86 上"碰巧正确"的代码，在 ARM 上会崩溃**。

---

## Happens-Before：因果链，不是时间链

这是内存模型中最容易被误解的概念。很多人听到 Happens-Before 就以为是"物理时间上谁先谁后"——**不是的**。

想象两个城市之间有一条电报线。城市 A 发了三封电报：
1. "今天下雨"
2. "记得带伞"
3. "伞在门后"

城市 B 收到电报的顺序可能完全打乱。但城市 B 知道：**如果收到了"记得带伞"，那么"今天下雨"一定也已经发出**（虽然不一定已经收到）。这就是**因果关系**。

Happens-Before 就是多线程编程中的"因果关系"。它不是问你"哪条指令先执行"，而是问你"如果线程 B 看到了某个值，它能不能保证看到另一个相关的值"。

```
线程 A（生产者）                        线程 B（消费者）
─────────────────────────────────────────────────────────
data = 42;        ──────┐
                         │  release-acquire 建立因果关系
ready.store(true,       ├────────────────>  if (ready.load(acquire))
            release)    │                    {
                         │                      // 保证能看到 data == 42
                      happens-before          }
```

`ready.store(release)` 就像一封带有"回执"的电报："如果你收到了这封电报，那么我之前发的所有电报你也都应该能收到。"

> Happens-Before 是**传递的**。如果 A → B 且 B → C，那么 A → C。

---

## 动手实验

### 实验 1：验证 Release-Acquire 的可见性

```cpp
// acquire_release.cpp
#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>

std::atomic<int> data{0};
std::atomic<bool> ready{false};

void producer() {
    data.store(42, std::memory_order_relaxed);       // 写数据
    ready.store(true, std::memory_order_release);    // 发布：之前的写不会跑到这之后
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) { // 获取：之后的读不会跑到这之前
        // 自旋等待
    }
    // 由于 release-acquire 配对，这里一定能看到 data == 42
    assert(data.load(std::memory_order_relaxed) == 42);
    std::cout << "Consumer saw data=" << data.load() << "\n";
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join(); t2.join();
    return 0;
}
```

这个程序里的 `assert` **永远不会触发**，因为 `release` 和 `acquire` 建立了 Happens-Before 关系。

### 实验 2：不同 Memory Order 的性能差异（x86 上测 fence）

```cpp
// fence_bench.cpp
#include <atomic>
#include <chrono>
#include <iostream>

constexpr int ITER = 100'000'000;

void bench_fence(const char* name, std::memory_order order) {
    volatile int buffer[64];
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; ++i) {
        buffer[i % 64] = i;
        std::atomic_thread_fence(order);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << name << ": " << ms << " ms\n";
}

int main() {
    bench_fence("relaxed", std::memory_order_relaxed); // 空操作！
    bench_fence("acq_rel", std::memory_order_acq_rel); // 仅编译器屏障
    bench_fence("seq_cst", std::memory_order_seq_cst); // mfence 指令
    return 0;
}
```

**预期结果（x86-64）**：
- `relaxed`：最快（编译器直接删除 fence）
- `acq_rel`：与 relaxed 接近（仅阻止编译器重排）
- `seq_cst`：**显著更慢**（生成 `mfence`，冲刷 Store Buffer）

> 在 Intel i7 级别 CPU 上：`relaxed` 约 30ms，`seq_cst` 约 450ms——**15 倍差距**。

---

## 小结

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 编译器重排指令 | 单线程优化视角 | `memory_order` 约束 |
| CPU Store Buffer | 写操作延迟可见 | `release` / `seq_cst` |
| CPU Load 乱序 | 提前执行无依赖读 | `acquire` / `seq_cst` |
| 全局顺序不一致 | 多核心缓存不同步 | `seq_cst` |

> **问题链**：我们知道了内存序有五种级别，也知道了编译器和 CPU 为什么会重排代码。但在实际编程中，95% 的场景只需要一种模式：**Release-Acquire**。下一篇专门讲这个最实用的模式。→ [[Release-Acquire——最实用的跨线程同步]]

---

> [!info] 延伸阅读
> - [[Release-Acquire——最实用的跨线程同步]] —— 生产者-消费者的数据发布模式
> - [[无锁队列——综合运用]] —— 原子操作与内存序的综合实战
