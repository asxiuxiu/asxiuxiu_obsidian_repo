---
title: UE-Engine-源码解析：Tick 调度与分阶段更新
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Engine Tick 调度
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：Tick 调度与分阶段更新

## Why：为什么要深入理解 Tick 调度？

在 UE 中，几乎所有动态行为都依赖于 Tick 更新。从 Actor 的移动、物理模拟、动画评估到网络同步，都需要在正确的时序下执行。理解 Tick 分组（TickGroup）、依赖系统和任务调度器，是避免时序 bug、优化帧率和实现复杂交互逻辑的关键。

## What：Tick 调度与分阶段更新是什么？

UE 的 Tick 系统由三层构成：
1. **`FTickFunction`**：Tick 函数的抽象基类，承载配置（TickGroup、Prerequisites、线程选项）。
2. **`ETickingGroup`**：Tick 分阶段枚举，决定 Tick 函数在一帧中的相对执行顺序。
3. **`FTickTaskManager`**：Tick 任务调度器，负责解析依赖、分组释放任务、支持并行执行。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/`
- **Build.cs 文件**：`Engine.Build.cs`
- **关键目录**：
  - `Classes/Engine/EngineBaseTypes.h` — `ETickingGroup`、`FTickFunction`
  - `Classes/GameFramework/Actor.h` — `FActorTickFunction`
  - `Classes/Engine/World.h` / `Private/LevelTick.cpp` — `UWorld::Tick`、`UWorld::RunTickGroup`
  - `Private/TickTaskManager.cpp` — `FTickTaskManager`

---

## 接口梳理（第 1 层）

### 核心结构一览

| 结构体/枚举 | 定义位置 | 职责 |
|-------------|----------|------|
| `ETickingGroup` | `EngineBaseTypes.h:83` | Tick 分阶段枚举 |
| `FTickFunction` | `EngineBaseTypes.h:172` | Tick 函数抽象基类 |
| `FActorTickFunction` | `EngineBaseTypes.h:525` | Actor Tick，内部调用 `AActor::TickActor` |
| `FActorComponentTickFunction` | `EngineBaseTypes.h:570` | Component Tick，内部调用 `UActorComponent::ConditionalTick` |

### FTickFunction 关键成员

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/EngineBaseTypes.h`

```cpp
struct FTickFunction
{
    TEnumAsByte<ETickingGroup> TickGroup;      // 最小起始 Tick 组
    TEnumAsByte<ETickingGroup> EndTickGroup;   // 必须结束于的 Tick 组
    uint8 bCanEverTick:1;                      // 是否可注册
    uint8 bStartWithTickEnabled:1;             // 初始是否启用
    uint8 bTickEvenWhenPaused:1;               // 暂停时仍可执行
    uint8 bRunOnAnyThread:1;                   // 是否可在非游戏线程并行执行
    TArray<FTickPrerequisite> Prerequisites;   // 前置依赖列表
};
```

### ETickingGroup 顺序

| 值 | 枚举名 | 说明 |
|----|--------|------|
| 0 | `TG_PrePhysics` | 物理模拟前执行 |
| 1 | `TG_StartPhysics` | 启动物理模拟（特殊组，不可降级） |
| 2 | `TG_DuringPhysics` | 可与物理模拟并行执行 |
| 3 | `TG_EndPhysics` | 结束物理模拟（特殊组，不可降级） |
| 4 | `TG_PostPhysics` | 物理与布料模拟完成后执行 |
| 5 | `TG_PostUpdateWork` | 所有更新工作完成后执行 |
| 6 | `TG_LastDemotable` | 末尾兜底组 |
| 7 | `TG_NewlySpawned` | 每帧在所有组之后反复执行，处理新生成对象 |
| 8 | `TG_MAX` | 边界值 |

---

## 数据结构（第 2 层）

### Tick 状态机

```cpp
enum class ETickState : uint8
{
    Disabled,      // 未启用
    Enabled,       // 已启用，等待执行
    CoolingDown    // 执行后的冷却期
};
```

### Tick 依赖解析

- `FTickFunction::AddPrerequisite(UObject*, FTickFunction&)` 可将另一 Tick 函数设为本函数的前置。
- `FTickTaskManager` 在 `StartFrame` 时解析依赖图，动态计算：
  - `ActualStartTickGroup`：实际可开始的 Tick 组
  - `ActualEndTickGroup`：实际必须结束的 Tick 组
- 若前置任务所在的组更晚，则本函数会被**降级（demote）**到更晚的 TickGroup，但**不会**进入 `TG_StartPhysics` / `TG_EndPhysics`。

---

## 行为分析（第 3 层）

### World Tick 调用链

```
UWorld::Tick(ELevelTick TickType, float DeltaSeconds)
    ├── FWorldDelegates::OnWorldTickStart
    ├── 网络包处理
    ├── NavigationSystem->Tick()
    ├── FTickTaskManagerInterface::Get().StartFrame(...)
    ├── RunTickGroup(TG_PrePhysics)
    ├── RunTickGroup(TG_StartPhysics)
    ├── RunTickGroup(TG_DuringPhysics, false)  // 非阻塞
    ├── RunTickGroup(TG_EndPhysics)
    ├── RunTickGroup(TG_PostPhysics)
    ├── RunTickGroup(TG_PostUpdateWork)
    ├── RunTickGroup(TG_LastDemotable)
    ├── FTickTaskManagerInterface::Get().EndFrame()
    ├── FWorldDelegates::OnWorldPostActorTick
    └── 网络广播
```

### 关键函数 1：UWorld::Tick

> 文件：`Engine/Source/Runtime/Engine/Private/LevelTick.cpp`，第 1477 行附近

```cpp
void UWorld::Tick(ELevelTick TickType, float DeltaSeconds)
{
    FWorldDelegates::OnWorldTickStart.Broadcast(this, TickType, DeltaSeconds);
    // 网络、时间、导航...
    if (bDoingActorTicks)
    {
        FTickTaskManagerInterface::Get().StartFrame(this, DeltaSeconds, TickType, LevelsToTick);
        RunTickGroup(TG_PrePhysics);
        RunTickGroup(TG_StartPhysics);
        RunTickGroup(TG_DuringPhysics, false);
        RunTickGroup(TG_EndPhysics);
        RunTickGroup(TG_PostPhysics);
        // ... TimerManager / Camera / Streaming
        RunTickGroup(TG_PostUpdateWork);
        RunTickGroup(TG_LastDemotable);
        FTickTaskManagerInterface::Get().EndFrame();
    }
    FWorldDelegates::OnWorldPostActorTick.Broadcast(this);
}
```

### 关键函数 2：FTickTaskManager::RunTickGroup

> 文件：`Engine/Source/Runtime/Engine/Private/TickTaskManager.cpp`，第 2115 行附近

```cpp
void FTickTaskManager::RunTickGroup(ETickingGroup Group, bool bBlockTillComplete)
{
    SyncManager->StartTickGroup(Context.World, Group, TicksToManualDispatch);
    TickTaskSequencer.ReleaseTickGroup(Group, bBlockTillComplete, TicksToManualDispatch);
    Context.TickGroup = ETickingGroup(Context.TickGroup + 1);

    if (bBlockTillComplete)
    {
        for (int32 Iter = 0; Iter < 101; ++Iter)
        {
            int32 Num = LevelList[LevelIndex]->QueueNewlySpawned(Context.TickGroup);
            if (Num && Context.TickGroup == TG_NewlySpawned)
                TickTaskSequencer.ReleaseTickGroup(TG_NewlySpawned, true, ...);
            else
                break;
        }
    }
    SyncManager->EndTickGroup(Context.World, Group);
}
```

### 关键函数 3：FActorTickFunction::ExecuteTick

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`，第 370 行附近

```cpp
void FActorTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ...)
{
    if (IsValid(Target))
    {
        Target->TickActor(DeltaTime * Target->CustomTimeDilation, TickType, *this);
    }
}
```

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `Gameplay` | Actor/Component 通过 `PrimaryActorTick` / `PrimaryComponentTick` 注册 Tick |
| `AI` / `Animation` | 动画评估、行为树更新通常注册为特定 TickGroup 的任务 |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `Core` | 提供 `FGraphEvent`、`TFunction` 等任务系统基础 |
| `PhysicsCore` | `TG_StartPhysics` / `TG_EndPhysics` 与物理模拟同步 |

---

## 设计亮点与可迁移经验

1. **TickGroup 分组明确时序**：通过 9 个核心分组，将物理前、物理中、物理后、更新后等阶段严格区分，避免了大量手动排序代码。
2. **依赖驱动的动态降级**：通过 `Prerequisites` 和 `ActualStartTickGroup`，自动处理 Tick 函数之间的时序依赖，无需硬编码优先级值。
3. **并行 Tick 支持**：`bRunOnAnyThread` 允许非关键 Tick 在任务系统上并行执行，显著提升 CPU 利用率。
4. **TG_NewlySpawned 的循环处理**：新生成对象的 Tick 在所有常规组之后反复执行，直到队列为空，保证了 Spawn 后立即生效的需求。

---

## 关键源码片段

### UWorld::RunTickGroup

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/World.h`，第 3314 行

```cpp
UE_API void RunTickGroup(ETickingGroup Group, bool bBlockTillComplete = true);
```

### Actor Tick 注册

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`

```cpp
UPROPERTY(EditDefaultsOnly, Category="Tick")
FActorTickFunction PrimaryActorTick;
```

---

## 关联阅读

- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-Engine-源码解析：Actor 与 Component 模型]]
- [[UE-Engine-源码解析：场景图与变换传播]]
- [[UE-Engine-源码解析：GameFramework 与规则体系]]
- [[UE-Core-源码解析：线程、任务与同步原语]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-Engine-源码解析：Tick 调度与分阶段更新
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17
