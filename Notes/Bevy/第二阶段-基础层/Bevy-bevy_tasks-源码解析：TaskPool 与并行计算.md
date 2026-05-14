---
title: Bevy-bevy_tasks-源码解析：TaskPool 与并行计算
date: 2026-05-14
tags:
  - bevy-source
  - bevy_tasks
  - concurrency
  - task-pool
aliases:
  - Bevy TaskPool 源码解析
  - bevy_tasks TaskPool
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy全解析主索引]]

Bevy 的 ECS 调度可以把不冲突的 System 并行执行，但并行本身不会凭空发生——需要有人把任务分发到多个 CPU 核心上执行。`bevy_tasks` 就是 Bevy 的**任务执行层**，它封装了一套跨平台的异步任务池，让引擎上层（ECS 调度、Query 并行迭代、资源加载等）无需关心线程细节。

这篇笔记聚焦 `TaskPool` 本身：它如何创建工作线程？如何把异步 Future 丢到池子里自动执行？`scope` 又是怎么做到安全借用非 `'static` 数据的？

---

## 为什么不用 Rayon 或标准线程池？

最 naive 的方案是：每个需要并行的地方手动 `std::thread::spawn`。但游戏引擎里到处是异步逻辑——资源加载要等磁盘和网络、System 之间要按依赖图调度、有些任务还需要在下一帧前完成，有些则可以跨多帧。操作系统线程太重了，频繁创建销毁会把性能吃光。

Rayon 是一个很好的数据并行库，但它的核心模型是「把迭代器拆成工作包塞进线程池」，不太适合「大量细粒度的异步 Future 被自动驱动」的场景。Bevy 需要的是：**一个能自动 poll Future 的线程池**，用户只管 `spawn` 一个 async 块，池子自己负责把它推进到完成。

于是 Bevy 选择了基于 `async-executor` + `async-task` 的异步执行器模型，并在上面封装了 `TaskPool`。

---

## TaskPool 的公共接口

从使用者的角度看，`TaskPool` 只暴露三件事：**创建池子**、**spawn 任务**、**在 scope 里并行执行**。

### 创建与配置

```rust
let pool = TaskPoolBuilder::new()
    .num_threads(4)
    .thread_name("MyPool".to_string())
    .build();
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 30~122 行

`TaskPoolBuilder` 是一个典型的构建器，支持配置线程数、栈大小、线程名，以及线程创建/销毁时的回调。如果不指定线程数，默认使用系统逻辑核心数（通过 `available_parallelism` 获取）。

### spawn：丢一个 Future 进池子

```rust
let task = pool.spawn(async { 42 });
```

`spawn` 接收一个 `Send + 'static` 的 Future，返回一个 `Task<T>`。这个 `Task` 本身也是个 Future，你可以 `await` 它拿结果，也可以 `.detach()` 让它在后台自己跑。最关键的是：**即使你不 poll 这个 `Task`，池子里的工作线程也会自动把它执行完。**

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 551~564 行

对于非 `Send` 的 Future，用 `spawn_local`，它只会在当前线程的本地执行器上运行：

```rust
let task = pool.spawn_local(async { /* !Send */ });
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 566~582 行

### scope：安全借用栈数据做并行

`spawn` 要求 Future 是 `'static`，这意味着你不能直接借用局部变量。但很多时候我们想并行处理一批数据，又需要借用同一块内存。`scope` 就是解决这个问题的：

```rust
let mut x = 0;
pool.scope(|s| {
    s.spawn(async { x = 1; });
    s.spawn(async { x = 2; });
});
// scope 结束后，所有任务都已完成，x 可以安全使用
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 229~318 行

`scope` 的签名用到了高阶生命周期 `for<'scope> FnOnce(&'scope Scope<'scope, 'env, T>)`，这是 Rust 里实现「scope」模式的标准技巧——保证所有在 scope 内 spawn 的任务都在 `scope` 返回前完成，因此编译器允许你借用 `'env` 生命周期的数据。

---

## 核心数据结构

### TaskPool 本身

```rust
pub struct TaskPool {
    executor: Arc<crate::executor::Executor<'static>>,
    threads: Vec<JoinHandle<()>>,
    shutdown_tx: async_channel::Sender<()>,
}
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 134~142 行

- `executor`：真正的异步执行器，来自 `async-executor` crate（通过 `crate::executor::Executor` 做了一层薄包装）。它是 `Send + Sync`，可以被多个线程共享。
- `threads`：工作线程的 `JoinHandle` 列表。
- `shutdown_tx`：一个无界 channel 的发送端。Drop `TaskPool` 时关闭这个 channel，工作线程收到关闭信号后退出循环。

### 工作线程的局部状态

每个工作线程有两样东西挂在 `thread_local!` 上：

```rust
thread_local! {
    static LOCAL_EXECUTOR: LocalExecutor<'static> = const { LocalExecutor::new() };
    static THREAD_EXECUTOR: Arc<ThreadExecutor<'static>> = Arc::new(ThreadExecutor::new());
}
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 144~148 行

- `LOCAL_EXECUTOR`：单线程执行器（`async-executor::LocalExecutor`），只能在本线程 tick。`spawn_local` 的任务就丢到这里。
- `THREAD_EXECUTOR`：一个只能在本线程被 tick 的「线程绑定执行器」。它在 `scope` 里扮演关键角色——让某些任务必须跑在调用 `scope` 的那个线程上。

### Scope 的结构

```rust
pub struct Scope<'scope, 'env: 'scope, T> {
    executor: &'scope Executor<'scope>,
    external_executor: &'scope ThreadExecutor<'scope>,
    scope_executor: &'scope ThreadExecutor<'scope>,
    spawned: &'scope ConcurrentQueue<FallibleTask<Result<T, Box<dyn Any + Send>>>>,
    scope: PhantomData<&'scope mut &'scope ()>,
    env: PhantomData<&'env mut &'env ()>,
}
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 623~635 行

`Scope` 并不拥有这些字段，它只是借用。`spawned` 是一个无界并发队列（`concurrent-queue`），所有在 scope 内 spawn 的任务都会被包装成 `FallibleTask` 塞进这里。`FallibleTask` 是 `async-task` 提供的、能捕获 panic 的任务句柄。

---

## 工作线程的生命周期

`TaskPool` 创建时会启动 `num_threads` 个工作线程。每个线程的入口长这样：

```rust
thread_builder.spawn(move || {
    TaskPool::LOCAL_EXECUTOR.with(|local_executor| {
        if let Some(on_thread_spawn) = on_thread_spawn {
            on_thread_spawn();
        }
        let _destructor = CallOnDrop(on_thread_destroy);
        loop {
            let res = std::panic::catch_unwind(|| {
                let tick_forever = async move {
                    loop {
                        local_executor.tick().await;
                    }
                };
                block_on(ex.run(tick_forever.or(shutdown_rx.recv())))
            });
            if let Ok(value) = res {
                value.unwrap_err(); // 期待收到 Closed 错误，说明 shutdown_tx 被 drop
                break;
            }
        }
    });
});
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 188~213 行

拆解一下这段逻辑：

1. **线程初始化**：执行 `on_thread_spawn` 回调（如果有），并注册 `on_thread_destroy` 在线程退出时调用。
2. **核心循环**：用 `catch_unwind` 包裹，防止一个 panic 把整条工作线程炸掉。
3. **双轨驱动**：`ex.run(tick_forever.or(shutdown_rx.recv()))` 是关键。`ex` 是池子共享的 `async-executor::Executor`，`tick_forever` 让本地执行器一直 tick，`shutdown_rx.recv()` 等关闭信号。两者用 `.or()` 组合—— whichever wakes first。
4. **优雅退出**：当 `TaskPool` 被 drop，`shutdown_tx` 关闭，`shutdown_rx.recv()` 返回 `Closed` 错误，`.or()` 选中它，`unwrap_err()` 确认这是预期中的关闭信号，然后 `break` 退出循环。
5. **panic 恢复**：如果 `catch_unwind` 捕获到 panic，它返回 `Err`，循环继续，线程不会死。

这里有一个巧妙的设计：**工作线程不仅 tick 全局 executor（从共享队列抢任务），还 tick 自己的 `LOCAL_EXECUTOR`**。这意味着 `spawn_local` 的任务即使没有其他线程帮忙，也会被本线程持续推进。

---

## scope 的内部执行策略

`scope` 返回前必须确保所有子任务完成。它的内部通过 `scope_with_executor_inner` 实现，这里有一系列生命周期转换（`unsafe { mem::transmute }`），目的是骗过编译器——因为 Rust 无法静态证明「所有借用的数据都会在函数返回前释放」，但运行时上这是 guaranteed 的。

真正执行任务的是四个 `execute_*` 函数，它们对应四种场景：

| 场景 | 函数 | 行为 |
|------|------|------|
| 全局池 + 外部执行器 + scope 执行器 | `execute_global_external_scope` | tick 全局 executor + external ticker + scope ticker |
| 外部执行器 + scope 执行器 | `execute_external_scope` | tick external + scope ticker |
| 仅全局池 + scope 执行器 | `execute_global_scope` | tick 全局 executor + scope ticker |
| 仅 scope 执行器 | `execute_scope` | tick scope ticker |

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 465~549 行

为什么需要分这么细？因为 `scope` 可以指定任务跑到「调用 scope 的线程」（`spawn_on_scope`）、跑到「外部线程执行器」（`spawn_on_external`，通常是主线程）、或者跑到「全局线程池」（`spawn`）。如果 `external_executor` 和 `scope_executor` 是同一个，就只能 tick 一次，否则会死锁。

这四个函数的结构几乎一样：

```rust
async fn execute_global_scope<'scope, 'ticker, T>(
    executor: &'scope Executor<'scope>,
    scope_ticker: ThreadExecutorTicker<'scope, 'ticker>,
    get_results: impl Future<Output = Vec<T>>,
) -> Vec<T> {
    let execute_forever = async move {
        loop {
            let tick_forever = async {
                loop { scope_ticker.tick().await; }
            };
            let _ = AssertUnwindSafe(executor.run(tick_forever))
                .catch_unwind().await.is_ok();
        }
    };
    get_results.or(execute_forever).await
}
```

> 文件：`crates/bevy_tasks/src/task_pool.rs`，第 512~531 行

`get_results` 是一个 Future，它从 `spawned` 队列中逐个 pop 任务并 `await` 结果。`execute_forever` 则不停地 tick 执行器，让其他任务得以推进。两者用 `.or()` 竞争——一旦 `get_results` 完成（所有任务都返回了），整个 `scope` 就可以返回。

**panic 传播**：每个 scope 任务都被 `AssertUnwindSafe(f).catch_unwind()` 包装。如果任务 panic，`task.await` 会拿到 `Err(payload)`，然后调用 `std::panic::resume_unwind(payload)` 把 panic 抛回调用者。这意味着 `scope` 内的 panic 不会静默吞掉，也不会炸掉工作线程。

---

## ThreadExecutor：让任务绑定到特定线程

`ThreadExecutor` 解决了一个具体问题：有些任务必须跑在特定线程上（比如主线程的渲染提交、Web 上的 JS 事件循环）。它的结构非常简单：

```rust
pub struct ThreadExecutor<'task> {
    executor: Executor<'task>,
    thread_id: ThreadId,
}
```

> 文件：`crates/bevy_tasks/src/thread_executor.rs`，第 43~47 行

你可以从任何线程往里面 `spawn`，但只有创建它的那个线程能拿到 `ThreadExecutorTicker` 来 tick：

```rust
pub fn ticker(&self) -> Option<ThreadExecutorTicker> {
    if thread::current().id() == self.thread_id {
        Some(ThreadExecutorTicker { executor: self, ... })
    } else {
        None
    }
}
```

> 文件：`crates/bevy_tasks/src/thread_executor.rs`，第 76~84 行

在 `scope` 里，`scope_executor` 就是调用 `scope` 的那个线程的 `ThreadExecutor`，`external_executor` 则由调用者传入（默认也是当前线程的）。通过 `spawn_on_scope` 和 `spawn_on_external`，你可以精确控制任务落在哪条线程上执行。

---

## 单线程回退（WASM / no_std）

在 WASM 或不支持多线程的平台上，`bevy_tasks` 会编译成 `single_threaded_task_pool.rs` 中的实现。这时 `TaskPool` 是个空结构体，`spawn` 直接走 `LOCAL_EXECUTOR` 或 `wasm_bindgen_futures::spawn_local`，`scope` 则用 `RefCell<Vec<Option<T>>>` 串行执行所有任务。

> 文件：`crates/bevy_tasks/src/single_threaded_task_pool.rs`，第 83~251 行

这个设计保证了上层代码（ECS 调度、Query 并行迭代等）完全不用关心平台差异——同样的 `ComputeTaskPool::get().spawn(...)` 在多线程平台走线程池，在 WASM 走单线程事件循环。

---

## 与上下层的关系

- **下层依赖**：`bevy_tasks` 的「发动机」是 `async-executor`（多线程执行器）和 `async-task`（任务原语）。Bevy 通过 `crates/bevy_tasks/src/executor.rs` 做了一层薄包装，以便在 `async_executor` feature 关闭时回退到 `edge-executor`。
  > 文件：`crates/bevy_tasks/src/executor.rs`，第 17~25 行

- **上层使用者**：
  - **ECS 多线程调度器**（`MultiThreadedExecutor`）用 `ComputeTaskPool::get()` 来并行执行不冲突的 System。
  - **Query 并行迭代**（`QueryParIter::for_each`）把大量 Entity 拆成 batch，分发到 `ComputeTaskPool`。
  - **资产系统**用 `IoTaskPool` 做文件 IO，`AsyncComputeTaskPool` 做后台资源处理。

这些上层模块的具体分析见各自笔记：
- [[Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]]
- [[Bevy-bevy_ecs-源码解析：Query 与 SystemParam]]
- [[Bevy-bevy_asset-源码解析：AssetServer 与 Handle]]

---

## 设计亮点

1. **Future 自动驱动**：用户不需要手动 poll `Task`，工作线程会自动推进。这比传统线程池 + 回调的模型更符合 Rust 的 async 生态。
2. **scope 的生命周期魔法**：通过 `for<'scope>` 高阶 trait bound + `unsafe` 生命周期转换，在零运行时开销的前提下实现了安全的非 `'static` 借用并行。
3. **panic 隔离**：工作线程用 `catch_unwind` 包裹核心循环，scope 内用 `catch_unwind` 包裹每个任务，既防止单个任务炸掉整个池子，又能把 panic 正确传播给调用者。
4. **单线程透明回退**：一套 API 同时服务多线程桌面平台和单线程 WASM，切换完全由条件编译控制。

---

## 索引状态

- **所属阶段**：第二阶段 2.3 任务与异步（bevy_tasks）
- **对应索引条目**：[[Bevy-bevy_tasks-源码解析：TaskPool 与并行计算]]
- **关联笔记**：[[Bevy-bevy_tasks-源码解析：ComputeTaskPool 与 IO 线程]]（同模块，聚焦全局单例与线程分配策略）
