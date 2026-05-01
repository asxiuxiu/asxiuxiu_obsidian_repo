---
title: Release-Acquire——最实用的跨线程同步
date: 2026-05-01
tags:
  - 操作系统
  - 并发
  - 内存序
  - C++
aliases:
  - Release Acquire
---

> [[索引|← 返回 并发与多线程索引]]

# Release-Acquire——最实用的跨线程同步

---

## 从一个问题开始：95% 的无锁场景都在用这对组合

上一篇我们讲了五种 `memory_order`，看起来选择很多。但实际写代码时，绝大多数场景只需要一种模式：

> 一个线程准备好数据，然后通过某个原子变量"举手示意"；另一个线程看到"举手"后，安全地读取数据。

这就是 **Release-Acquire** 模式。它是无锁编程中最基础、最常用的同步原语。

---

## 核心思想：先准备好数据，再举手示意

想象一个运动员（生产者）要上场。他需要先：
1. 系好鞋带
2. 戴上护具
3. 检查装备

然后**举手示意裁判**："我准备好了！"

裁判（消费者）看到他举手，就知道"系鞋带、戴护具、检查装备"这些事一定都**已经做完了**。

Release-Acquire 做的就是这件事：
- **Release（举手）**：保证"举手"之前的所有操作（准备数据）不会被重排到"举手"之后。
- **Acquire（看到举手）**：保证"看到举手"之后的所有操作（读取数据）不会被重排到"看到举手"之前。

```cpp
std::atomic<int*> g_data{nullptr};

// 生产者：先写好数据，再举手示意
void producer() {
    int* p = new int(42);                              // 准备数据
    g_data.store(p, std::memory_order_release);        // 举手（release）
}

// 消费者：看到举手，就知道数据准备好了
void consumer() {
    int* p = nullptr;
    while (!(p = g_data.load(std::memory_order_acquire))) {
        // 自旋等待……生产实践中会用 condition variable 或 yield
    }
    // 保证能看到完整的 *p == 42
    std::cout << *p << "\n";
}
```

---

## 代码示例：用版本号发布批量数据

游戏中常见的场景：主线程每帧更新玩家状态，渲染线程读取状态来绘制画面。状态包含很多字段（位置、旋转、血量、弹药），不可能每个字段都用 `std::atomic`。

解决方案：**用版本号作为"整批数据的发布标志"**。

```cpp
// release_acquire_gamestate.cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

struct GameState {
    float player_x = 0.0f;
    float player_y = 0.0f;
    int health = 100;
    int ammo = 30;
};

GameState g_state;                    // 普通变量，大量字段
std::atomic<int> g_version{0};        // 原子版本号，作为发布标志

// 主线程：更新游戏状态
void update_thread() {
    for (int frame = 1; frame <= 5; ++frame) {
        // 模拟一帧的更新
        g_state.player_x += 1.0f;
        g_state.player_y += 2.0f;
        g_state.health -= 5;
        g_state.ammo -= 1;
        
        // 所有数据写完后，一次性提升版本号（release）
        g_version.fetch_add(1, std::memory_order_release);
        
        std::cout << "[Update] Frame " << frame 
                  << " version=" << g_version.load() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 渲染线程：读取游戏状态
void render_thread() {
    int local_version = 0;
    int frames_rendered = 0;
    
    while (frames_rendered < 5) {
        int ver = g_version.load(std::memory_order_acquire);
        if (ver != local_version) {           // 检测到新版本！
            local_version = ver;
            
            // 由于 acquire，这里读取的所有字段都保证是最新的
            std::cout << "[Render] Version " << ver 
                      << ": pos=(" << g_state.player_x 
                      << ", " << g_state.player_y << ")"
                      << " health=" << g_state.health
                      << " ammo=" << g_state.ammo << "\n";
            frames_rendered++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main() {
    std::thread t1(update_thread);
    std::thread t2(render_thread);
    t1.join(); t2.join();
    return 0;
}
```

**关键理解**：
- `fetch_add(release)` 保证：版本号提升**之前**，所有对 `g_state` 的写入都已经完成。
- `load(acquire)` 保证：一旦渲染线程看到版本号变了，它读取 `g_state` 的任何字段都能看到**最新的值**。

---

## 四种场景的 memory_order 选择

现在你可以根据场景快速选型了：

### 场景 1：只关心最终数字对不对（计数器、统计）

```cpp
std::atomic<int> tasks_done{0};
tasks_done.fetch_add(1, std::memory_order_relaxed);  // 完成计数
```

**为什么用 `relaxed`？** 没有任何线程需要根据这个计数器的值去推断"其他数据是否准备好了"。它只是一个数字。

### 场景 2：先准备好数据，再举手示意（生产者-消费者）

```cpp
g_data.store(ptr, std::memory_order_release);    // 生产者发布
ptr = g_data.load(std::memory_order_acquire);    // 消费者获取
```

**为什么用 `release` + `acquire`？** 单向同步：A 线程完成一系列操作，然后通过原子变量通知 B 线程"你可以继续了"。

### 场景 3：争夺一把虚拟锁（自旋锁）

```cpp
// lock() 用 acquire，unlock() 用 release
while (locked.exchange(true, std::memory_order_acquire)) {}
locked.store(false, std::memory_order_release);
```

**为什么？** 自旋锁的本质是"保护一段临界区"：`unlock` 之前的所有写，必须在 `lock` 之后对下一个获得锁的线程可见。

### 场景 4：所有人都必须对先后顺序达成一致（初始化标志）

```cpp
g_stage.compare_exchange_strong(expected, 1, std::memory_order_seq_cst);
```

**为什么用 `seq_cst`？** 多个线程同时尝试初始化，需要所有线程对"谁先谁后"达成一致。`seq_cst` 是唯一提供**全局顺序一致性**的级别。

---

## 动手实验

### 实验：验证 Release-Acquire 的正确性

```cpp
// ra_correctness.cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

constexpr int NUM_PAIRS = 4;
constexpr int ITERATIONS = 100000;

std::atomic<int> data{0};
std::atomic<bool> ready{false};

void producer() {
    for (int i = 0; i < ITERATIONS; ++i) {
        data.store(i, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        
        // 等消费者读完
        while (ready.load(std::memory_order_relaxed)) {}
    }
}

void consumer() {
    for (int i = 0; i < ITERATIONS; ++i) {
        while (!ready.load(std::memory_order_acquire)) {}
        int val = data.load(std::memory_order_relaxed);
        
        if (val != i) {
            std::cout << "ERROR: expected " << i << " got " << val << "\n";
            return;
        }
        
        ready.store(false, std::memory_order_relaxed);
    }
    std::cout << "All " << ITERATIONS << " iterations passed!\n";
}

int main() {
    std::thread p(producer);
    std::thread c(consumer);
    p.join(); c.join();
    return 0;
}
```

**预期结果**：永远输出 `All 100000 iterations passed!`。如果把 `release` 和 `acquire` 都改成 `relaxed`，可能观察到错误（尤其在 ARM 上）。

---

## 一句话总结

| 需求 | 选型 |
|------|------|
| 不需要同步，只保证原子性 | `relaxed` |
| 单向通知：A 准备完数据，通知 B | `release` / `acquire` |
| 互斥访问：锁保护临界区 | `acquire`（加锁）/ `release`（解锁） |
| 全局一致：多线程对顺序达成一致 | `seq_cst` |

> **问题链**：现在你已经掌握了原子操作、锁、条件变量和内存序。下一篇，我们把所有这些知识串起来，实现一个真正的**无锁队列**——这是游戏引擎 Job System 的核心数据结构。→ [[无锁队列——综合运用]]

---

> [!info] 延伸阅读
> - [[无锁队列——综合运用]] —— SPSC Ring Buffer 的完整实现
> - [[内存序——编译器和CPU如何"欺骗"你]] —— 内存序的理论基础
