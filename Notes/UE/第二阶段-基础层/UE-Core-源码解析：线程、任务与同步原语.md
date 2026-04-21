---
title: UE-Core-源码解析：线程、任务与同步原语
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - core
  - threading
  - tasks
aliases:
  - UE-Core-线程任务与同步原语
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么 UE 需要三层任务抽象？

现代游戏引擎必须在多核 CPU 上榨取并行性能，但同时要面对：

- **多线程渲染**：Game Thread、Render Thread、RHI Thread 的严格顺序依赖
- **动态负载**：每帧任务数量、依赖关系都不同，无法提前静态划分
- **避免死锁**：在命名线程（如 GameThread）上等待任务时，若该线程空转会大幅降低吞吐
- **跨平台一致性**：Windows、Linux、主机平台的线程 API 差异巨大

UE 的解决方案是**三层任务体系**：
1. **底层**：`LowLevelTasks::FTask` — 调度器原子的状态机
2. **中层**：`UE::Tasks::FTaskBase` / `FBaseGraphTask` — 依赖图 + 命名线程兼容
3. **上层**：`UE::Tasks::Launch`、`TGraphTask`、`Async()`、`TPromise/TFuture` — 开发者友好接口

## What：线程与任务系统的模块地图

### 模块定位

| 子系统 | 核心头文件 |
|--------|-----------|
| 新版任务系统 | `Engine/Source/Runtime/Core/Public/Tasks/Task.h` |
| 任务私有实现 | `Engine/Source/Runtime/Core/Public/Tasks/TaskPrivate.h` |
| 任务图接口 | `Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h` |
| 异步 Future | `Engine/Source/Runtime/Core/Public/Async/Future.h` |
| 异步辅助 | `Engine/Source/Runtime/Core/Public/Async/Async.h` |
| 线程抽象 | `Engine/Source/Runtime/Core/Public/HAL/Thread.h` |
| 可运行线程 | `Engine/Source/Runtime/Core/Public/HAL/RunnableThread.h` |
| 线程管理器 | `Engine/Source/Runtime/Core/Public/HAL/ThreadManager.h` |
| 同步原语 | `Engine/Source/Runtime/Core/Public/HAL/Event.h`、`HAL/PlatformMutex.h` |

### 架构简图

```
┌─────────────────────────────────────────────────────────────┐
│  上层 API                                                    │
│  Launch()  │  TGraphTask::CreateTask()  │  Async()         │
│  TPromise/TFuture                                            │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  中层抽象                                                    │
│  FBaseGraphTask (继承 FTaskBase)  │  UE::Tasks::FTaskEvent  │
│  依赖管理：Prerequisites / Subsequents / Nested             │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  底层调度                                                    │
│  LowLevelTasks::FTask (原子状态机)                           │
│  工作线程池 (Worker Threads)                                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 第 1 层：接口层（What）

### 新版任务系统：UE::Tasks::Launch

> 文件：`Engine/Source/Runtime/Core/Public/Tasks/Task.h`，第 265~302 行

```cpp
namespace UE::Tasks
{
    template<typename TaskBodyType>
    TTask<TInvokeResult_T<TaskBodyType>> Launch(
        const TCHAR* DebugName,
        TaskBodyType&& TaskBody,
        ETaskPriority Priority = ETaskPriority::Normal,
        EExtendedTaskPriority ExtendedPriority = EExtendedTaskPriority::None,
        ETaskFlags Flags = ETaskFlags::None
    );

    template<typename TaskBodyType, typename PrerequisitesCollectionType>
    TTask<...> Launch(
        const TCHAR* DebugName,
        TaskBodyType&& TaskBody,
        PrerequisitesCollectionType&& Prerequisites,
        ...
    );
}
```

`Launch` 是 UE 5.x 主推的异步任务接口，特点：
- 返回 `TTask<ResultType>`，可移动/复制，支持 `Wait()` / `GetResult()`
- 支持前置依赖（Prerequisites），自动构建 DAG
- 支持 `EExtendedTaskPriority::Inline`，允许在当前线程立即执行

### 旧版任务图：TGraphTask

> 文件：`Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`，第 596~707 行

```cpp
template<typename TTask>
class TGraphTask final : public TConcurrentLinearObject<...>, public FBaseGraphTask
{
public:
    class FConstructor
    {
    public:
        template<typename...T>
        inline FGraphEventRef ConstructAndDispatchWhenReady(T&&... Args);
    };

    static FConstructor CreateTask(const FGraphEventArray* Prerequisites = nullptr);
};
```

典型用法：
```cpp
FGraphEventRef Task = TGraphTask<FMyTask>::CreateTask(&Prerequisites)
    .ConstructAndDispatchWhenReady(Args...);
```

### 异步 Future：TPromise / TFuture

> 文件：`Engine/Source/Runtime/Core/Public/Async/Future.h`

```cpp
template<typename ResultType>
class TPromise
{
public:
    void SetValue(ResultType&& InValue);
    void EmplaceValue(Args&&... InArgs);
    TFuture<ResultType> GetFuture();
};

template<typename ResultType>
class TFuture
{
public:
    ResultType Get();
    ResultType Consume();
    bool Wait(FTimespan Timeout) const;
    TFuture Then(Callable&& Func);
    TSharedFuture<ResultType> Share();
};
```

`Async(EAsyncExecution::TaskGraph, []{})` 内部会将 Lambda 包装为 `TAsyncGraphTask`，再通过 `TGraphTask` 提交到任务图。

### 线程模型：FRunnableThread + FThreadManager

> 文件：`Engine/Source/Runtime/Core/Public/HAL/RunnableThread.h`

```cpp
class FRunnableThread
{
public:
    static FRunnableThread* Create(FRunnable* InRunnable, const TCHAR* ThreadName,
        uint32 StackSize = 0, EThreadPriority Priority = TPri_Normal, ...);

    virtual void SetThreadPriority(EThreadPriority NewPriority);
    virtual bool Kill(bool bShouldWait = true);
    virtual void WaitForCompletion();
    static FRunnableThread* GetRunnableThread();  // 通过 TLS 获取当前线程
};
```

> 文件：`Engine/Source/Runtime/Core/Public/HAL/ThreadManager.h`

```cpp
class FThreadManager
{
    TMap<uint32, FRunnableThread*> Threads;
    ...
};
```

`FRunnableThread` 是跨平台线程封装，`FThreadManager` 是全局单例，维护线程 ID 到对象的映射。

### 命名线程：ENamedThreads

> 文件：`Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`，第 54~208 行

```cpp
namespace ENamedThreads
{
    enum Type : int32
    {
        RHIThread,
        GameThread,
        ActualRenderingThread,
        AnyThread = 0xff,

        // 高 16 位编码队列、任务优先级、线程优先级
        MainQueue = 0x000,
        LocalQueue = 0x100,
        NormalTaskPriority = 0x000,
        HighTaskPriority = 0x200,
        NormalThreadPriority = 0x000,
        HighThreadPriority = 0x400,
        BackgroundThreadPriority = 0x800,
    };
}
```

命名线程是 UE 任务系统的核心概念。任务可以指定 `GetDesiredThread()` 返回 `ENamedThreads::GameThread`，确保该任务只在游戏线程执行。

---

## 第 2 层：数据层（How - Structure）

### LowLevelTasks::FTask：原子状态机

> 文件：`Engine/Source/Runtime/Core/Public/Async/Fundamental/Task.h`

```cpp
namespace LowLevelTasks
{
    struct FTask
    {
        alignas(PLATFORM_CACHE_LINE_SIZE) std::atomic<ETaskState> State;
        // PackedData 包含：优先级、执行体指针、用户数据等
        ...
    };
}
```

状态转换：
```
Ready → Scheduled → Running → Completed
```

所有状态转换由调度器通过原子 CAS 操作完成，保证无锁。

### FTaskBase：中层依赖与引用计数

> 文件：`Engine/Source/Runtime/Core/Public/Tasks/TaskPrivate.h`

```cpp
namespace UE::Tasks::Private
{
    class FTaskBase
    {
        // 前置/后置任务列表
        // 嵌套任务引用计数
        // 命名线程兼容翻译
        // 调试名、优先级、Trace ID
        ...
    };
}
```

`FTaskBase` 的关键数据：
- `Prerequisites` / `Subsequents`：动态数组，存储 `TRefCountPtr<FTaskBase>`
- `NestedTasksCount`：原子计数，父任务需等所有嵌套任务完成才算完成
- `DebugName`：编译期条件化，Test/Shipping 构建中会被剔除

### FBaseGraphTask：旧版兼容层

> 文件：`Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`，第 469~593 行

```cpp
class FBaseGraphTask : public UE::Tasks::Private::FTaskBase
{
public:
    explicit FBaseGraphTask(const FGraphEventArray* InPrerequisites)
        : FTaskBase(/*InitRefCount=*/1, false)
    {
        if (InPrerequisites != nullptr)
        {
            AddPrerequisites(*InPrerequisites, false);
        }
        UnlockPrerequisites();
    }
    ...
    void Unlock(ENamedThreads::Type CurrentThreadIfKnown = ENamedThreads::AnyThread)
    {
        TryLaunch(0);
    }
};
```

`FBaseGraphTask` 实质是 `FTaskBase` 的薄包装，额外提供：
- `Unlock()`（旧称 `DispatchSubsequents`）
- `Wait()` 中对 `ENamedThreads::LocalQueue` 的回退处理
- `IsTaskEvent()` 标记

### 同步原语

| 类型 | 文件 | 说明 |
|------|------|------|
| `FEvent` | `HAL/Event.h` | 平台抽象事件，支持 Manual/Auto Reset |
| `FEventRef` | `HAL/Event.h` | RAII 池化事件 |
| `FCriticalSection` | 平台相关 | Windows 下为 `CRITICAL_SECTION` |
| `FMutex` / `FRecursiveMutex` | `HAL/PlatformMutex.h` | 平台转发 |
| `FPooledSyncEvent` | `Async/ParkingLot.h` 相关 | Future 内部使用的轻量同步事件 |

---

## 第 3 层：逻辑层（How - Behavior）

### 调用链 1：Launch 任务的提交与执行

```cpp
auto Task = UE::Tasks::Launch(TEXT("MyTask"), []{ /* work */ });
```

调用链：

```
UE::Tasks::Launch(DebugName, Lambda)
  → TTask<ResultType>.Launch(...)
    → Private::TExecutableTask<Lambda>::Create(...)
      → 分配 FTaskBase 内存（通常来自 TaskGraph 线性分配器）
      → 拷贝/移动 Lambda 到任务体内
      → FTaskBase::Init(DebugName, Priority, ExtendedPriority, Flags)
    → *Pimpl.GetInitReference() = Task  （建立 TTask 与 FTaskBase 的关联）
    → Task->TryLaunch(sizeof(*Task))
      → 检查 Prerequisites 是否已完成
        ├─ 已完成 → LowLevelTasks::Schedule() → 投入工作线程队列
        └─ 未完成 → 等待 Prerequisites 完成后再 Schedule
```

### 调用链 2：Wait 的协作式策略（Retraction）

> 文件：`Engine/Source/Runtime/Core/Public/Tasks/Task.h`，第 76~94 行

```cpp
bool Wait(FTimespan Timeout) const
{
    return !IsValid() || Pimpl->Wait(FTimeout{ Timeout });
}

bool Wait() const
{
    if (IsValid())
    {
        Pimpl->Wait();
    }
    return true;
}
```

`FTaskBase::Wait()` 内部逻辑：

```
FTaskBase::Wait()
  → TryRetractAndExecute(Timeout)
    ├─ 若任务尚未开始执行（State == Ready/Scheduled）
    │    → 将该任务"收回"到当前线程直接执行
    │    → 避免线程阻塞和上下文切换
    └─ 若任务已在其他线程运行
         → 进入阻塞等待
           ├─ 当前线程是命名线程（GameThread）
           │    → 帮该线程处理队列中其他任务（ProcessUntilTasksComplete）
           │    → 防止死锁并提高吞吐
           └─ 当前线程是工作线程
                → 通常直接休眠，等待唤醒
```

这是 UE 任务系统最重要的性能优化之一：**等待任务时先尝试就地执行**，只有在无法收回时才真正阻塞。

### 调用链 3：TGraphTask 与命名线程任务

```cpp
FGraphEventRef Task = TGraphTask<FMyTask>::CreateTask(&Prereqs)
    .ConstructAndDispatchWhenReady(Args...);
```

```
TGraphTask::CreateTask(&Prereqs)
  → FConstructor(Prereqs)
    → ConstructAndDispatchWhenReady(Args...)
      → new TGraphTask(Prereqs)
      → placement new 构造 TMyTask 到 TaskStorage
      → TranslatePriority(TMyTask::GetDesiredThread())
        → 将 ENamedThreads::GameThread 翻译为 ETaskPriority + EExtendedTaskPriority
      → FBaseGraphTask::Init(Pri, ExtPri)
      → Ref->TryLaunch(sizeof(TGraphTask))
```

当任务指定在命名线程（如 `GameThread`）执行时：
- `TryLaunch` 不会将其放入工作线程队列
- 而是标记为"待该命名线程处理"
- 命名线程在自己的 `ProcessThreadUntilIdle` / `ProcessUntilTasksComplete` 循环中取出并执行

### 调用链 4：Async() 与 Future 的线程映射

> 文件：`Engine/Source/Runtime/Core/Public/Async/Async.h`

```cpp
template<typename Callable>
TFuture<...> Async(EAsyncExecution Execution, Callable&& Function, ...);
```

| Execution 值 | 底层映射 | 说明 |
|-------------|---------|------|
| `TaskGraph` | `TAsyncGraphTask` → `TGraphTask` | 投入任务图 |
| `Thread` | `TAsyncRunnable` → `FRunnableThread::Create()` | 创建独立 OS 线程 |
| `ThreadPool` | `TAsyncQueuedWork` → `GThreadPool` | 投入通用线程池 |
| `LargeThreadPool` | `TAsyncQueuedWork` → `GLargeThreadPool` | 投入大任务线程池 |

`TFuture::Get()` 内部会调用 `FFutureState::Wait()`，后者使用 `FPooledSyncEvent` 实现跨线程等待。

---

## 上下层关系

| 上层模块 | 使用方式 |
|---------|---------|
| `RenderCore` / `Renderer` | `ENQUEUE_RENDER_COMMAND` 本质是 `TGraphTask` + `ActualRenderingThread_Local` |
| `Slate` | `FSlateApplication` 在游戏线程 Tick 中处理 Slate 任务队列 |
| `Engine` | `UWorld::Tick` 中大量 `AsyncTask(GameThread, ...)` 调用 |
| `CoreUObject` | `AsyncLoadingThread` 是独立 `FRunnableThread`，与任务图并行 |
| `Animation` | `ParallelFor` 基于任务图将骨骼动画评估拆分为多个子任务 |

---

## 设计亮点与可迁移经验

1. **任务回缩（Retraction）**：`Wait()` 不是傻等，而是先尝试把未执行任务"抢回来"在当前线程执行。这对自研引擎的任务系统极具参考价值。
2. **命名线程 + 工作线程混合模型**：关键逻辑（Game、Render、RHI）绑定到固定线程，杂项计算丢给工作线程池，兼顾顺序正确性与并行度。
3. **旧版 API 的无缝桥接**：`FBaseGraphTask` 继承 `FTaskBase`，`TGraphTask` 继续使用，但底层调度器已替换为新的 `LowLevelTasks`。这种**接口兼容、实现替换**的演进策略降低了大规模重构风险。
4. **编译期剔除调试信息**：`DebugName` 在 Test/Shipping 构建中通过宏完全剔除，保证零运行时开销。
5. **Future 的 Continuation 支持**：`TFuture::Then()` / `Next()` 支持链式异步编程，是现代 C++ 异步风格向游戏引擎的移植。

---

## 关键源码片段

**Launch 任务创建与提交**
> 文件：`Engine/Source/Runtime/Core/Public/Tasks/Task.h`，第 110~154 行

```cpp
template<typename TaskBodyType>
void Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody,
    ETaskPriority Priority = ETaskPriority::Normal,
    EExtendedTaskPriority ExtendedPriority = EExtendedTaskPriority::None,
    ETaskFlags Flags = ETaskFlags::None)
{
    check(!IsValid());
    using FExecutableTask = Private::TExecutableTask<std::decay_t<TaskBodyType>>;
    FExecutableTask* Task = FExecutableTask::Create(DebugName, Forward<TaskBodyType>(TaskBody), Priority, ExtendedPriority, Flags);
    *Pimpl.GetInitReference() = Task;
    Task->TryLaunch(sizeof(*Task));
}
```

**TGraphTask 构造与分发**
> 文件：`Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`，第 613~652 行

```cpp
template<typename...T>
inline FGraphEventRef ConstructAndDispatchWhenReady(T&&... Args)
{
    FGraphEventRef Ref{ ConstructAndHoldImpl(Forward<T>(Args)...) };
    Ref->TryLaunch(sizeof(TGraphTask));
    return Ref;
}
```

**命名线程枚举**
> 文件：`Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h`，第 56~108 行

```cpp
namespace ENamedThreads
{
    enum Type : int32
    {
        RHIThread,
        GameThread,
        ActualRenderingThread,
        AnyThread = 0xff,
        MainQueue = 0x000,
        LocalQueue = 0x100,
        NormalTaskPriority = 0x000,
        HighTaskPriority = 0x200,
        ...
    };
}
```

---

## 关联阅读

- [[UE-Core-源码解析：内存分配器家族]] — 任务图使用的 `TConcurrentLinearObject` 与 `FTaskGraphBlockAllocationTag`
- [[UE-Core-源码解析：委托与事件系统]] — `FSimpleDelegateGraphTask` 如何在任务图中执行委托
- [[UE-RenderCore-源码解析：渲染图与渲染线程]] — `ENQUEUE_RENDER_COMMAND` 的任务图实现
- [[UE-专题：多线程与任务系统]] — 从 Core Tasks 到 RenderCore 到 RHI 的完整任务链路

---

## 索引状态

- **所属 UE 阶段**：第二阶段 - 基础层 / 2.2 内存、线程与任务
- **对应 UE 笔记**：UE-Core-源码解析：线程、任务与同步原语
- **本轮分析完成度**：✅ 已完成三层分析（接口层、数据层、逻辑层）
