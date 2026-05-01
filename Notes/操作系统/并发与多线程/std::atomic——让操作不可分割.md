---
title: std::atomic——让操作不可分割
date: 2026-05-01
tags:
  - 操作系统
  - 并发
  - 原子操作
  - C++
aliases:
  - C++ Atomic
---

> [[索引|← 返回 并发与多线程索引]]

# std::atomic——让操作不可分割

---

## 从一个问题开始：能不能让 `++counter` 真的"一步完成"？

上一篇我们亲眼见证了数据竞争的破坏力：两个线程同时 `++counter`，结果总是小于预期。问题的根源在于 `++counter` 在 CPU 层面是"读-改-写"三步，线程切换可能发生在任意两步之间。

自然的想法是：如果 CPU 能提供一条指令，把"读、加 1、写回"打包成不可分割的一步，数据竞争不就消失了吗？

现代 CPU 确实提供了这样的指令。在 x86-64 上叫 `lock xadd`，在 ARM64 上叫 `LDADDAL`。C++11 用 `std::atomic` 把这些硬件能力封装起来，让你不必关心底层架构差异。

---

## std::atomic：这个操作不许被打断

`std::atomic<T>` 是 C++11 提供的模板类，它保证对 `T` 的读、写、加减等操作是**不可分割的（Atomic）**——要么全做完，要么完全没做，不存在"做到一半被插进来"的状态。

### 基本用法

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};  // 声明为原子变量

void increment() {
    for (int i = 0; i < 1'000'000; ++i) {
        counter.fetch_add(1);  // 原子加 1
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";  // 一定是 2000000
    return 0;
}
```

**编译运行**：
```bash
g++ -std=c++17 atomic_counter.cpp -o atomic_counter -pthread
./atomic_counter
```

**预期结果**：`counter` 永远精确等于 2000000。

---

## `fetch_add` 到底做了什么？

普通整数的 `++counter` 分三步执行，但 `std::atomic` 的 `fetch_add` 会生成一条**硬件原子指令**。CPU 保证这条指令执行期间，其他核心无法插手。

`fetch_add` 还会返回**加之前的旧值**：

```cpp
std::atomic<int> counter{10};
int old = counter.fetch_add(5);  // counter 变成 15，old 等于 10
```

如果你不需要旧值，可以直接用运算符重载形式，效果一样：

```cpp
counter++;           // 等价于 counter.fetch_add(1)
counter += 5;        // 等价于 counter.fetch_add(5)
int val = counter;   // 原子读取
```

> [!info] 默认内存序
> 上面这些操作都使用了默认的 `std::memory_order_seq_cst`（顺序一致性）。这是最安全但性能最低的内存序。关于 `memory_order` 的选择，后面会专门讲。在完全理解之前，**默认用 `seq_cst` 永远是对的**。

---

## `compare_exchange`：CAS 操作

除了加减，`std::atomic` 还提供一个更底层的原子操作：**比较并交换（Compare-And-Swap，CAS）**。它是很多无锁数据结构的基石。

CAS 的逻辑像一把"指纹锁"：

```
"如果当前值等于我预期的值，就把它换成新值；否则告诉我当前实际值是多少"
```

```cpp
std::atomic<int> value{100};

int expected = 100;
bool success = value.compare_exchange_strong(expected, 200);
// 如果 value 当前是 100，就换成 200，返回 true
// 如果 value 当前不是 100（比如被别的线程改了），返回 false，并把实际值写入 expected
```

`compare_exchange_strong` 和 `compare_exchange_weak` 的区别：
- **strong**：保证 CAS 操作一定执行（除非真的竞争失败）
- **weak**：允许"假失败"——即使当前值等于预期值，也可能因为某些硬件原因返回失败。但 weak 在某些架构上更快，适合用在循环里。

```cpp
// 用 weak 实现自旋 CAS（推荐模式）
int expected = old_value;
while (!atomic_var.compare_exchange_weak(expected, new_value)) {
    expected = old_value;  // 失败后要重置 expected
}
```

---

## 动手实验

### 实验 1：验证原子操作的必要性

```cpp
// atomic_vs_nonatomic.cpp
#include <thread>
#include <iostream>
#include <atomic>

int bad_counter = 0;              // 非原子——会丢数据
std::atomic<int> good_counter{0}; // 原子——不会丢

void increment_bad() {
    for (int i = 0; i < 1'000'000; ++i) ++bad_counter;
}

void increment_good() {
    for (int i = 0; i < 1'000'000; ++i) ++good_counter;
}

int main() {
    std::thread t1(increment_bad);
    std::thread t2(increment_bad);
    t1.join(); t2.join();
    std::cout << "bad_counter  = " << bad_counter
              << " (expected 2000000)\n";

    std::thread t3(increment_good);
    std::thread t4(increment_good);
    t3.join(); t4.join();
    std::cout << "good_counter = " << good_counter
              << " (expected 2000000)\n";
    return 0;
}
```

**预期结果**：`bad_counter` 显著小于 2000000；`good_counter` 永远等于 2000000。

### 实验 2：用 CAS 实现原子递增

```cpp
// cas_increment.cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};

void cas_increment() {
    for (int i = 0; i < 100'000; ++i) {
        int expected = counter.load();
        while (!counter.compare_exchange_weak(expected, expected + 1)) {
            // 如果 counter 被别的线程改了，expected 会被更新为最新值
            // 循环重试直到成功
        }
    }
}

int main() {
    std::thread t1(cas_increment);
    std::thread t2(cas_increment);
    t1.join(); t2.join();
    std::cout << "counter = " << counter << "\n";
    return 0;
}
```

这个实验展示了 CAS 的用法，也揭示了它的缺点：**竞争激烈时，while 循环会反复重试，浪费 CPU**。对于单纯的计数器，`fetch_add` 比 CAS 高效得多。CAS 的真正价值在于实现复杂的数据结构（如链表、队列）的原子更新。

---

## atomic 的局限：只能保护一个变量

`std::atomic` 很强大，但它只能保证"单个变量的操作是原子的"。如果你的临界区涉及多个变量，atomic 就无能为力了。

```cpp
// 错误示范：用两个 atomic 保护一段逻辑
std::atomic<int> head{0};
std::atomic<int> tail{0};

void enqueue(int value) {
    // 即使 head 和 tail 各自是 atomic，
    // 这两行之间仍然可能被其他线程打断！
    buffer[head] = value;
    head.fetch_add(1);
}
```

在 `buffer[head] = value` 和 `head.fetch_add(1)` 之间，消费者线程可能读到"数据已写入但 head 还没更新"的状态，或者相反。要保护**一段代码**（多个操作组成的整体），你需要**锁**。→ [[互斥锁——保护临界区]]

---

## 小结

| 操作 | 普通变量 | `std::atomic` |
|------|---------|--------------|
| `++counter` | 读-改-写三步，可被中断 | 单条硬件指令，不可分割 |
| 多线程计数 | 结果随机偏小 | 结果永远正确 |
| 适用场景 | 单线程 | 多线程共享的单个变量 |
| 局限 | — | 无法保护多变量组成的逻辑 |

> **问题链**：`std::atomic` 解决了"单个变量操作的原子性"，但现实往往涉及多个变量（如队列的 head 和 tail）。如何保护"一段代码"而不是"一个变量"？→ [[互斥锁——保护临界区]]

---

> [!info] 延伸阅读
> - [[互斥锁——保护临界区]] —— 用锁保护一段代码
> - [[内存序——编译器和CPU如何"欺骗"你]] —— 深入理解 `std::atomic` 的 `memory_order` 参数
