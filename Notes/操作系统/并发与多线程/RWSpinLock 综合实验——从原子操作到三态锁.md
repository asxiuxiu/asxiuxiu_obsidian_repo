---
title: RWSpinLock 综合实验——从原子操作到三态锁
date: 2026-06-02
tags:
  - 操作系统
  - 并发
  - 自旋锁
  - CAS
  - 实验
  - 练习
aliases:
  - RWSpinLock 实验
---

> [[Notes/操作系统/并发与多线程/索引|← 返回 并发与多线程索引]]

# RWSpinLock 综合实验——从原子操作到三态锁

---

## 为什么做这个实验？

如果你读完 atomic、spinlock、memory_order 的笔记后感觉"好像懂了但又串不起来"，这是正常的——**这些概念只有在同一个真实问题中被同时调用时，才能真正内化成你的思维工具**。

`RWSpinLock`（读写自旋锁）是游戏引擎中高频出现的组件，它用**一个 `std::atomic<int32_t>`** 同时管理三种状态：

- 多个读者同时读
- 一个升级锁持有者（阻塞新读者，等待旧读者离开）
- 一个写者独占

实现它需要同时运用：
- `fetch_add` / `fetch_sub`（读者计数）
- `compare_exchange_strong`（写锁抢占）
- `memory_order_acquire` / `release` / `acq_rel`（状态转换的可见性）

这个实验的目标：**不直接看 chaos 源码，自己推导并实现一个简化版 RWSpinLock**。

---

## 实验前自检

在开始前，请确认你能回答以下问题（如果不能，先回到对应笔记复习）：

1. `std::atomic<int>` 的 `fetch_add(4)` 返回的是什么？是加之后的值还是加之前的值？
2. `compare_exchange_strong(expected, desired)` 在成功和失败时分别对 `expected` 做了什么？
3. 为什么 `memory_order_acquire` 叫"获取"，它能保证看到别的线程的什么操作？
4. 自旋锁的 TTAS（Test-Test-And-Set）优化是为了解决什么问题？

> 如果以上有任何一题不确定，先去复习：
> - [[atomic——让操作不可分割]]
> - [[自旋锁——另一种等待方式]]
> - [[Release-Acquire——最实用的跨线程同步]]

---

## 实验目标

实现一个简化版 `RWSpinLock`，支持以下接口：

```cpp
class RWSpinLock {
public:
    void LockRead();      // 获取读锁（多个读者可同时持有）
    void UnlockRead();    // 释放读锁
    void LockWrite();     // 获取写锁（独占）
    void UnlockWrite();   // 释放写锁
};
```

**简化假设**（降低第一次实现的难度）：
- 暂不支持 `try_lock`（非阻塞尝试）
- 暂不支持锁升级/降级
- 写锁释放后，等待的读者和写者由操作系统调度决定优先级（不用实现公平队列）

---

## 实验引导：分步思考

### Step 0：设计状态编码

`RWSpinLock` 的核心是一颗 `std::atomic<int32_t> bits_`。我们需要用整数的不同位/区域来表示状态。

chaos 源码的编码方案是：
- `READER = 4`：每个读者贡献 +4
- `UPGRADED = 2`：升级锁（本实验暂不实现）
- `WRITER = 1`：写锁

**思考问题**：
- 为什么 READER 是 4 而不是 1？如果 READER = 1，写锁也是 1，会发生什么冲突？
- `bits_ == 0` 表示什么？
- `bits_ == 8` 表示什么？（8 = 2 * READER）

> [!tip] 提示
> 把 READER 定为 4（二进制 `100`）、WRITER 定为 1（二进制 `001`），是为了让读计数和写标志在二进制层面**互不掩盖**。这样你可以通过位运算检查"是否有写锁"而不受读者数量的影响。

### Step 1：实现 LockRead()

读锁的核心逻辑：
1. 原子地增加读者计数（`fetch_add`）
2. 检查增加之后，是否有写锁正在等待或持有
3. 如果有写锁，**回滚**刚才增加的读者计数，然后自旋重试

**代码骨架**：

```cpp
void LockRead() {
    while (true) {
        int32_t value = bits_.fetch_add(READER, std::memory_order_???);
        if (!(value & WRITER)) {
            return;  // 没有写锁，成功获取读锁
        }
        // 有写锁，回滚读者计数
        bits_.fetch_add(-READER, std::memory_order_???);
        // 自旋等待...
    }
}
```

**需要你做决策的地方**：
- `fetch_add(READER, ...)` 应该用什么 `memory_order`？
- `fetch_add(-READER, ...)` 应该用什么 `memory_order`？
- 自旋等待时应该做什么？（`pause`？`yield`？纯空转？）

> [!tip] 提示
> 获取锁时用 `acquire`，释放/回滚时用 `release`。为什么？因为这是经典的"进入临界区 / 离开临界区"模式。

### Step 2：实现 UnlockRead()

这个最简单：原子地减去 READER。

但需要思考：`fetch_add(-READER)` 用什么 memory_order？

> [!tip] 提示
> 读锁释放意味着"本线程对共享数据的访问结束了"，后续线程（可能是写者）需要看到本次读操作的结果。所以用 `release`。

### Step 3：实现 LockWrite()

写锁是独占的。只有在**没有任何读者和写者**时才能获取成功。

**代码骨架**：

```cpp
void LockWrite() {
    while (true) {
        int32_t expected = 0;
        if (bits_.compare_exchange_strong(expected, WRITER,
                std::memory_order_???, std::memory_order_???)) {
            return;
        }
        // CAS 失败，说明有人持锁，自旋等待...
    }
}
```

**需要你做决策的地方**：
- `compare_exchange_strong` 的两个 memory_order 参数分别是什么？
- 为什么这里必须用 CAS，而不能用 `fetch_or` 或 `exchange`？
- 自旋等待时是否需要 TTAS 优化？

> [!tip] 提示
> CAS 的语义是"只有当 bits_ 是我预期的 0 时，才把它设为 WRITER"。这正好对应"没有任何人持锁"的条件。`fetch_or` 会无条件设置位，如果同时有读者进入，写锁和读锁会共存——数据竞争！

### Step 4：实现 UnlockWrite()

写锁释放：把 WRITER 位清零。

> [!warning] 一个常见错误
> 不要写 `bits_.fetch_add(-WRITER)` 或 `bits_ &= ~WRITER`（非原子）。最简单可靠的方式是 `bits_.store(0, std::memory_order_release)`，因为写锁独占时 `bits_` 一定是 1。

### Step 5：验证正确性

写完后，用以下测试用例验证：

**测试 1：读者并发**
```cpp
// 8 个线程各读 100 万次，不应该死锁
```

**测试 2：写者独占**
```cpp
// 4 个线程各写 10 万次，最终的计数器应该恰好是 40 万
```

**测试 3：读写混合**
```cpp
// 4 个读线程 + 2 个写线程同时运行，不应该崩溃或计数错误
```

---

## 参考答案（完成后对照）

<details>
<summary>点击展开完整实现</summary>

```cpp
// flags: -pthread -O2
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

class RWSpinLock {
    static constexpr int32_t READER = 4;
    static constexpr int32_t WRITER = 1;

    std::atomic<int32_t> bits_{0};

public:
    void LockRead() {
        while (true) {
            // 乐观地增加读者计数（acquire：进入临界区）
            int32_t value = bits_.fetch_add(READER, std::memory_order_acquire);
            // 检查是否有写锁正在等待或持有
            if (!(value & WRITER)) {
                return;  // 成功
            }
            // 回滚（release：撤销临界区进入）
            bits_.fetch_add(-READER, std::memory_order_release);

            // 自旋等待，避免忙等
            while (bits_.load(std::memory_order_relaxed) & WRITER) {
                #if defined(__x86_64__)
                __asm__ volatile("pause" ::: "memory");
                #elif defined(__aarch64__)
                __asm__ volatile("yield" ::: "memory");
                #endif
            }
        }
    }

    void UnlockRead() {
        bits_.fetch_add(-READER, std::memory_order_release);
    }

    void LockWrite() {
        while (true) {
            int32_t expected = 0;
            // 只有当 bits_ == 0（无人持锁）时才设为 WRITER
            // acq_rel：同时作为 acquire（看到之前解锁的 release）和 release（让后续操作看到本次写锁）
            if (bits_.compare_exchange_strong(expected, WRITER,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return;
            }
            // TTAS：先只读检查，看到 0 才尝试 CAS
            while (bits_.load(std::memory_order_relaxed) != 0) {
                #if defined(__x86_64__)
                __asm__ volatile("pause" ::: "memory");
                #elif defined(__aarch64__)
                __asm__ volatile("yield" ::: "memory");
                #endif
            }
        }
    }

    void UnlockWrite() {
        bits_.store(0, std::memory_order_release);
    }
};

// === 测试 ===
int main() {
    RWSpinLock lock;
    int sharedCounter = 0;

    // 测试 2：写者独占
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 100'000; ++j) {
                    lock.LockWrite();
                    ++sharedCounter;
                    lock.UnlockWrite();
                }
            });
        }
        for (auto& t : threads) t.join();
        std::cout << "Write-only test: counter = " << sharedCounter
                  << " (expected 400000)\n";
    }

    // 测试 3：读写混合
    {
        RWSpinLock lock2;
        int value = 42;
        std::atomic<int> readSum{0};

        std::vector<std::thread> threads;

        // 4 个读线程
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 100'000; ++j) {
                    lock2.LockRead();
                    readSum += value;  // 读操作
                    lock2.UnlockRead();
                }
            });
        }

        // 2 个写线程
        for (int i = 0; i < 2; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 50'000; ++j) {
                    lock2.LockWrite();
                    value = (value == 42) ? 100 : 42;  // 翻转
                    lock2.UnlockWrite();
                }
            });
        }

        for (auto& t : threads) t.join();
        std::cout << "Mixed test passed: value = " << value << "\n";
    }

    return 0;
}
```

</details>

---

## 和 chaos 实现的对比

你的实现和 chaos 源码的 `RWSpinLock` 核心思想一致，但 chaos 还多了几个工业级细节：

| 特性 | 你的简化版 | chaos 工业版 |
|------|-----------|-------------|
| 读者计数回滚 | `fetch_add(-READER)` | 相同 |
| 写锁抢占 | `compare_exchange_strong` | 相同 |
| 锁升级/降级 | 未实现 | `unlock_and_lock_shared()`、`unlock_and_lock_upgrade()` |
| 升级锁 | 未实现 | `UPGRADED = 2`，阻塞新读者但允许旧读者离开 |
| 公平性 | 无（写者可能饿死） | 写者优先（写锁等待时新读者被阻塞） |
| Holder RAII | 无 | `ReadHolder` / `WriteHolder` / `UpgradedHolder` |

**chaos 的锁升级/降级设计要点**：
- `unlock_and_lock_shared()`：**先加读者计数，再释放写锁**。这样写锁释放的瞬间已经有一个读者存在，不会出现"锁真空期"被其他写线程趁虚而入。
- `unlock_and_lock_upgrade()`：**先获得升级位，再释放写锁**。升级锁持有者是"准写者"，它阻塞新读者进入，但等待已存在的旧读者离开，然后可以升级为完整写锁。

这些设计遵循一条黄金规则：**先获得新状态，再释放旧状态**。这样可以保证状态转换的原子性。

---

## 通过这个实验，你应该带走什么？

1. **CAS 不是自旋锁**：CAS 是一种原子操作（条件写入），自旋锁是用 CAS 实现的一种锁。TLS 和它们完全无关——它是"数据隔离"而不是"同步"。
2. **memory_order 不是玄学**：`acquire` = "我要进入临界区，请确保我看到之前 release 的所有修改"；`release` = "我要离开临界区，请确保我的所有修改对后续 acquire 可见"。
3. **单一原子变量可以编码复杂状态机**：`RWSpinLock` 用 4 字节的 `atomic<int32_t>` 管理三态，这是高性能并发代码的典型模式。

> **下一步**：你的线程池笔记里已经用了 `thread_local` 和 `atomic`。现在带着对 `acquire/release` 和 CAS 的深入理解，重新读一遍 [[Notes/SelfGameEngine/基础工具层/线程池与任务系统]] 的 Work-Stealing 队列实现——你会注意到 `Push` 里的 `atomic_thread_fence(std::memory_order_release)` 和 `Steal` 里的 `compare_exchange_strong(..., seq_cst)` 都是有明确理由的。

---

> [!info] 关联笔记
> - [[atomic——让操作不可分割]] —— `fetch_add` 和 `compare_exchange_strong` 的语法基础
> - [[自旋锁——另一种等待方式]] —— TTAS 优化和 `pause`/`yield` 指令
> - [[Release-Acquire——最实用的跨线程同步]] —— 为什么 acquire/release 配对能保证可见性
> - [[Notes/SelfGameEngine/基础工具层/线程池与任务系统]] —— 工业级 Work-Stealing 中的 atomic + TLS 综合运用
> - [[Game/第二阶段-基础层/platform-源码解析：线程与同步]] —— chaos `RWSpinLock` 的完整工业实现
