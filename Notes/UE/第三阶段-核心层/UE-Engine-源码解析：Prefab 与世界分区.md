---
title: UE-Engine-源码解析：Prefab 与世界分区
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - world-partition
  - level-instance
aliases:
  - UE-Engine-WorldPartition
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引\|UE全解析主索引]])

## Why：为什么要理解 Prefab 与世界分区？

传统 UE 的 Level-SubLevel 流送模型在超大地图（Open World）中遇到了瓶颈：所有 SubLevel 的边界都是手动划分的，编辑器加载整个世界会耗尽内存，运行时流送的粒度也不够精细。**WorldPartition（世界分区）** 是 UE5 为解决这些问题引入的革命性系统，它自动将大世界切分为**Cell（单元格）**，按玩家位置和 Streaming Source 动态流送。**LevelInstance（关卡实例）** 则是 UE5 的 Prefab 机制，允许将一组 Actor 封装为可复用的子关卡实例。理解这两者，是开发开放世界游戏和模块化场景编辑的核心。

## What：WorldPartition 与 LevelInstance 是什么？

### LevelInstance（关卡实例 / Prefab）

`ALevelInstance` 是一个特殊的 Actor，它引用一个子关卡资源（`TSoftObjectPtr<UWorld>`），并在运行时按需加载/卸载该子关卡。它有两种行为模式：

1. **`Partitioned`（旧称 Embedded）**：子关卡中的 Actor 被合并进主 WorldPartition，参与自动分区和流送。
2. **`LevelStreaming`（旧称 Standalone）**：通过 `ULevelStreamingLevelInstance` 独立加载子关卡，适合作为传统 Prefab 使用。

### WorldPartition（世界分区）

`UWorldPartition` 是控制大世界动态流送的**总控器**，核心组件包括：

1. **`UWorldPartitionRuntimeHash`**：抽象基类，定义 Cell 查询接口。
2. **`UWorldPartitionRuntimeSpatialHash`**：默认实现，基于空间哈希（Grid + Cell）管理 Actor 分布。
3. **`UWorldPartitionRuntimeCell`**：流送的最小单元，状态机为 `Unloaded` → `Loaded` → `Activated`。
4. **`UWorldPartitionSubsystem`**：每帧 Tick，收集 Streaming Sources 并驱动 Cell 加载/卸载。

## How：WorldPartition 与 LevelInstance 的三层源码剖析

### 第 1 层：接口层（What）

#### ALevelInstance 的核心接口

> 文件：`Engine/Source/Runtime/Engine/Public/LevelInstance/LevelInstanceActor.h`

```cpp
UCLASS()
class ALevelInstance : public AActor, public ILevelInstanceInterface
{
    UPROPERTY(EditAnywhere, Category = LevelInstance)
    TSoftObjectPtr<UWorld> WorldAsset;

    UPROPERTY(VisibleAnywhere, Category = LevelInstance)
    TObjectPtr<ULevelInstanceComponent> LevelInstanceComponent;
};
```

`ALevelInstance` 本身是一个 Actor，因此拥有完整的位置、旋转、缩放。它的 `WorldAsset` 指向被实例化的子关卡，运行时由 `ULevelInstanceSubsystem` 管理加载和卸载。

#### UWorldPartition 的核心接口

> 文件：`Engine/Source/Runtime/Engine/Public/WorldPartition/WorldPartition.h`，第 140~180 行

```cpp
UCLASS()
class UWorldPartition final : public UObject
{
public:
    void Initialize(UWorld* InWorld, FTransform InTransform);
    void Uninitialize();
    bool IsStreamingEnabled() const;

    // 编辑器核心：根据 ActorDesc 生成分区和 Cell 包
    void GenerateStreaming(...);

    // 运行时注入/移除外部流送对象
    void InjectExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* InObject);
    void RemoveExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* InObject);

    UWorldPartitionRuntimeHash* GetRuntimeHash() const;
    void Tick(float DeltaSeconds);
};
```

#### UWorldPartitionRuntimeHash 的 Cell 查询接口

> 文件：`Engine/Source/Runtime/Engine/Public/WorldPartition/WorldPartitionRuntimeHash.h`

```cpp
UCLASS()
class UWorldPartitionRuntimeHash : public UObject
{
public:
    virtual void ForEachStreamingCellsSources(const FWorldPartitionStreamingSource& StreamingSource, ...) const;
    virtual void ForEachStreamingCellsQuery(...) const;
    virtual void InjectExternalStreamingObject(...) const;

#if WITH_EDITOR
    virtual void GenerateStreaming(...);
    virtual void FlushStreamingContent();
#endif
};
```

### 第 2 层：数据层（How - Structure）

#### WorldPartition 的 Cell 状态机

`UWorldPartitionRuntimeCell` 是流送的最小单元，每个 Cell 都有自己的生命周期：

```
Unloaded
   │ Load()
   ▼
Loaded
   │ Activate()
   ▼
Activated
   │ Deactivate()
   ▼
Loaded
   │ Unload()
   ▼
Unloaded
```

`UWorldPartitionRuntimeLevelStreamingCell` 是 Cell 的默认实现，它内部持有 `UWorldPartitionLevelStreamingDynamic*`，通过标准的关卡流送系统加载 Actor。

#### Spatial Hash 的网格层级

`UWorldPartitionRuntimeSpatialHash` 维护以下数据结构：

| 结构 | 说明 |
|---|---|
| `FSpatialHashRuntimeGrid` | 编辑器配置：CellSize、LoadingRange、Origin、Priority |
| `FSpatialHashStreamingGrid` | 运行时生成的流送网格 |
| `GridLevel -> LayerCell -> GridCells` | 多级分层结构，大体积 Actor 会被提升到更高层级，避免小 Cell 频繁加载卸载 |

每个 Actor 在编辑器生成 `FWorldPartitionActorDesc`（Actor 描述符），描述符中包含 Bounds。`GenerateStreaming` 根据 Bounds 将 Actor 分配到对应的 Grid 和 Cell 中，Cook 时每个 Cell 被打包成独立的 Level 包。

#### LevelInstance 的两种数据路径

| 模式 | 数据路径 | 特点 |
|---|---|---|
| **Partitioned** | `FLevelInstanceActorDesc` → 合并进主 WorldPartition → 参与 Spatial Hash 分配 | 子关卡 Actor 与主世界统一流送 |
| **LevelStreaming** | `ALevelInstance` → `ULevelInstanceSubsystem` → `ULevelStreamingLevelInstance` → 标准流送 | 独立加载/卸载，适合作为 Prefab |

### 第 3 层：逻辑层（How - Behavior）

#### WorldPartition Cell 流送的完整调用链

```
UWorldPartitionSubsystem::Tick(DeltaTime)
  ├── 收集 StreamingSources
  │     ├── 玩家位置（PlayerController）
  │     ├── 摄像机位置
  │     └── LoaderAdapter（如 LevelInstance 的固定范围）
  ├── UWorldPartitionRuntimeHash::ForEachStreamingCellsSources(Sources, ...)
  │     └── UWorldPartitionRuntimeSpatialHash
  │           └── 对每个 Source，计算其 LoadingRange 内的 GridCells
  │                 └── 标记为 ShouldBeLoaded / ShouldBeActivated
  ├── 对比上一帧状态，得到 Delta（CellsToLoad / CellsToUnload / CellsToActivate / CellsToDeactivate）
  ├── 对每个 CellToLoad
  │     └── UWorldPartitionRuntimeLevelStreamingCell::Load()
  │           └── UWorldPartitionLevelStreamingDynamic::Load()
  │                 └── 标准关卡流送系统加载 Cell Level 包
  ├── 对每个 CellToActivate
  │     └── UWorldPartitionRuntimeLevelStreamingCell::Activate()
  │           └── 激活 Cell 中所有 Actor（调用 BeginPlay）
  └── 卸载流程与加载相反
```

#### LevelInstance Standalone 模式的加载调用链

```
ALevelInstance::LoadLevelInstance()
  └── ILevelInstanceInterface::LoadLevelInstance()
        └── ULevelInstanceSubsystem::RequestLoadLevelInstance(this)
              ├── 将实例加入 LevelInstancesToLoadOrUpdate 队列
              ├── 创建 ULevelStreamingLevelInstance
              └── ULevelStreamingLevelInstance::LoadInstance()
                    └── 发起标准关卡流送
                          └── OnLevelInstanceLoaded()
                                └── 子关卡 Actor 被加入世界场景
```

#### ULevelInstanceComponent 的编辑期代理作用

`ULevelInstanceComponent` 继承 `USceneComponent`，但它**只在编辑期有意义**。它的核心作用是作为 `ALevelInstance` RootComponent 的代理：
- 在编辑时管理 `ALevelInstanceEditorInstanceActor` 的变换。
- **避免跨关卡 Attachment**，防止 Undo/Redo、事务系统、蓝图重建时出错。
- 支持 `FWorldPartitionActorFilter`，对 Embedded LevelInstance 做 Actor 过滤。

## 上下层关系

| 上层使用者 | 用法 |
|---|---|
| `LevelEditor` | 编辑大世界、生成 WorldPartition Cell、放置 LevelInstance |
| `WorldPartitionHLOD` | 远距离时加载 HLOD Cell 替代高细节 Cell |
| `DataLayers` | 按数据层开关控制 Cell 的加载策略 |
| `ContentBundles` | 通过 `InjectExternalStreamingObject` 注入外部场景内容 |

| 下层依赖 | 说明 |
|---|---|
| `Engine` | `UWorld`、`ULevel`、`AActor`、`ULevelStreaming` 等核心类 |
| `CoreUObject` | UObject 生命周期和序列化 |

## 设计亮点与可迁移经验

1. **自动分区替代手动 SubLevel**：WorldPartition 将大世界按空间哈希自动切分为 Cell，开发者不再需要手动划分 SubLevel 边界，显著降低大世界制作复杂度。
2. **Cell 状态机实现渐进加载**：`Unloaded → Loaded → Activated` 的三态模型，允许先加载资源到内存再延迟激活 Actor，避免一次性大量 `BeginPlay` 导致的卡顿。
3. **ActorDesc 解耦编辑器与运行时**：编辑器维护 `FWorldPartitionActorDesc`，Cook 时根据描述符生成分区的 Cell 包。运行时不需要编辑器元数据，只加载必要的 Cell。
4. **LevelInstance 的双模式设计**：`Partitioned` 模式融入 WorldPartition 实现开放世界，`Standalone` 模式保持独立流送实现 Prefab 复用，一种机制满足两种需求。

## 关联阅读

- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-Engine-源码解析：Actor 与 Component 模型]]
- [[UE-Engine-源码解析：场景图与变换传播]]
- [[UE-PakFile-源码解析：Pak 加载与 VFS]]

## 索引状态

- **所属 UE 阶段**：第三阶段 - 核心层 / 3.2 序列化与数据层
- **对应 UE 笔记**：UE-Engine-源码解析：Prefab 与世界分区
- **本轮分析完成度**：✅ 已完成全部三层分析
