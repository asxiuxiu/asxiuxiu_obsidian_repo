---
title: UE-AssetRegistry-源码解析：资产注册与发现
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - asset-registry
aliases:
  - UE-AssetRegistry
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么要理解 AssetRegistry？

在大型项目中，资产数量动辄数十万。UE 不能每次启动都扫描整个 `Content` 目录，也不能在编辑器中漫无目的地遍历磁盘文件。`AssetRegistry` 是 UE 的"资产目录簿"，它维护着所有 `.uasset` 文件的元数据索引（包名、类、Tags、依赖关系、ChunkID 等）。内容浏览器、Cooker、打包工具、Chunk 安装系统都依赖它快速定位和筛选资产。理解 AssetRegistry 的扫描机制、缓存结构和查询接口，是优化编辑器启动速度和打包流程的关键。

## What：AssetRegistry 模块是什么？

`AssetRegistry` 是一个运行时模块，核心职责包括：

1. **资产发现（Discovery）**：后台线程遍历磁盘/ Pak，找出所有需要解析的资产文件。
2. **资产收集（Gather）**：后台线程读取 `.uasset` 文件头，提取 `FPackageFileSummary` 和 AssetRegistryData 段。
3. **资产查询（Query）**：提供 `IAssetRegistry` 接口，支持按路径、类、Tags、依赖关系等多维度过滤查询。
4. **状态持久化（State）**：将扫描结果序列化为 `AssetRegistry.bin`，供运行时快速加载。

### 核心类定位

| 类 | 文件 | 职责 |
|---|---|---|
| `IAssetRegistry` | `Public/AssetRegistry/IAssetRegistry.h` | 全局单例接口，暴露查询、扫描、依赖追踪 API |
| `FAssetData` | `CoreUObject/Public/AssetRegistry/AssetData.h` | 单个资产的元数据结构 |
| `FAssetRegistryImpl` | `Private/AssetRegistryImpl.h` | 实际实现，持有 State + Gatherer |
| `FAssetRegistryState` | `Public/AssetRegistry/AssetRegistryState.h` | 磁盘缓存状态，持久化视图 |
| `FAssetDataGatherer` | `Private/AssetDataGatherer.h` | `FRunnable` 后台线程，读取并解析资产文件头 |
| `FAssetDataDiscovery` | `Private/AssetDataGathererPrivate.h` | 目录遍历子系统，发现待扫描文件 |
| `FPackageReader` | `Internal/AssetRegistry/PackageReader.h` | 基于 `FArchiveUObject` 的包文件读取器 |

## How：AssetRegistry 的三层源码剖析

### 第 1 层：接口层（What）

#### IAssetRegistry 的核心接口

> 文件：`Engine/Source/Runtime/AssetRegistry/Public/AssetRegistry/IAssetRegistry.h`，第 143~300 行（节选）

```cpp
class ASSETREGISTRY_API IAssetRegistry : public FAssetRegistryInterface
{
public:
    // 查询资产
    virtual FAssetData GetAssetByObjectPath(const FSoftObjectPath& ObjectPath, EResolveClass ResolveClass = EResolveClass::Yes) const = 0;
    virtual bool GetAssets(const FARFilter& Filter, TArray<FAssetData>& OutAssetData) const = 0;
    virtual bool GetAssetsByPath(const FName Path, TArray<FAssetData>& OutAssetData, bool bRecursive = false, bool bIncludeOnlyOnDiskAssets = false) const = 0;

    // 依赖与引用
    virtual bool GetDependencies(const FAssetIdentifier& AssetIdentifier, TArray<FAssetDependency>& OutDependencies, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package, const FAssetRegistryDependencyOptions& Options = FAssetRegistryDependencyOptions()) const = 0;
    virtual bool GetReferencers(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutReferencers, UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package, const FAssetRegistryDependencyOptions& Options = FAssetRegistryDependencyOptions()) const = 0;

    // 扫描与发现
    virtual void ScanPathsSynchronous(const TArray<FString>& InPaths, bool bForceRescan = false, bool bIgnoreDenyListScanFilters = false) = 0;
    virtual void ScanFilesSynchronous(const TArray<FString>& InFilePaths, bool bForceRescan = false) = 0;
    virtual void SearchAllAssets(bool bSynchronousSearch) = 0;

    // 生命周期事件
    virtual void AssetCreated(UObject* NewAsset) = 0;
    virtual void AssetDeleted(UObject* DeletedAsset) = 0;
    virtual void AssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath) = 0;

    // 委托事件
    FOnAssetAdded OnAssetAdded;
    FOnAssetsRemoved OnAssetsRemoved;
    FOnFilesLoaded OnFilesLoaded;
    FOnAssetUpdated OnAssetUpdated;
};
```

`IAssetRegistry` 的设计遵循**读写分离**：查询接口是 const/线程安全的（内部通过锁保护），扫描和修改接口则由主线程驱动，后台线程仅负责 Discovery 和 Gather。

#### FAssetData 的元数据结构

> 文件：`Engine/Source/Runtime/CoreUObject/Public/AssetRegistry/AssetData.h`，第 160~210 行

```cpp
USTRUCT(BlueprintType)
struct FAssetData
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category = AssetData, transient)
    FName PackageName;                // /Game/Path/Package

    UPROPERTY(BlueprintReadOnly, Category = AssetData, transient)
    FName PackagePath;                // /Game/Path

    UPROPERTY(BlueprintReadOnly, Category = AssetData, transient)
    FName AssetName;                  // Asset

    UPROPERTY(BlueprintReadOnly, Category = AssetData, transient)
    FTopLevelAssetPath AssetClassPath; // /Script/Engine.StaticMesh

    uint32 PackageFlags = 0;
    FAssetDataTagMapSharedView TagsAndValues;   // 可搜索 Tags
    TSharedPtr<FAssetBundleData> TaggedAssetBundles;
    // ... ChunkIDs、OptionalOuterPath 等
};
```

`FAssetData` 是**轻量元数据结构**，不包含实际的 UObject 或资源数据，只存储"如何找到这个资产"的信息。它使用大量 `FName` 代替 `FString`，显著降低内存占用并加速比较。

### 第 2 层：数据层（How - Structure）

#### 资产扫描的双线程流水线

AssetRegistry 将扫描拆分为 **Discovery** 和 **Gather** 两个后台阶段，由主线程 Tick 消费结果：

```
主线程
  └── ScanPathsSynchronous(InPaths)
        └── 启动 FAssetDataDiscovery（后台线程）
              └── 遍历磁盘，发现 .uasset → 推入待读取队列
        └── 启动 FAssetDataGatherer（后台线程）
              └── FFileReadScheduler 调度并发读取
                    └── FPackageReader 打开 .uasset
                          ├── 读取 FPackageFileSummary
                          ├── 读取 AssetRegistryData 段 → TArray<FAssetData>
                          └── 读取 DependencyData 段 → FPackageDependencyData
        └── 主线程 TickGatherer
              └── GetAndTrimSearchResults()
                    └── 注入 FAssetRegistryState
                          └── 更新索引并广播 OnAssetAdded
```

#### FAssetRegistryState 的索引结构

`FAssetRegistryState` 内部维护多个并行索引，以支持不同类型的查询：

| 索引 | 数据结构 | 用途 |
|---|---|---|
| 包名索引 | `TMap<FName, FAssetData*>` | `GetAssetByPackageName` |
| 路径索引 | `TMap<FName, TArray<FAssetData*>>` | `GetAssetsByPath` |
| 类索引 | `TMap<FTopLevelAssetPath, TArray<FAssetData*>>` | `GetAssetsByClass` |
| Tag 索引 | `TMap<FName, TMap<FName, TArray<FAssetData*>>>` | 按 Tag 键值过滤 |
| 依赖图 | `TMap<FAssetIdentifier, FDependsNode*>` | `GetDependencies` / `GetReferencers` |
| 路径树 | `FPathTree` | 快速判断路径存在性和子路径枚举 |

这些索引在后台 Gather 完成时由主线程一次性批量更新，避免了查询时的频繁加锁。

### 第 3 层：逻辑层（How - Behavior）

#### 资产扫描的完整调用链

以 `ScanPathsSynchronous({"/Game/Characters"})` 为例：

```
IAssetRegistry::ScanPathsSynchronous
  └── FAssetRegistryImpl::ScanPathsSynchronous
        ├── 将路径加入 ScanDirs
        ├── 启动/唤醒 FAssetDataDiscovery
        │     └── IterateDirectoryStat("/Game/Characters")
        │           └── 对每个 .uasset 文件创建 FPathSet 记录
        ├── FAssetDataDiscovery 将结果推入 FResults::FilesToSearch
        └── FAssetDataGatherer 从 FResults 取出文件
              └── FFileReadScheduler::InitiateRead(File)
                    └── 异步读取文件头到内存
              └── FPackageReader::ReadPackageData
                    ├── OpenPackageReader(Buffer)
                    ├── SerializePackageFileSummary
                    ├── SerializeAssetRegistryData → ExtractAssetData
                    └── SerializeDependencyData → ExtractDependencies
        └── 主线程 Tick
              └── FAssetRegistryImpl::TickGatherer
                    └── GetAndTrimSearchResults(Results)
                          └── IngestResultsIntoState(Results)
                                ├── 新增资产 → AddAssetData → 更新所有索引
                                ├── 广播 OnAssetAdded
                                └── 若全部完成 → 广播 OnFilesLoaded
```

#### FARFilter 的编译与查询优化

`FARFilter` 是内容浏览器等上层最常用的查询条件封装。在查询前，它会通过 `CompileFilter()` 被编译为 `FARCompiledFilter`，预计算正则表达式、Tag 条件、类白名单等，从而在执行阶段只需做简单的集合交集运算：

```cpp
// 编译阶段
FARFilter Filter;
Filter.PackagePaths.Add("/Game/Characters");
Filter.ClassPaths.Add("/Script/Engine.StaticMesh");
Filter.bRecursivePaths = true;

// 查询阶段
TArray<FAssetData> Results;
AssetRegistry->GetAssets(Filter, Results);
```

内部执行逻辑：
1. 先按 `PackagePaths` 从路径索引中取出候选集。
2. 若有过滤 `ClassPaths`，从类索引取候选集，与前者求交。
3. 若有 `TagsAndValues` 条件，从 Tag 索引取候选集，继续求交。
4. 最终返回交集结果。

## 上下层关系

| 上层使用者 | 用法 |
|---|---|
| `ContentBrowser` | 按路径/类/Tag 搜索资产 |
| `Cooker` / `UAT` | 生成 `AssetRegistry.bin` 供运行时查询 |
| `ChunkInstall` | `GetAssetAvailability` / `PrioritizeAssetInstall` 控制流式安装 |
| `ReferenceViewer` | `GetDependencies` / `GetReferencers` 绘制依赖图 |
| `DirectoryWatcher` *(Editor)* | 监听文件变化，触发增量扫描 |

| 下层依赖 | 说明 |
|---|---|
| `Core` / `CoreUObject` | 基础类型与 UObject 系统 |
| `PakFile` | Pak 包感知（Chunk 安装状态查询） |
| `DirectoryWatcher` *(Editor)* | 实时监听目录变化 |
| `TargetPlatform` *(Editor)* | 平台相关资产过滤 |

## 设计亮点与可迁移经验

1. **Discovery + Gather 的双线程流水线**：磁盘遍历（IO 密集）和文件头解析（CPU 密集）分离，通过队列解耦，充分利用多核并避免主线程阻塞。
2. **多索引并行加速查询**：同一份资产元数据在不同维度建立冗余索引（包名、路径、类、Tag），是典型的**空间换时间**策略。
3. **轻量 FAssetData 元数据**：不加载实际资源，只保留查找所需的最小信息，使 AssetRegistry 能索引数十万资产而内存可控。
4. **状态持久化（FAssetRegistryState）**：编辑器/Cooker 将扫描结果序列化为二进制缓存，运行时直接加载缓存而无需重新扫描磁盘，显著降低启动时间。

## 关联阅读

- [[UE-CoreUObject-源码解析：Package 与加载]]
- [[UE-PakFile-源码解析：Pak 加载与 VFS]]
- [[UE-Engine-源码解析：World 与 Level 架构]]

## 索引状态

- **所属 UE 阶段**：第三阶段 - 核心层 / 3.2 序列化与数据层
- **对应 UE 笔记**：UE-AssetRegistry-源码解析：资产注册与发现
- **本轮分析完成度**：✅ 已完成全部三层分析
