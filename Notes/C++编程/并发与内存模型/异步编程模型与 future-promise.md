---
title: 异步编程模型与 future/promise
date: 2026-06-13
tags:
  - C++
  - 并发
  - 异步编程
  - future
  - promise
  - async
aliases:
  - future
  - promise
  - 异步编程
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 异步编程模型与 future/promise

> [!info] 一句话概括
> 当你把任务丢给另一个线程去做时，总得有个办法把「结果」拿回来。`std::promise` 是写结果的那一端，`std::future` 是读结果的那一端；`std::async` 和 `std::packaged_task` 则是帮你把任务和这对通道绑定起来的不同包装方式。

---

## 问题 0：为什么需要异步结果传递？

想象主线程要加载一张 4K 纹理。如果主线程自己调用 `load_texture("hero.png")`，在文件从磁盘读出来的几百毫秒内，游戏画面会完全卡住——因为主线程还在忙着等 IO，没法处理渲染和输入。

更合理的做法是：把加载任务交给一个工作线程，主线程继续跑帧循环。等工作线程加载完了，主线程再拿到那张纹理，更新到渲染管线里。

这里的关键问题是：**工作线程怎么把结果安全地交还给主线程？**

最原始的办法是共享变量：

```cpp
// flags: -O0 -g
std::shared_ptr<Texture> g_texture = nullptr;
std::mutex g_mtx;
std::condition_variable g_cv;
bool g_ready = false;

void worker_load() {
    auto tex = std::make_shared<Texture>("hero.png");
    std::lock_guard<std::mutex> lock(g_mtx);
    g_texture = tex;
    g_ready = true;
    g_cv.notify_one();
}

void main_thread_wait() {
    std::unique_lock<std::mutex> lock(g_mtx);
    g_cv.wait(lock, [] { return g_ready; });
    use_texture(g_texture);
}
```

这种写法能工作，但每一个异步任务都要配一组「共享变量 + 互斥锁 + 条件变量 + 完成标志」。任务多了，代码会变成一盘散沙。

> [!abstract]
> **异步结果传递**要解决的问题是：把「任务在线程间转移」和「结果从工作线程回到调用方」这两件事，封装成一套可复用的接口，而不是每个任务都手写同步原语。

---

## 问题 1：`future` 和 `promise` 是什么关系？

`std::future` 和 `std::promise` 本质上是一条**单向管道**的两端：

- **`std::promise<T>`**：写端。你在工作线程里调用 `promise.set_value(result)`，把结果写进去。
- **`std::future<T>`**：读端。你在主线程里调用 `future.get()`，阻塞地拿到结果；或者调用 `future.wait_for(...)`，非阻塞地检查有没有结果。

```cpp
// flags: -O0 -g
#include <future>
#include <iostream>
#include <thread>

int compute_answer() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 42;
}

int main() {
    std::promise<int> prom;                 // 创建写端
    std::future<int> fut = prom.get_future(); // 创建读端，和 promise 绑定

    std::thread t([p = std::move(prom)]() mutable {
        int result = compute_answer();
        p.set_value(result);                 // 工作线程写结果
    });

    std::cout << "waiting...\n";
    int answer = fut.get();                  // 主线程阻塞等待结果
    std::cout << "answer = " << answer << "\n";

    t.join();
    return 0;
}
```

注意几个关键细节：

1. **一个 promise 只能对应一个 future**。`prom.get_future()` 只能调用一次，第二次会抛异常。
2. **future 不可复制，只能移动**。因为结果只能被一个消费者拿走。
3. **`get()` 只能调用一次**。第二次 `get()` 是未定义行为——结果已经被移走了。
4. **`set_value` 和 `get` 之间是同步的**。一旦 `set_value` 发生，`get` 就会醒来；这背后通常由条件变量实现，但你不需要自己写。

> [!abstract]
> **promise/future 是一对一次性、单生产者、单消费者的结果通道。** promise 在工作线程里写结果，future 在调用方读结果。

---

## 问题 2：`std::async` 是怎么把任务和 future 包起来的？

手动创建 `promise` 再启动线程，还是有些啰嗦。`std::async` 帮你做了三件事：

1. 创建一个 `promise`。
2. 启动一个异步任务去执行你的函数。
3. 返回一个 `future`，已经和内部的 `promise` 绑定好。

```cpp
// flags: -O0 -g
#include <future>
#include <iostream>

int compute_answer() {
    return 42;
}

int main() {
    std::future<int> fut = std::async(std::launch::async, compute_answer);
    std::cout << "answer = " << fut.get() << "\n";
    return 0;
}
```

看起来比手写线程简洁很多。但这里藏着一个大坑：**`std::async` 默认的 launch policy 是 `std::launch::async | std::launch::deferred`**，也就是说，它可能立即开一个线程，也可能直到你调用 `get()` 时才在当前线程同步执行。如果你期望任务一定在另一个线程并发执行（比如为了释放主线程），默认 policy 就是陷阱。

正确做法是显式写 `std::launch::async`：

```cpp
// flags: -O0 -g
#include <future>

auto fut = std::async(std::launch::async, [] { /* ... */ });
```

> [!warning]
> 默认 launch policy 的「可能 deferred」行为，会让你的性能测试和线程安全假设全部失效。除非你真的想要惰性求值，否则永远显式指定 `std::launch::async`。

---

## 问题 3：`std::packaged_task` 是另一种什么包装？

`std::async` 适合「提交一个函数，立刻拿到 future」。但有时候你想**先把任务包装好，稍后再决定交给哪个线程执行**。这就是 `std::packaged_task` 的场景。

`std::packaged_task` 把任何可调用对象包装成一个「可以产生 future 的任务对象」，并且它自己可以被拷贝/移动到线程池队列里：

```cpp
// flags: -O0 -g
#include <future>
#include <iostream>
#include <thread>

int add(int a, int b) {
    return a + b;
}

int main() {
    std::packaged_task<int(int, int)> task(add);   // 把函数签名固定下来
    std::future<int> fut = task.get_future();       // 拿到读端

    std::thread t(std::move(task), 3, 4);           // 把任务移到线程里执行
    std::cout << "3 + 4 = " << fut.get() << "\n";
    t.join();
    return 0;
}
```

和 `std::async` 的关键区别：

| 特性 | `std::async` | `std::packaged_task` |
|------|-------------|---------------------|
| 执行时机 | 调用时启动或 deferred | 你自己决定何时何地执行 |
| 适用场景 | 一次性简单异步调用 | 任务队列、线程池 |
| 灵活性 | 低 | 高 |

> [!abstract]
> **`std::packaged_task` 是任务的「未来结果容器」**：你把函数装进去，把它扔进线程池队列，等线程执行它时，结果会自动写到绑定的 future 里。

---

## 问题 4：异步任务怎么和线程池结合？

Day 66 要手写线程池。线程池的核心是「一个任务队列 + 若干工作线程」。`std::packaged_task` 正好适合当队列里的元素：

```cpp
// flags: -O0 -g -pthread
#include <future>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iostream>

class ThreadPool {
public:
    using Task = std::packaged_task<void()>;

    explicit ThreadPool(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(mtx_);
                        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();  // 执行任务，结果自动写到 future
                }
            });
        }
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;
        std::packaged_task<ReturnType()> task(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> fut = task.get_future();
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.emplace(std::move(task));
        }
        cv_.notify_one();
        return fut;
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};

int main() {
    ThreadPool pool(4);
    auto fut = pool.submit([] { return 42; });
    std::cout << "result = " << fut.get() << "\n";
    return 0;
}
```

这个例子把几个知识点串了起来：

1. **`packaged_task` 作为队列元素**：因为它可以被移动，所以能放进 `std::queue`。
2. **`submit` 返回 future**：调用方拿到 future 后可以去做别的事，稍后再 `get()`。
3. **条件变量做任务等待**：工作线程没任务时阻塞，有任务时被唤醒。关于条件变量和虚假唤醒的细节，参见 [[条件变量与虚假唤醒#什么是虚假唤醒？|条件变量与虚假唤醒]]。
4. **完美转发参数**：`submit` 用万能引用把参数传给 `std::bind`，再包装成无参任务。

> [!tip]
> 这个线程池是教学版，距离工业级还很远。真正的线程池还要考虑任务窃取、任务亲和性、队列容量限制、异常传播等。下一篇笔记会讨论 [[工作窃取队列与线程池设计#工作窃取的基本思想|工作窃取队列与线程池设计]]。

---

## 总结

- **异步结果传递**解决的是「任务交出去之后，结果怎么拿回来」的问题。
- **`std::promise`** 是写端，**`std::future`** 是读端；两者是一对一次性、单消费者通道。
- **`std::async`** 帮你快速创建异步任务并返回 future，但默认 launch policy 可能 deferred，最好显式写 `std::launch::async`。
- **`std::packaged_task`** 更适合线程池：把任务包装成可移动对象，放进队列，执行后自动写结果到 future。
- 手写线程池时，把 `packaged_task` 当队列元素，配合条件变量和 future，是最自然的异步结果传递模型。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 异步资源加载、任务图 | 资源加载任务提交到线程池后返回 `future` 句柄，主线程每帧 `wait_for(0)` 检查完成状态，避免阻塞渲染；任务图用 future-like 句柄表达依赖关系 |
| **UE** | `FAsyncTask`、异步加载系统 | `FAsyncTask` 等封装把任务提交到线程池并返回同步对象；UE 的任务图系统用前置任务计数器 + 事件通知实现类似 future 的依赖链，工业级实现更复杂但底层思想一致 |

> [!note] 关键取舍
> 标准库的 `future/promise` 适合学习和中小型项目。工业引擎通常不会直接暴露 `std::future`，因为开销、异常策略和平台控制都不够精细。但理解「结果通道」的抽象，是设计任务图和异步加载系统的基础。

---

> 相关笔记：
> - [[条件变量与虚假唤醒#手写线程池时要注意什么？|条件变量与虚假唤醒]] — 线程池底层等待-通知机制
> - [[工作窃取队列与线程池设计#为什么需要线程池？|工作窃取队列与线程池设计]] — 工业级线程池的任务队列与负载均衡
> - [[Notes/C++编程/并发与内存模型/原子操作与无锁编程基础|原子操作与无锁编程基础]] — 替代锁队列的高性能方案
