---
title: UE-Engine-源码解析：World 与 Level 架构
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Engine World Level 架构
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：World 与 Level 架构

## Why：为什么要深入理解 World 与 Level？

在 UE 中，`UWorld` 是运行时最核心的容器对象。从加载一个关卡地图到生成 Actor、触发 Gameplay、渲染一帧画面，所有操作都发生在某个 `UWorld` 的上下文中。理解 World 与 Level 的层级关系、创建流程和流送机制，是掌握 UE 场景管理、关卡流送（Level Streaming）和编辑器 PIE 模式的前提。

## What：World 与 Level 架构是什么？

- **`UWorld`**：代表一个完整的"地图/世界"。管理 PersistentLevel、StreamingLevels、渲染场景（`FSceneInterface*`）、物理场景、网络驱动、World Subsystems。
- **`ULevel`**：Actor 容器。保存 Actors 数组、Model（BSP）、LevelScriptActor、WorldSettings。
- **`UWorldComposition`**：World Composition 架构的管理器。维护 Tiles 列表，负责基于视距的 Level 流送和 World Origin Rebase。
- **`FWorldContext`**：引擎层对 UWorld 的"轨道"封装。一个 `FWorldContext` 对应一条 World 生命周期轨道（Editor World、PIE World、Seamless Travel 过渡世界）。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/`
- **Build.cs 文件**：`Engine.Build.cs`
- **核心依赖**：
  - `PublicDependencyModuleNames`：`Core`、`CoreUObject`、`NetCore`、`Slate`、`RenderCore`、`RHI`、`PhysicsCore`、`GameplayTags`、`AudioExtensions`、`Json`、`AssetRegistry`
  - `PrivateDependencyModuleNames`：`Landscape`、`UMG`、`AudioMixer`、`Networking`、`Projects`、`HTTP`
- **关键目录**：
  - `Classes/Engine/World.h`、`Level.h`、`WorldComposition.h`
  - `Private/World.cpp`、`Level.cpp`

---

## 接口梳理（第 1 层）

### 核心头文件一览

| 头文件 | 核心类 | 职责 |
|--------|--------|------|
| `World.h` | `UWorld` | 运行时世界容器，管理 Level、Scene、GameMode、Tick |
| `Level.h` | `ULevel` | Actor 集合容器，保存 Actors、Model、WorldSettings |
| `WorldComposition.h` | `UWorldComposition` | World Composition 的 Tile 网格管理与自动流送 |
| `Engine.h` | `FWorldContext` | 引擎对 World 生命周期的轨道封装 |

### UWorld 核心成员

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/World.h`

```cpp
class ENGINE_API UWorld : public UObject
{
    GENERATED_BODY()
public:
    TObjectPtr<ULevel> PersistentLevel;               // 主关卡
    TArray<TObjectPtr<ULevel>> Levels;                // 所有已加载的 Level
    TArray<TObjectPtr<ULevelStreaming>> StreamingLevels; // 可流送关卡对象
    TObjectPtr<UWorldComposition> WorldComposition;   // World Composition 管理器
    FSceneInterface* Scene;                           // 渲染场景接口
    AGameModeBase* AuthorityGameMode;                 // 服务器 GameMode
    float TimeSeconds;
    float DeltaTimeSeconds;
};
```

### UWorld / ULevel 层级结构

```
UWorld
├── PersistentLevel (ULevel*)
│   ├── Actors[]
│   ├── Model (UModel*)
│   ├── ModelComponents[]
│   ├── LevelScriptActor
│   └── WorldSettings (AWorldSettings*)
├── Levels (TArray<ULevel*>)
│   ├── [0] PersistentLevel
│   └── [1..N] Streaming Levels
├── StreamingLevels (TArray<ULevelStreaming*>)
├── WorldComposition (UWorldComposition*)
│   └── Tiles[] + TilesStreaming[]
└── Scene (FSceneInterface*)
```

---

## 数据结构（第 2 层）

### UWorld 的状态标志

| 标志/字段 | 含义 |
|----------|------|
| `bIsWorldInitialized` | `InitWorld()` 是否已完成 |
| `bActorsInitialized` | `InitializeActorsForPlay()` 是否已完成 |
| `bBegunPlay` | `BeginPlay()` 是否已触发 |
| `WorldType` | `EWorldType::Type`（Game、Editor、PIE、Preview 等） |

### ULevel 的核心数组

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/Level.h`

```cpp
class ENGINE_API ULevel : public UObject
{
    GENERATED_BODY()
public:
    TArray<TObjectPtr<AActor>> Actors;
    TArray<TObjectPtr<UModelComponent>> ModelComponents;
    TObjectPtr<UModel> Model;
    TObjectPtr<AWorldSettings> WorldSettings;
    TObjectPtr<ALevelScriptActor> LevelScriptActor;
    TArray<TObjectPtr<UAssetUserData>> AssetUserData;
};
```

### FWorldContext 的核心字段

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/Engine.h`，第 310~486 行

```cpp
struct FWorldContext
{
    TEnumAsByte<EWorldType::Type> WorldType;
    TObjectPtr<UWorld> ThisCurrentWorld;
    FURL LastURL;
    FURL LastRemoteURL;
    TObjectPtr<UGameViewportClient> GameViewport;
    TObjectPtr<UGameInstance> OwningGameInstance;
    FString PIEInstanceName;
    int32 PIEInstance = INDEX_NONE;
};
```

---

## 行为分析（第 3 层）

### World 创建流程

```
UWorld::CreateWorld(...)
    └── UWorld::InitializeNewWorld(...)
            ├── 创建 PersistentLevel
            ├── 创建 BSP Model
            ├── 生成 AWorldSettings
            └── UWorld::InitWorld(...)
                    ├── InitializeSubsystems()
                    ├── 初始化 Scene、PhysicsScene、Navigation、AISystem
                    ├── Levels.Add(PersistentLevel)
                    ├── ConditionallyCreateDefaultLevelCollections()
                    └── PersistentLevel->InitializeRenderingResources()
```

### 关键函数 1：UWorld::CreateWorld

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 2727 行附近

```cpp
UWorld* UWorld::CreateWorld(EWorldType::Type InWorldType, bool bInformEngineOfWorld, ...)
{
    UPackage* WorldPackage = CreatePackage(...);
    UWorld* NewWorld = NewObject<UWorld>(WorldPackage, TEXT("WorldName"));
    NewWorld->WorldType = InWorldType;
    NewWorld->InitializeNewWorld(IVS, bInSkipInitWorld);
    if (bInformEngineOfWorld)
    {
        GEngine->WorldAdded(NewWorld);
    }
    return NewWorld;
}
```

### 关键函数 2：UWorld::InitializeNewWorld

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 2597 行附近

```cpp
void UWorld::InitializeNewWorld(const InitializationValues IVS, bool bInSkipInitWorld)
{
    PersistentLevel = NewObject<ULevel>(this, TEXT("PersistentLevel"));
    PersistentLevel->Initialize(FURL(nullptr));
    PersistentLevel->Model = NewObject<UModel>(PersistentLevel);
    PersistentLevel->Model->Initialize(nullptr, 1);
    PersistentLevel->OwningWorld = this;

    AWorldSettings* WorldSettings = SpawnActor<AWorldSettings>(GEngine->WorldSettingsClass, SpawnInfo);
    PersistentLevel->SetWorldSettings(WorldSettings);

    if (!bInSkipInitWorld)
    {
        InitWorld(IVS);
    }
    UpdateWorldComponents(...);
}
```

### 关键函数 3：InitializeActorsForPlay

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 5842 行附近

```cpp
void UWorld::InitializeActorsForPlay(const FURL& InURL, bool bResetTime, ...)
{
    UpdateWorldComponents(bRerunConstructionScripts, true, Context);
    for (ULevel* Level : Levels)
    {
        Level->InitializeNetworkActors();
    }
    bActorsInitialized = true;

    if (AuthorityGameMode)
    {
        AuthorityGameMode->InitGame(...);
    }

    for (ULevel* Level : Levels)
    {
        Level->RouteActorInitialize(0); // 0 = process all
    }
}
```

### 关键函数 4：ULevel::RouteActorInitialize

> 文件：`Engine/Source/Runtime/Engine/Private/Level.cpp`，第 3817 行附近

```cpp
enum class ERouteActorInitializationState : uint8
{
    Preinitialize,   // PreInitializeComponents()
    Initialize,      // InitializeComponents() + PostInitializeComponents()
    BeginPlay,       // DispatchBeginPlay()
    Finished
};
```

### 关键函数 5：UWorld::BeginPlay

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 6044 行附近

```cpp
void UWorld::BeginPlay()
{
    for (UWorldSubsystem* WorldSubsystem : WorldSubsystems)
    {
        WorldSubsystem->OnWorldBeginPlay();
    }
    if (AuthorityGameMode)
    {
        AuthorityGameMode->StartPlay();
    }
    FWorldDelegates::OnWorldBeginPlay.Broadcast(this);
}
```

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `UnrealEd` | 编辑器创建/切换 World，管理 PIE 隔离 |
| `GameEngine` | `LoadMap()` 加载关卡，创建/销毁 World |
| `LevelStreaming` | 通过 `StreamingLevels` 动态加载/卸载子关卡 |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `CoreUObject` | `UPackage` 加载， UObject 生命周期管理 |
| `RenderCore` / `RHI` | `FSceneInterface` 分配与渲染 |
| `PhysicsCore` | `PhysicsScene` 初始化 |

---

## 设计亮点与可迁移经验

1. **World 是容器，Level 是集合**：UWorld 不直接存储 Actor，而是通过 `Levels` 数组委托给 ULevel。这种分层设计让 Persistent Level 和 Streaming Level 拥有统一的存储语义。
2. **FWorldContext 的轨道模型**：一个 `FWorldContext` 对应一条 World 生命周期轨道。Editor、PIE、Game 可以并行存在而不互相干扰。
3. **三阶段 Actor 初始化**：`Preinitialize` → `Initialize` → `BeginPlay` 的状态机，保证了组件注册、网络初始化、Gameplay 启动的时序正确性。
4. **WorldComposition 的 Tile 网格**：将大世界拆分为基于距离的 Tile 流送单元，是开放世界引擎处理超大地图的通用模式。

---

## 关键源码片段

### UWorld::InitWorld

> 文件：`Engine/Source/Runtime/Engine/Private/World.cpp`，第 2339 行附近

```cpp
void UWorld::InitWorld(const InitializationValues IVS)
{
    InitializeSubsystems();
    // 初始化 Scene、PhysicsScene、Navigation、AISystem
    Levels.Empty(1);
    Levels.Add(PersistentLevel);
    PersistentLevel->OwningWorld = this;
    PersistentLevel->bIsVisible = true;
    ConditionallyCreateDefaultLevelCollections();
    bIsWorldInitialized = true;
    PersistentLevel->InitializeRenderingResources();
    PersistentLevel->OnLevelLoaded();
    PostInitializeSubsystems();
}
```

---

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]]
- [[UE-CoreUObject-源码解析：Package 与加载]]
- [[UE-Engine-源码解析：Actor 与 Component 模型]]
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]]
- [[UE-Engine-源码解析：场景图与变换传播]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-Engine-源码解析：World 与 Level 架构
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17
