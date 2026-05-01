---
title: std::thread——创建和控制线程
date: 2026-05-01
tags:
  - 操作系统
  - 并发
  - C++
  - 多线程
aliases:
  - C++ Thread Basics
---

> [[索引|← 返回 并发与多线程索引]]

# std::thread——创建和控制线程

---

## 从一个问题开始：我知道要多线程，但代码怎么写？

上一篇我们理解了"为什么需要多线程"——为了同时处理渲染、物理、音频等任务。但知识到这儿还没法落地：你知道"需要多个工人并行干活"，却不知道**在 C++ 代码里怎么喊出"来个人帮我干这个"**。

C++11 之前，标准库根本没有多线程支持。你只能调用操作系统原生 API：Windows 上用 `CreateThread`，Linux 上用 `pthread_create`，macOS 上又是另一套。代码无法跨平台，而且参数多得让人头疼。

C++11 终于把多线程搬进了标准库。你现在只需要包含一个头文件 `#include <thread>`，就能用同一套代码在 Windows、Linux、macOS 上创建线程。

---

## std::thread："来个人帮我干这个"

`std::thread` 是 C++11 标准库提供的**线程类**。你可以把它理解成一个"招工启事"——你告诉它"去执行这个函数"，操作系统就会真的创建一个线程来运行那个函数。

### 最基本的用法

```cpp
#include <thread>
#include <iostream>

void say_hello() {
    std::cout << "Hello from new thread!\n";
}

int main() {
    std::thread t(say_hello);  // 招工：创建一个新线程，执行 say_hello
    t.join();                   // 等人干完活
    std::cout << "Hello from main thread!\n";
    return 0;
}
```

**编译运行**：
```bash
g++ -std=c++17 first_thread.cpp -o first_thread -pthread
./first_thread
```

**预期输出**（顺序固定）：
```
Hello from new thread!
Hello from main thread!
```

**逐行拆解**：

- `std::thread t(say_hello);` —— 这行代码一执行，就发生了三件事：
  1. 操作系统在内核中创建一个新的线程（分配 TCB、分配栈空间）。
  2. 新线程开始执行 `say_hello()` 函数。
  3. `main` 函数**不会等待**，它立刻继续往下走。

  所以到这一行结束时，你的程序里已经有**两个执行流**在同时跑了。

- `t.join();` —— "我在这里等着，直到 `t` 线程执行完毕"。如果 `say_hello()` 还没执行完，`main` 线程就停在 `join()` 这行不动。

> [!warning] 忘记 join 的后果
> 如果一个 `std::thread` 对象被销毁时还在运行（既没有 `join()` 也没有 `detach()`），程序会直接调用 `std::terminate()` —— 也就是**崩溃**。
> 
> 为什么 C++ 标准要这么严格？因为编译器不知道你到底是"忘了等它"还是"故意不想等"。它宁可让你崩溃，也不愿意默默泄漏一个后台线程。

---

### join() vs detach()：等他还是放他自由？

创建线程后，你有两个选择：

| 选择 | API | 含义 | 适用场景 |
|------|-----|------|---------|
| **等他干完** | `t.join()` | 阻塞当前线程，直到 `t` 结束 | 需要线程的结果，或必须确保它完成才能继续 |
| **放他自由** | `t.detach()` | 把线程交给操作系统，不再管它 | 后台任务（如日志写入、网络心跳），主线程不依赖其结果 |

`detach()` 的比喻：你叫了一个外卖骑手，但你说"你不用回来汇报，送到就行"，然后你继续做自己的事。骑手送完外卖就自己消失了。

```cpp
std::thread t(say_hello);
t.detach();  // 放他自由——主线程继续跑，不等待
// 注意：detach 后不要再操作 t！
```

> [!danger] detach 后的线程是"孤儿"
> 一旦 `detach()`，`std::thread` 对象 `t` 就不再和那个底层线程有关联了。如果你之后对 `t` 调用任何操作（比如再 `join`），行为是未定义的。

---

### 更灵活的创建方式：lambda 和参数传递

上面的例子用的是普通函数，但实践中更常用的是 **lambda**（匿名函数）：

```cpp
#include <thread>
#include <iostream>

int main() {
    int x = 42;

    // 创建一个线程，执行 lambda，并捕获外部变量 x
    std::thread t([x]() {
        std::cout << "Thread got x = " << x << "\n";
    });

    t.join();
    return 0;
}
```

`[x]` 叫做**捕获列表**，意思是"把主线程里的 `x` **拷贝一份**给新线程"。新线程拿到的是 `x` 的副本，两个线程各自独立，互不影响。

如果你想让新线程**修改**主线程的变量，要用引用捕获 `[&x]`：

```cpp
int result = 0;
std::thread t([&result]() {
    result = 42;  // 修改主线程的变量！
});
t.join();
std::cout << result;  // 输出 42
```

> [!warning] 引用捕获的危险
> `[&result]` 让子线程直接操作主线程的变量。如果主线程在子线程还在运行时退出了，`result` 就被销毁了，子线程访问的是野指针。下一篇我们会看到，这种"同时读写"就是**数据竞争**。

除了 lambda，你还可以直接给线程函数传参数：

```cpp
void compute(int start, int end, int* result) {
    int sum = 0;
    for (int i = start; i < end; ++i) sum += i;
    *result = sum;
}

int main() {
    int result = 0;
    std::thread t(compute, 1, 1000, &result);  // 参数依次传给 compute
    t.join();
    std::cout << "Sum = " << result << "\n";  // Sum = 499500
    return 0;
}
```

> [!tip] 传参的坑：引用会"退化"成拷贝
> 如果你传的参数是引用类型（比如 `std::string&`），`std::thread` 的构造函数会默认把它**拷贝**一份传给线程函数。如果你想真的传引用，要用 `std::ref` 包装：
> ```cpp
> std::thread t(func, std::ref(my_string));
> ```

---

## this_thread：查看"我当前是谁"

`<thread>` 头文件还提供了一些查询"当前线程"信息的工具：

```cpp
#include <thread>
#include <iostream>

void whoami() {
    std::cout << "My thread ID: " << std::this_thread::get_id() << "\n";
}

int main() {
    std::cout << "Main thread ID: " << std::this_thread::get_id() << "\n";
    std::thread t(whoami);
    t.join();
    return 0;
}
```

| 工具 | 作用 |
|------|------|
| `std::this_thread::get_id()` | 获取当前线程的唯一 ID |
| `std::this_thread::sleep_for(100ms)` | 让当前线程休眠一段时间 |
| `std::this_thread::yield()` | 主动让出 CPU，提示调度器"我可以被换走" |

`sleep_for` 的用途：模拟耗时操作，或在自旋等待中避免 CPU 空转。

```cpp
using namespace std::chrono_literals;
std::this_thread::sleep_for(100ms);   // 睡 100 毫秒
std::this_thread::sleep_for(2s);      // 睡 2 秒
```

---

## 动手实验

### 实验 1：你的第一个多线程程序

```cpp
// first_thread.cpp
#include <thread>
#include <iostream>

void worker(int id) {
    std::cout << "Worker " << id << " starting\n";
    std::cout << "Worker " << id << " done\n";
}

int main() {
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);

    t1.join();
    t2.join();

    std::cout << "All workers finished\n";
    return 0;
}
```

### 实验 2：join vs detach 的区别

```cpp
// join_vs_detach.cpp
#include <thread>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

void slow_task() {
    std::this_thread::sleep_for(500ms);
    std::cout << "Slow task done\n";
}

int main() {
    // 版本 A：join —— 主线程等待
    {
        std::thread t(slow_task);
        std::cout << "Waiting...\n";
        t.join();
        std::cout << "Join finished\n";
    }

    std::cout << "---\n";

    // 版本 B：detach —— 主线程不等待
    {
        std::thread t(slow_task);
        t.detach();
        std::cout << "Detached, main continues immediately\n";
        std::this_thread::sleep_for(600ms);  // 给后台线程时间完成
    }

    return 0;
}
```

---

## 常见陷阱

| 陷阱 | 现象 | 解决 |
|------|------|------|
| **忘记 join/detach** | `std::thread` 析构时程序崩溃 | `join()` 或 `detach()` 必须在析构前调用 |
| **detach 后操作 thread 对象** | 未定义行为，可能崩溃 | detach 后视 thread 对象为"空壳"，不再使用 |
| **lambda 捕获局部变量引用** | 线程还在跑，局部变量已销毁，访问野指针 | 用值捕获 `[x]`，或确保引用的对象生命周期长于线程 |

---

> **问题链**：现在你会创建线程了。但多个线程同时读写共享数据会出问题——下一篇我们用实际代码演示这个 bug。→ [[数据竞争——多线程的第一个敌人]]

---

> [!info] 延伸阅读
> - [[数据竞争——多线程的第一个敌人]] —— 用代码实际演示多线程的计数器 bug
> - [[进程与线程——为什么需要多线程]] —— 进程、线程与共享内存的概念回顾
