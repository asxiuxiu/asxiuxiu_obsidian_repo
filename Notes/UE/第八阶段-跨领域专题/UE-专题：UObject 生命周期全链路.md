---
title: UE-专题：UObject 生命周期全链路
date: 2026-04-18
tags:
  - ue-source
  - engine-architecture
  - uobject
  - lifecycle
  - cross-cutting
aliases:
  - UE UObject 生命周期全链路
  - UE UObject Lifecycle Pipeline
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-专题：UObject 生命周期全链路

## Why：为什么要梳理 UObject 的完整生命周期？

UObject 是 Unreal Engine 的元对象层，是"没有它就不是 UE"的核心基础设施。从反射、序列化、网络同步到垃圾回收，几乎所有引擎上层功能都建立在 UObject 提供的统一对象模型之上。理解 UObject 从**构造 → 注册 → 初始化 → Tick → 销毁 → GC → 析构**的完整链路，是阅读任何上层模块（Engine、Renderer、Gameplay）的前提，也是避免时序 Bug（如 `BeginPlay` 中访问未初始化组件、`EndPlay` 后资源泄漏）的关键。

---

## What：UObject 生命周期的全景图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          UObject 生命周期全链路                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  【构造阶段】                                                                │
│  StaticAllocateObject → UObjectBase 构造函数 → AddObject → AllocateUObjectIndex│
│       │                              │                                      │
│       ▼                              ▼                                      │
│  GUObjectAllocator.AllocateUObject  GUObjectArray[InternalIndex] = Item     │
│       │                                                                     │
│       ▼                                                                     │
│  ClassConstructor → C++ 子类构造函数 → PostSpawnInitialize / PostLoad        │
│       │                                                                     │
│       ▼                                                                     │
│  【初始化阶段】                                                              │
│  PostInitProperties → InitializeComponents → PostInitializeComponents        │
│       │                                                                     │
│       ▼                                                                     │
│  BeginPlay（若世界已开始 Play）                                              │
│       │                                                                     │
│       ▼                                                                     │
│  【活跃阶段】                                                                │
│  TickActor → Tick(DeltaTime) → ReceiveTick → ProcessLatentActions           │
│       │                                                                     │
│       ▼                                                                     │
│  【销毁触发】                                                                │
│  DestroyActor / MarkAsGarbage / RemoveFromWorld / World Cleanup             │
│       │                                                                     │
│       ▼                                                                     │
│  【Gameplay 结束】                                                           │
│  RouteEndPlay → EndPlay → UninitializeComponents → UnregisterComponents      │
│       │                                                                     │
│       ▼                                                                     │
│  【GC 标记】                                                                 │
│  CollectGarbage → PerformReachabilityAnalysis → 标记 Unreachable            │
│       │                                                                     │
│       ▼                                                                     │
│  【GC 异步销毁】                                                             │
│  ConditionalBeginDestroy → BeginDestroy → IsReadyForFinishDestroy?           │
│       │                              │                                      │
│       ▼                              ▼                                      │
│  ConditionalFinishDestroy → FinishDestroy → ~UObjectBase                    │
│       │                                                                     │
│       ▼                                                                     │
│  【内存回收】                                                                │
│  GUObjectArray.FreeUObjectIndex → FMemory::Free → FMalloc 归还 OS           │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 第 1 层：接口层（What）—— 生命周期的关键 API 与边界

### 1.1 构造与注册接口

| 函数/宏 | 定义位置 | 职责 |
|---------|---------|------|
| `NewObject<T>()` | `UObjectGlobals.h` | 模板入口，封装 `StaticConstructObject_Internal` |
| `StaticConstructObject_Internal` | `UObjectGlobals.cpp` | 构造总控：分配内存 + 调用构造函数 + GC 解锁 |
| `StaticAllocateObject` | `UObjectGlobals.cpp` | 分配原始内存 + 全局注册（名称冲突检测、自动命名） |
| `UObjectBase::AddObject` | `UObjectBase.cpp` | 调用 `GUObjectArray.AllocateUObjectIndex`，加入哈希表 |
| `GENERATED_BODY()` | `.generated.h`（由 UHT 生成） | 注入 `ClassConstructor`、反射元数据、GC Token Stream |

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h`，第 1891~1910 行（NewObject）

### 1.2 初始化接口

| 函数 | 定义位置 | 职责 |
|------|---------|------|
| `PostInitProperties` | `Object.h` | 属性初始化后回调，蓝图中对应 `Construction Script` |
| `InitializeComponents` | `Actor.h` / `ActorComponent.h` | 调用所有组件的 `InitializeComponent` |
| `PostInitializeComponents` | `Actor.h` | 组件初始化完成后回调 |
| `BeginPlay` | `Actor.h` / `ActorComponent.h` | Gameplay 启动回调，注册 Tick |

### 1.3 Tick 接口

| 结构体/函数 | 定义位置 | 职责 |
|-------------|---------|------|
| `FTickFunction` | `EngineBaseTypes.h` | Tick 函数抽象基类，承载 TickGroup、Prerequisites |
| `FActorTickFunction` | `EngineBaseTypes.h` | Actor Tick，内部调用 `AActor::TickActor` |
| `UWorld::Tick` | `LevelTick.cpp` | World 主循环，按 `TG_PrePhysics` → `TG_NewlySpawned` 调度 |
| `AActor::Tick` | `Actor.cpp` | 每帧更新，可被子类重写 |

### 1.4 销毁接口

| 函数 | 定义位置 | 职责 |
|------|---------|------|
| `AActor::Destroy` | `Actor.cpp` | Gameplay 显式销毁入口 |
| `UWorld::DestroyActor` | `LevelActor.cpp` | 实际执行：EndPlay → Unregister → MarkAsGarbage |
| `UObject::ConditionalBeginDestroy` | `Obj.cpp` | GC 触发的异步销毁开始（设置 RF_BeginDestroyed） |
| `UObject::BeginDestroy` | `Obj.cpp` | 虚函数：清 Linker、Rename、子类释放资源 |
| `UObject::IsReadyForFinishDestroy` | `Object.h` | 子类可重写：等待异步清理完成（如 Render Thread fence） |
| `UObject::ConditionalFinishDestroy` | `Obj.cpp` | GC 触发的最终销毁（设置 RF_FinishDestroyed） |
| `UObject::FinishDestroy` | `Obj.cpp` | 虚函数：销毁 UProperty、重置序列号 |

### 1.5 GC 交互接口

| 函数 | 定义位置 | 职责 |
|------|---------|------|
| `MarkAsGarbage` | `UObjectBaseUtility.h` | 设置 `RF_MirroredGarbage` + `InternalFlags::Garbage` |
| `AddToRoot` / `RemoveFromRoot` | `UObjectBaseUtility.h` | 加入/移出根集，阻止/允许 GC 回收 |
| `CollectGarbage` | `GarbageCollection.cpp` | GC 主入口：可达性分析 + Incremental Purge |
| `TryCollectGarbage` | `GarbageCollection.cpp` | 非阻塞尝试获取 GC Lock |

---

## 第 2 层：数据层（How - Structure）—— 状态与内存布局

### 2.1 UObjectBase 内存布局

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h`，第 58~140 行

```cpp
class UObjectBase
{
    EObjectFlags    ObjectFlags;      // 公共对象标志（32-bit）
    int32           InternalIndex;    // GUObjectArray 全局索引
    TNonAccessTrackedObjectPtr<UClass> ClassPrivate;
    FNameAndObjectHashIndex NamePrivate;
    TNonAccessTrackedObjectPtr<UObject> OuterPrivate;
};
```

对象头约 **40 字节**，所有业务数据在子类中扩展。这种"薄基类"设计让对象数组遍历和 GC 扫描非常高效。

### 2.2 双标志系统：EObjectFlags + EInternalObjectFlags

**`EObjectFlags`（Public 标志，32-bit）**：

| 标志 | 含义 |
|------|------|
| `RF_MarkAsRootSet` | 构造时请求加入根集（实际映射到 InternalFlags） |
| `RF_BeginDestroyed` | `ConditionalBeginDestroy` 已调用 |
| `RF_FinishDestroyed` | `ConditionalFinishDestroy` 已调用 |
| `RF_MirroredGarbage` | 逻辑垃圾（与 `EInternalObjectFlags::Garbage` 镜像） |

**`EInternalObjectFlags`（Internal 标志，32-bit）**：

| 标志 | 含义 |
|------|------|
| `PendingConstruction` | 仅调用了 UObjectBase 构造，尚未完成初始化 |
| `RootSet` | 根集，GC 永不回收 |
| `Unreachable` | 可达性分析后标记为不可达 |
| `Garbage` | 逻辑垃圾（与 RF_MirroredGarbage 镜像） |
| `Async` | 存在于非 Game Thread |

**镜像设计**：外部代码通过 `UObject*` 检查 `RF_MirroredGarbage` 无需访问 `FUObjectArray`；GC 内部遍历 `FUObjectItem` 时直接读取 `InternalFlags`，缓存友好。

### 2.3 GUObjectArray 分区模型

```
GUObjectArray (FUObjectItem[])
├─ [0] ~ [ObjLastNonGCIndex]      DisregardForGC 区（常驻区）
│     ├─ UClass、UPackage、UEnum 等静态元数据
│     └─ 永远不经过 GC，生命周期与进程一致
│
└─ [ObjFirstGCIndex] ~ [NumElements-1]   GC 区（动态区）
      ├─ 运行时 NewObject 创建的对象
      └─ 不可达时由 GC 标记并释放
```

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectArray.cpp`，第 230~336 行

### 2.4 FUObjectItem：全局数组的元数据槽

```cpp
struct FUObjectItem
{
    int64 FlagsAndRefCount;   // 高32位 = EInternalObjectFlags，低32位 = RefCount
    UObjectBase* Object;
    int32 SerialNumber;       // WeakObjectPtr 解析用序列号
    int32 ClusterRootIndex;   // GC Cluster 根索引
};
```

- **`InternalIndex`**：O(1) 全局查找，GC 扫描无需指针解引用。
- **`SerialNumber`**：对象复用索引时，保证 `TWeakObjectPtr` 不会误判。
- **`RefCount`**：`TStrongObjectPtr` 的强引用兜底。

### 2.5 Tick 状态机

```cpp
enum class ETickState : uint8
{
    Disabled,      // 未启用
    Enabled,       // 已启用，等待执行
    CoolingDown    // 执行后的冷却期
};
```

Actor 的 `PrimaryActorTick.bCanEverTick` 决定是否注册到 `FTickTaskManager`。被 `MarkAsGarbage` 的 Actor **本帧仍可能继续 Tick**，直到 GC 将其从 TickTaskManager 中移除。

---

## 第 3 层：逻辑层（How - Behavior）—— 完整调用链

### 调用链 1：构造与注册（NewObject → AllocateUObjectIndex）

```
NewObject<AActor>(Outer, Class, Name, Flags)
  └─> StaticConstructObject_Internal(Params)
        ├─> FGCReconstructionGuard GCGuard          // 禁止 GC 在构造期间回收
        ├─> StaticAllocateObject(InClass, InOuter, InName, InFlags)
        │     ├─> 名称冲突检测：StaticFindObjectFastInternal
        │     ├─> 自动命名：MakeUniqueObjectName(InOuter, InClass)
        │     ├─> GUObjectAllocator.AllocateUObject(TotalSize, Alignment, bAllowPermanent)
        │     │     └─> FMemory::Malloc(Size, Alignment)  →  FMalloc
        │     └─> new (Obj) UObjectBase(Class, Flags, InternalFlags, Outer, Name, -1, SerialNumber, RemoteId)
        │           └─> AddObject(InName, InternalFlags, ...)
        │                 ├─> GUObjectArray.AllocateUObjectIndex(this, InternalFlagsToSet, ...)
        │                 │     ├─> DisregardForGC 区 或 GC 区分配 FUObjectItem 槽
        │                 │     └─> 设置 PendingConstruction 标志
        │                 └─> HashObject(this)  // 加入按名哈希表
        └─> (!bRecycledSubobject)
              └─> (*InClass->ClassConstructor)(FObjectInitializer(Result, Params))
                    └─> AActor::AActor(FObjectInitializer)
                          └─> 执行 C++ 构造函数，初始化 RootComponent、PrimaryActorTick 等
```

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 4921~4993 行（StaticConstructObject_Internal）
> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 3575~3870 行（StaticAllocateObject）

### 调用链 2：初始化（SpawnActor → InitializeComponents → BeginPlay）

```
UWorld::SpawnActor<AActor>(Class, Transform, SpawnParameters)
  └─> NewObject<AActor>(...)
  └─> AActor::PostSpawnInitialize(SpawnTransform, Owner, Instigator, ...)
        ├─> FixupNativeActorComponents()          // 修复 Native 组件
        ├─> SetWorldTransform(SpawnTransform)
        ├─> DispatchOnComponentsCreated()          // 通知组件已创建
        ├─> PostActorCreated()                     // 子类可重写
        ├─> ExecuteConstruction()                  // 运行 Blueprint Construction Script
        └─> PostActorConstruction()
              ├─> PreInitializeComponents()
              ├─> InitializeComponents()           // 遍历所有组件：Component->InitializeComponent()
              ├─> PostInitializeComponents()       // 组件初始化完成后回调
              └─> DispatchBeginPlay()              // 若世界已开始 Play
                    └─> BeginPlay()
                          ├─> RegisterAllActorTickFunctions(true)  // 注册 Tick
                          ├─> 遍历 Components：Component->BeginPlay()
                          └─> ReceiveBeginPlay() / OnBeginPlay.Broadcast
```

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`，第 3200~3400 行（PostSpawnInitialize / PostActorConstruction）

**关键时序**：`BeginPlay` 只在 `UWorld::BeginPlay()` 被调用后触发。编辑器中放置的 Actor 在 `InitializeActorsForPlay` 阶段批量触发 `BeginPlay`。

### 调用链 3：Tick 调度（World::Tick → Actor::Tick）

```
UWorld::Tick(ELevelTick TickType, float DeltaSeconds)
  ├─> FWorldDelegates::OnWorldTickStart
  ├─> TickDispatch(DeltaSeconds)                 // 网络包处理
  ├─> FTickTaskManagerInterface::Get().StartFrame(...)
  ├─> RunTickGroup(TG_PrePhysics)
  ├─> RunTickGroup(TG_StartPhysics)
  ├─> RunTickGroup(TG_DuringPhysics, false)
  ├─> RunTickGroup(TG_EndPhysics)
  ├─> RunTickGroup(TG_PostPhysics)
  │     └─> FActorTickFunction::ExecuteTick()
  │           └─> AActor::TickActor(DeltaTime * CustomTimeDilation, TickType, ...)
  │                 └─> AActor::Tick(DeltaSeconds)
  │                       ├─> ReceiveTick() / OnTick.Broadcast
  │                       └─> ProcessLatentActions()  // 延迟动作（Delay、Timeline）
  ├─> RunTickGroup(TG_PostUpdateWork)
  ├─> RunTickGroup(TG_LastDemotable)
  ├─> FTickTaskManagerInterface::Get().EndFrame()
  └─> TickFlush(DeltaSeconds)                    // 网络广播
```

> 文件：`Engine/Source/Runtime/Engine/Private/LevelTick.cpp`，第 1477 行附近（UWorld::Tick）
> 文件：`Engine/Source/Runtime/Engine/Private/TickTaskManager.cpp`，第 2115 行附近（RunTickGroup）

### 调用链 4：显式销毁（DestroyActor → EndPlay → MarkAsGarbage）

```
AActor::Destroy(bool bNetForce, bool bShouldModifyLevel)
  └─> UWorld::DestroyActor(AActor* ThisActor, ...)
        ├─> 权限/网络检查、防递归 (FMarkActorIsBeingDestroyed)
        ├─> IStreamingManager::Get().NotifyActorDestroyed()
        ├─> ThisActor->Destroyed()
        │     └─> RouteEndPlay(EEndPlayReason::Destroyed)
        │           └─> AActor::EndPlay(EEndPlayReason::Destroyed)
        │                 ├─> ReceiveEndPlay / OnEndPlay.Broadcast
        │                 └─> 遍历 Components：Component->EndPlay(EEndPlayReason::Destroyed)
        ├─> 解除 Attach（Detach children / self）
        ├─> ClearComponentOverlaps()
        ├─> 通知 NetDriver::NotifyActorDestroyed()
        ├─> UWorld::RemoveActor()          // 从 Level->Actors 移除（软移除，设为 nullptr）
        ├─> ThisActor->UnregisterAllComponents()
        ├─> ThisActor->MarkAsGarbage()     // 设置 RF_MirroredGarbage + InternalFlags::Garbage
        ├─> ThisActor->MarkComponentsAsGarbage()  // 遍历 OwnedComponents：OnComponentDestroyed(true) + MarkAsGarbage()
        └─> RegisterAllActorTickFunctions(false, true)  // 注销 Tick
```

> 文件：`Engine/Source/Runtime/Engine/Private/LevelActor.cpp`，第 500~700 行（DestroyActor）
> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`，第 2100~2200 行（RouteEndPlay / EndPlay）

### 调用链 5：GC 驱动的异步销毁（CollectGarbage → Purge）

```
CollectGarbage(EObjectFlags KeepFlags, bool bPerformFullPurge)
  └─> AcquireGCLock()                          // 阻止其他线程操作 UObject
  └─> UE::GC::CollectGarbageInternal(KeepFlags, bPerformFullPurge)
        ├─> FRealtimeGC::PerformReachabilityAnalysis(KeepFlags, Options)
        │     ├─> 初始化：所有非根集对象标记为 MaybeUnreachable
        │     ├─> 根集播种：RootSet 对象 + KeepFlags 对象 + FGCObject 引用
        │     ├─> 引用传播：按 UHT 生成的 Token Stream 并行扫描所有 UPROPERTY 引用
        │     └─> 该 Actor 和组件被标记为 Unreachable
        ├─> PostCollectGarbageImpl()
        │     ├─> GatherUnreachableObjects()   // 收集所有不可达对象到 GUnreachableObjects
        │     ├─> ReleaseGCLock()              // 释放全局锁！后续可安全分配 UObject
        │     └─> UnhashUnreachableObjects()
        │           └─> ConditionalBeginDestroy()
        │                 ├─> 设置 RF_BeginDestroyed
        │                 └─> AActor::BeginDestroy()
        │                       ├─> UnregisterAllComponents()
        │                       ├─> Level->Actors.RemoveSingleSwap(this)  // 物理移除
        │                       └─> UObject::BeginDestroy() → 清 Linker、Rename(NAME_None)
        └─> IncrementalPurgeGarbage()
              └─> IncrementalDestroyGarbage()
                    └─> ConditionalFinishDestroy()
                          ├─> 设置 RF_FinishDestroyed
                          ├─> UObject::FinishDestroy()
                          │     ├─> DestroyNonNativeProperties()
                          │     ├─> GUObjectArray.ResetSerialNumber(this)  // WeakPtr 失效
                          │     └─> GUObjectArray.RemoveObjectFromDeleteListeners(this)
                          └─> ~UObjectBase()
                                ├─> UnhashObject()  // 从名称哈希表移除
                                ├─> GUObjectArray.FreeUObjectIndex(this)  // 回收索引
                                └─> FMemory::Free(this) → FMalloc 归还内存
```

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp`，第 6203~6219 行（CollectGarbage）
> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp`，第 6095~6192 行（UnhashUnreachableObjects）
> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp`，第 4782~4870 行（IncrementalDestroyGarbage）

### 调用链 6：Level 切换 / World Cleanup 的批量销毁

```
UWorld::CleanupWorld(bSessionEnded, bCleanupResources, NewWorld)
  └─> CleanupWorldInternal()
        ├─> ClearWorldComponents()           // 遍历所有 Level 调用 ClearLevelComponents()
        ├─> PersistentLevel->CleanupLevel()  // 清 RF_Standalone、TrashPackage
        └─> SubsystemCollection.Deinitialize()

UWorld::RemoveFromWorld(ULevel* Level)       // 流关卡卸载
  ├─> BeginRemoval()
  ├─> IncrementalUnregisterComponents()      // 按时间预算分帧注销组件
  ├─> RouteActorEndPlayForRemoveFromWorld()  // 批量调用 Actor->RouteEndPlay(RemovedFromWorld)
  ├─> Level->ClearLevelComponents()          // 对所有 Actor UnregisterAllComponents()
  ├─> Levels.Remove(Level)
  └─> Level->OwningWorld = nullptr
        → Actor 失去 World 引用 → 下次 GC 被回收
```

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 4500~4700 行（CleanupWorld）
> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 3800~4000 行（RemoveFromWorld）

**关键区别**：流关卡卸载时 **不直接调用 DestroyActor**，而是批量 `RouteEndPlay` + `ClearLevelComponents`，让 Actor 随 Level 失去 `OwningWorld` 后由 **GC 统一回收**。这避免了逐个调用 `DestroyActor` 的开销。

---

## 生命周期状态流转图

```mermaid
stateDiagram-v2
    [*] --> PendingConstruction : StaticAllocateObject
    PendingConstruction --> Alive : ClassConstructor + PostInitProperties
    Alive --> Initializing : InitializeComponents
    Initializing --> Playing : PostInitializeComponents + BeginPlay
    Playing --> Ticking : RegisterTick
    Ticking --> Ticking : Tick(DeltaTime)
    Ticking --> MarkedForDeath : Destroy() / MarkAsGarbage()
    MarkedForDeath --> Ticking : 本帧继续 Tick
    MarkedForDeath --> GCUnreachable : CollectGarbage (Reachability Analysis)
    GCUnreachable --> BeginDestroyed : ConditionalBeginDestroy
    BeginDestroyed --> Finishing : IsReadyForFinishDestroy == true
    Finishing --> Destroyed : ConditionalFinishDestroy
    Destroyed --> Freed : ~UObjectBase + FreeUObjectIndex
    Freed --> [*] : FMemory::Free
    
    Alive --> Rooted : AddToRoot()
    Rooted --> Alive : RemoveFromRoot()
    Rooted --> Freed : ForceDelete / Shutdown
```

---

## 多线程与同步

| 线程 | 生命周期阶段 | 同步方式 |
|------|-------------|----------|
| **Game Thread** | 构造、初始化、Tick、Destroy、MarkAsGarbage | — |
| **GC Thread** | 可达性分析、BeginDestroy、FinishDestroy | `AcquireGCLock()` + `FGCCSyncObject` |
| **Render Thread** | `BeginDestroy` 中投递 `ENQUEUE_RENDER_COMMAND` 释放 GPU 资源 | `IsReadyForFinishDestroy()` 轮询等待 fence |
| **Async Loading** | 异步加载的对象构造（`InternalFlags::Async`） | GC Lock 在可达性分析后释放，允许加载线程分配 UObject |

**关键设计**：
- `CollectGarbage` 开始时获取全局锁，但**在可达性分析后立即释放**。这意味着 `BeginDestroy`、`FinishDestroy`、析构函数中可以安全地调用 `StaticFindObject`、`StaticAllocateObject` 等，而不会死锁。
- `AActor::BeginDestroy` 中调用 `UnregisterAllComponents()`，确保组件在 Render Thread 中的资源被正确释放。
- `UObject::ConditionalFinishDestroy` 重置 `SerialNumber`，所有 `TWeakObjectPtr` 自动失效，避免悬空引用。

---

## 上下层关系

### 上层调用者

| 上层 | 使用方式 |
|------|---------|
| `Engine Gameplay` | `SpawnActor` / `Destroy` / `GetWorld()->DestroyActor()` |
| `UMG / Slate` | `SNew` / `AddChild`（Slate 非 UObject，但持有 UObject 引用） |
| `Editor` | `UFactory` 构造资产、`Delete` 触发 `MarkAsGarbage` |
| `Network` | NetDriver 同步 Actor 生命周期（Spawn/Destroy 复制到客户端） |

### 下层依赖

| 下层 | 作用 |
|------|------|
| `Core / FMalloc` | 提供底层内存分配 |
| `TraceLog` | GC 各阶段插入 TRACE_CPUPROFILER_EVENT_SCOPE |
| `RenderCore / RHI` | `BeginDestroy` 中释放渲染资源 |

---

## 设计亮点与可迁移经验

1. **薄基类 + 全局索引**
   `UObjectBase` 仅 40 字节对象头，`InternalIndex` 实现 O(1) 全局查找。GC 扫描时不解引用指针，而是直接遍历 `FUObjectItem` 数组，缓存友好。

2. **双标志镜像加速**
   `RF_MirroredGarbage` 与 `EInternalObjectFlags::Garbage` 表示同一语义，让外部代码和 GC 内部都能快速检查，无需跨层访问。

3. **两阶段异步销毁**
   `BeginDestroy` / `FinishDestroy` 的分离，允许对象在销毁时释放跨线程资源（如 Render Thread 的 GPU 句柄），而不阻塞 Game Thread。这一模式对任何需要异步清理的对象系统都极具参考价值。

4. **GC 锁的精细粒度**
   GC 只在可达性分析期间持有全局锁，`BeginDestroy/FinishDestroy/析构` 阶段已释放锁。这种"标记阶段串行，清理阶段并行"的设计，既保证了安全性，又避免了长时间阻塞。

5. **Tick 与 GC 的解耦**
   被 `MarkAsGarbage` 的 Actor 本帧仍可能继续 Tick，GC 在下一帧才将其从 `FTickTaskManager` 中移除。这种延迟避免了在 Tick 中修改 Tick 容器导致的迭代器失效。

6. **批量销毁优于逐个销毁**
   流关卡卸载时通过 `RemoveFromWorld` 批量 `RouteEndPlay` + 失去 `OwningWorld`，由 GC 统一回收。这避免了逐个 `DestroyActor` 的 O(N²) 开销。

---

## 关键源码片段

### UObjectBase 构造函数

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectBase.cpp`，第 148~168 行

```cpp
UObjectBase::UObjectBase(UClass* InClass, EObjectFlags InFlags,
    EInternalObjectFlags InInternalFlags, UObject* InOuter, FName InName,
    int32 InInternalIndex, int32 InSerialNumber, FRemoteObjectId InRemoteId)
    : ObjectFlags(InFlags)
    , InternalIndex(INDEX_NONE)
    , ClassPrivate(InClass)
    , OuterPrivate(InOuter)
{
    check(ClassPrivate);
    AddObject(InName, InInternalFlags, InInternalIndex, InSerialNumber, InRemoteId);
}
```

### AActor::RouteEndPlay

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`，第 2100~2150 行

```cpp
void AActor::RouteEndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (!bActorInitialized)
    {
        return;
    }
    EndPlay(EndPlayReason);
    TInlineComponentArray<UActorComponent*> Components;
    GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component->bHasBegunPlay)
        {
            Component->EndPlay(EndPlayReason);
        }
    }
}
```

### GC 后释放内存

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 2800~2900 行（FreeUObjectIndex）

```cpp
void FUObjectArray::FreeUObjectIndex(UObjectBase* Object)
{
    int32 Index = Object->InternalIndex;
    FUObjectItem* ObjectItem = &ObjObjects[Index];
    ObjectItem->Object = nullptr;
    ObjectItem->FlagsAndRefCount = 0;
    ObjAvailableList.Add(Index);  // 索引回收到可用列表
}
```

---

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]] — UObjectBase 内存布局与全局注册机制
- [[UE-CoreUObject-源码解析：GC 与对象生命周期]] — 增量 GC、Cluster、Reachability Analysis 的完整流程
- [[UE-CoreUObject-源码解析：Package 与加载]] — UPackage、FLinkerLoad 与对象加载时序
- [[UE-Engine-源码解析：Actor 与 Component 模型]] — UObject 之上 AActor / UActorComponent 的组合模式
- [[UE-Engine-源码解析：World 与 Level 架构]] — World 创建、PIE 隔离、流关卡卸载
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]] — TickGroup 与依赖系统
- [[UE-专题：内存管理全链路]] — UObject 内存如何从 FMalloc 分配到最终释放

---

## 索引状态

- **所属 UE 阶段**：第八阶段 — 跨领域专题深度解析
- **对应 UE 笔记**：UE-专题：UObject 生命周期全链路
- **本轮分析完成度**：✅ 已完成三层分析（接口层 + 数据层 + 逻辑层 + 多线程交互 + 关联辐射）
- **更新日期**：2026-04-18
