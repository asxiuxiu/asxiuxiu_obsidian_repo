---
title: UE-专题：内存管理全链路
date: 2026-04-18
tags:
  - ue-source
  - engine-architecture
  - memory
  - cross-cutting
aliases:
  - UE 内存管理全链路
  - UE Memory Pipeline
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-专题：内存管理全链路

## Why：为什么要理解 UE 的内存管理全链路？

游戏引擎的内存管理不是单点问题，而是贯穿 CPU、GPU、GC、多线程的系统性工程。UE 的内存子系统按职责分布在 **Core**（底层分配器）、**CoreUObject**（对象级 GC）、**Engine**（World/Actor 容器）、**RenderCore/RHI**（渲染资源与 GPU 内存）四个层级。理解它们如何协同工作，才能回答以下关键问题：

- `NewObject` 分配的内存最终来自哪里？
- GC 回收的 UObject 内存何时真正归还给 OS？
- 渲染线程的 `FMemStack` 与游戏线程的 `FMalloc` 有什么关系？
- GPU 资源（Texture/Buffer）的生命周期如何与 UObject 绑定？

本专题按**全链路视角**，横向打通四个层级的内存管理，提取可迁移的通用工程原理。

---

## What：UE 内存管理的四层架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  第 4 层：GPU 内存（RenderCore / RHI）                                        │
│  ├─ FRHITexture / FRHIBuffer        → GPU 显存（D3D12 Heap / Vulkan Memory）  │
│  ├─ FRHITransientResourceAllocator  → 跨 Pass 别名复用物理内存                  │
│  ├─ FRDGBuilder::CreateTexture      → RenderGraph 逻辑资源 → 延迟绑定 RHI      │
│  └─ FRenderResource::InitRHI        → 游戏线程投递 → 渲染线程创建 GPU 资源       │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 3 层：渲染 CPU 内存（RenderCore）                                         │
│  ├─ FMemStack / FMemStackBase       → 渲染线程线性栈分配（64KB 页）             │
│  ├─ FRDGAllocator                   → RenderGraph 图生命周期专用分配器           │
│  ├─ FRHICmdListBaseLinearAllocator  → RHI 命令列表无锁缓存分配                  │
│  └─ FGlobalDynamicReadBuffer        → 动态 GPU 可读缓冲池化分配                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 2 层：UObject 内存（CoreUObject / Engine）                                │
│  ├─ FUObjectAllocator               → UObject 原始内存分配器                    │
│  ├─ GUObjectArray (FUObjectItem[])  → 全局对象数组 + GC 元数据槽                │
│  ├─ GC 可达性分析 → IncrementalPurge → FreeUObjectIndex                       │
│  └─ AActor/UActorComponent          → 引擎层对象的 Spawn/Destroy 语义           │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 1 层：通用堆内存（Core）                                                  │
│  ├─ FMalloc（Binned2/Binned3/Ansi/Stomp） → 小对象分箱 + TLS 线程缓存           │
│  ├─ FMemory::Malloc/Free/Realloc    → 全局静态入口，转发 GMalloc                │
│  ├─ FMallocThreadSafeProxy          → 装饰器：非线程安全分配器加锁包装           │
│  └─ FLinearAllocator                → 永久线性分配器（DisregardForGC 对象）    │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 第 1 层：接口层（What）—— 各层接口边界

### 1.1 Core 层：FMalloc 家族

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MemoryBase.h`，第 96~140 行

```cpp
class FMalloc : public FUseSystemMallocForNew, public FExec
{
public:
    virtual void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void  Free(void* Original) = 0;
    virtual void SetupTLSCachesOnCurrentThread();
    virtual void ClearAndDisableTLSCachesOnCurrentThread();
    ...
};
```

`FMalloc` 是所有 UE 内存分配的终极源头。生产环境默认使用 `FMallocBinned2` 或 `FMallocBinned3`，核心特性：
- **小对象分箱**：≤32KB（Binned2）或 ≤128KB（Binned3）按固定大小池分配。
- **TLS 线程缓存**：每个工作线程维护 `FPerThreadFreeBlockLists`，热路径无锁。
- **指针元数据外置**：通过 `FPtrToPoolMapping` 哈希反查 PoolIndex，Block 头部零开销。

### 1.2 CoreUObject 层：FUObjectAllocator + GUObjectArray

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectAllocator.h`，第 15~57 行

```cpp
class FUObjectAllocator
{
    UObjectBase* AllocateUObject(int32 Size, int32 Alignment, bool bAllowPermanent);
    void FreeUObject(UObjectBase* Object) const;
};
extern COREUOBJECT_API FUObjectAllocator GUObjectAllocator;
```

`FUObjectAllocator` 是 UObject 的**专属内存接口**。它不直接实现分配算法，而是内部调用 `FMemory::Malloc`（即第 1 层 `FMalloc`），但附加了以下语义：
- **DisregardForGC 对象**（如 `UClass`、`UPackage`）：允许使用 `FLinearAllocator` 永久分配，生命周期与进程一致。
- **动态对象**：走普通堆分配，GC 后通过 `FreeUObject` 归还。

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectArray.h`，第 49~169 行

```cpp
struct FUObjectItem
{
    int64 FlagsAndRefCount;   // 高32位 = EInternalObjectFlags，低32位 = RefCount
    UObjectBase* Object;
    int32 SerialNumber;
    int32 ClusterRootIndex;
};
```

`GUObjectArray` 是**所有 UObject 的注册表**。每个 UObject 对应一个 `FUObjectItem`，GC 遍历时不解引用 `UObject*`，而是直接扫描 `FUObjectItem` 数组，缓存友好。

### 1.3 Engine 层：World / Actor / Component 的容器语义

Engine 层不直接管理内存分配，而是通过 `UWorld::SpawnActor`、`AActor::Destroy`、`UActorComponent::DestroyComponent` 等 API 提供**对象级生命周期语义**。这些 API 内部最终调用 `NewObject`（CoreUObject）和 `MarkAsGarbage`（CoreUObject GC）。

### 1.4 RenderCore / RHI 层：渲染资源内存接口

> 文件：`Engine/Source/Runtime/RenderCore/Public/RenderResource.h`，第 55~120 行

```cpp
class FRenderResource
{
public:
    virtual void InitRHI() = 0;      // 渲染线程：创建 GPU 资源
    virtual void ReleaseRHI() = 0;   // 渲染线程：释放 GPU 资源
    void InitResource();             // 游戏线程：投递初始化命令
    void ReleaseResource();          // 游戏线程：投递释放命令
};
```

`FRenderResource` 是**渲染线程资源的抽象基类**。它的 `InitRHI` / `ReleaseRHI` 只在渲染线程执行，游戏线程通过 `BeginInitResource` / `BeginReleaseResource` 投递命令，避免跨线程直接操作 GPU 资源。

> 文件：`Engine/Source/Runtime/RHI/Public/RHIResources.h`，第 180~260 行

```cpp
class FRHIResource
{
    mutable FThreadSafeCounter NumReferences;  // 原子引用计数
    mutable FRHIResourceHandle VirtualAddress; // GPU 虚拟地址（部分后端）
public:
    uint32 AddRef() const;
    uint32 Release() const;
    bool MarkForDelete();  // RefCount==0 时标记待删除
};
```

`FRHIResource` 是所有 GPU 资源（Texture、Buffer、Shader、PipelineState）的根基类。采用**引用计数 + 延迟批量删除**：`Release()==0` 时只标记 `MarkedForDelete`，由 `FRHICommandListExecutor::DeleteResources` 在渲染线程安全点统一销毁。

---

## 第 2 层：数据层（How - Structure）—— 各层核心数据结构

### 2.1 Core 层：Binned 分配器的数据结构

| 结构体 | 核心字段 | 说明 |
|--------|---------|------|
| `FBundleNode` | `NextNodeInCurrentBundle:48, Count:8` | 空闲块链表节点，零额外开销 |
| `FPerThreadFreeBlockLists` | `FFreeBlockList FreeLists[NUM_SMALL_POOLS]` | TLS 线程缓存，热路径无锁 |
| `FPtrToPoolMapping` | `PtrToPoolPageBitShift, HashKeyShift, PoolMask` | 指针 → Pool 元数据的哈希映射 |

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 187~263 行

### 2.2 CoreUObject 层：UObject 内存布局与 GC 分区

**UObjectBase 对象头（约 40 字节）**：

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| +0 | `ObjectFlags` | `EObjectFlags` (int32) | 公共对象标志 |
| +4 | `InternalIndex` | `int32` | GUObjectArray 全局索引 |
| +8 | `ClassPrivate` | `ObjectPtr` | 指向 UClass |
| +16 | `NamePrivate` | `FName` / Union | 对象逻辑名 |
| +24 | `OuterPrivate` | `ObjectPtr` | 外层容器指针 |

**GUObjectArray 分区模型**：
- **DisregardForGC 区**（`0` ~ `ObjLastNonGCIndex`）：引擎启动时加载的静态类、包、原生对象，永远不经过 GC。
- **GC 区**（`ObjFirstGCIndex` ~ `NumElements-1`）：运行时动态创建的对象，由 GC 可达性分析管理。

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectArray.cpp`，第 230~336 行

### 2.3 RenderCore 层：渲染线程内存结构

| 结构体/类 | 核心字段 | 说明 |
|-----------|---------|------|
| `FMemStackBase` | `FMemStackBase::FPage* TopPage` | 线性栈分配器，64KB 页，FMemMark 批量回滚 |
| `FRDGAllocator` | `FMemStackBase* MemStack` | RenderGraph 专用，默认底层用 FMemStackBase |
| `FRDGTexture` | `FRHITexture* PooledTexture, FRDGTextureDesc Desc` | 逻辑资源，执行时绑定到池化 RHI 对象 |
| `FRDGPooledBuffer` | `FRHIBuffer* Buffer, uint32 RefCount` | 池化 Buffer，跨图复用 |

### 2.4 RHI 层：GPU 资源与 Transient 内存

| 结构体/类 | 核心字段 | 说明 |
|-----------|---------|------|
| `FRHITextureDesc` | `Extent, Format, NumMips, Flags` | 纹理描述符，不含 GPU 内存句柄 |
| `FRHIBufferDesc` | `Size, Stride, Usage` | 缓冲描述符 |
| `FRHITransientTexture` | `FRHITransientAllocationFences Fences` | 基于 Fence 的 GPU 内存别名对象 |
| `FRHITransientHeap` | `ID3D12Heap* / VkDeviceMemory` | 平台后端物理内存堆 |

**Transient 资源别名机制**：同一物理 GPU 内存可在不同 Render Pass 间复用，通过 `FRHITransientAllocationFences` 追踪 Graphics / AsyncCompute 管道的生命周期，确保读写不会冲突。

---

## 第 3 层：逻辑层（How - Behavior）—— 关键调用链

### 调用链 1：`NewObject<AActor>` 的内存全链路

```
UWorld::SpawnActor<AActor>()
  └─> NewObject<AActor>(Outer=World, Class=AActor::StaticClass())
        └─> StaticConstructObject_Internal(Params)
              ├─> StaticAllocateObject(InClass, InOuter, InName, InFlags)
              │     ├─> GUObjectAllocator.AllocateUObject(TotalSize, Alignment, bAllowPermanent)
              │     │     └─> FMemory::Malloc(Size, Alignment)  →  GMalloc->Malloc()
              │     │           └─> TMallocBinnedCommon::Malloc
              │     │                 ├─ 小对象：FPerThreadFreeBlockLists::Malloc(PoolIndex) → PopFromFront（无锁）
              │     │                 └─ 大对象：OS 分配（VirtualAlloc / mmap）
              │     └─> new (Obj) UObjectBase(...) → AddObject() → GUObjectArray.AllocateUObjectIndex()
              │           └─> 分配 FUObjectItem 槽，设置 InternalFlags::PendingConstruction
              └─> (*InClass->ClassConstructor)(FObjectInitializer(Result, Params))
                    └─> AActor::AActor(FObjectInitializer) → 执行 C++ 构造函数
```

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 3575~3870 行（StaticAllocateObject）
> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectArray.cpp`，第 230~336 行（AllocateUObjectIndex）

**关键结论**：`SpawnActor` 的物理内存来自 `FMalloc`（通常是 Binned2/3），但 UObject 语义由 `FUObjectAllocator` + `GUObjectArray` 封装。对象头（40 字节）之后的内存由子类构造函数决定（如 `AActor` 的 `RootComponent` 指针、`PrimaryActorTick` 等）。

### 调用链 2：UObject 销毁与 GC 内存回收

```
AActor::Destroy()
  └─> UWorld::DestroyActor(ThisActor)
        ├─> RouteEndPlay(EEndPlayReason::Destroyed)   // Gameplay 回调
        ├─> UnregisterAllComponents()                  // 从场景注销
        ├─> MarkAsGarbage()                            // 设置 RF_MirroredGarbage + InternalFlags::Garbage
        └─> MarkComponentsAsGarbage()                  // 遍历组件标记垃圾

// 下一轮 GC
CollectGarbage()
  └─> PerformReachabilityAnalysis()
        └─> 该 Actor 和组件被标记为 Unreachable
  └─> PostCollectGarbageImpl()
        └─> UnhashUnreachableObjects()
              └─> ConditionalBeginDestroy() → AActor::BeginDestroy()
                    ├─> UnregisterAllComponents()
                    ├─> Level->Actors.RemoveSingleSwap(this)  // 从 ULevel 数组物理移除
                    └─> UObject::BeginDestroy() → 清 Linker、Rename
        └─> IncrementalDestroyGarbage()
              └─> ConditionalFinishDestroy() → UObject::FinishDestroy()
                    ├─> DestroyNonNativeProperties()
                    ├─> GUObjectArray.ResetSerialNumber(this)  // WeakPtr 失效
                    └─> ~UObjectBase() → GUObjectArray.FreeUObjectIndex()
                          └─> FMemory::Free(Obj) → GMalloc->Free()
                                └─> TMallocBinnedCommon::Free → FPerThreadFreeBlockLists::Free（无锁回收）
```

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp`，第 6095~6192 行（UnhashUnreachableObjects）
> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/Obj.cpp`，第 1235~1339 行（ConditionalBeginDestroy / ConditionalFinishDestroy）

**关键结论**：从 `Destroy()` 到内存真正释放有三阶段延迟：
1. **Gameplay 阶段**：`EndPlay` + `UnregisterComponents`（立即执行）。
2. **GC 标记阶段**：可达性分析后将对象标记为 `Unreachable`（通常在下一帧或数帧后）。
3. **GC Purge 阶段**：`BeginDestroy` → `FinishDestroy` → 析构 → `FreeUObjectIndex` → `FMemory::Free`（增量执行，可分帧）。

### 调用链 3：渲染资源的 GPU 内存生命周期

```
游戏线程：UTexture2D::UpdateResource()
  └─> BeginInitResource(TextureResource)   // 投递初始化命令到渲染线程

渲染线程：FRHICommandListExecutor::ExecuteList()
  └─> FRHICommandListImmediate::InitResource(FRenderResource* Resource)
        └─> Resource->InitRHI()
              └─> UTexture2D::InitRHI()
                    └─> RHICreateTexture2D(Width, Height, Format, NumMips, NumSamples, Flags, CreateInfo)
                          └─> GDynamicRHI->RHICreateTexture2D(...)
                                └─> FD3D12DynamicRHI::RHICreateTexture2D
                                      ├─> D3D12CreateCommittedResource / CreatePlacedResource
                                      └─> 分配 GPU 显存，返回 FRHITexture 句柄

// 释放时
游戏线程：UTexture2D::BeginDestroy()
  └─> BeginReleaseResource(TextureResource)  // 投递释放命令

渲染线程：
  └─> Resource->ReleaseRHI()
        └─> RHIDestroyTexture(Texture)
              └─> FRHIResource::Release() → RefCount==0 → MarkForDelete()
                    └─> FRHICommandListExecutor::DeleteResources() 批量销毁
                          └─> ~FD3D12Texture() → ReleaseD3D12Resource() → GPU 显存归还
```

> 文件：`Engine/Source/Runtime/RenderCore/Public/RenderResource.h`，第 180~260 行（BeginInitResource / BeginReleaseResource）
> 文件：`Engine/Source/Runtime/RHI/Public/RHICommandList.h`，第 1200~1350 行（InitResource / ReleaseResource）

**关键结论**：GPU 资源的生命周期与 UObject **解耦**。UObject 的 `BeginDestroy` 只投递释放命令，真正的 GPU 内存释放发生在渲染线程，通过引用计数 + 批量删除保证安全。

### 调用链 4：RenderGraph 的 Transient GPU 内存复用

```
FRDGBuilder::CreateTexture(Desc, Name)
  └─> 分配 FRDGTexture 逻辑对象（仅描述符，无实际 GPU 内存）

FRDGBuilder::AddPass(...)
  └─> FRDGPass::SetInput/Output → 建立资源依赖图

FRDGBuilder::Execute()
  └─> FRDGBuilder::Compile()
        └─> 计算资源生命周期（LastProducer → LastConsumer）
        └─> FRHITransientResourceAllocator::Acquire(TextureDesc, Lifetime)
              ├─> 查找可复用的物理内存（Fence 已信号化）
              └─> 无可用 → FRHITransientHeap::Allocate(Size, Alignment)
                    └─> ID3D12Device::CreateHeap / vkAllocateMemory
  └─> FRDGBuilder::ExecutePass()
        └─> Pass Lambda 执行 → RHI 命令提交
  └─> FRDGBuilder::~FRDGBuilder() / ReleaseAll()
        └─> FRHITransientResourceAllocator::Release(Resource)
              └─> 标记 Fence，放入复用池（非立即释放）
```

> 文件：`Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h`，第 800~950 行（Execute / Compile）
> 文件：`Engine/Source/Runtime/RHI/Public/RHITransientResourceAllocator.h`，第 100~180 行（Acquire / Release）

**关键结论**：RenderGraph 通过**逻辑资源 + 延迟绑定 + Transient 别名**，实现了 GPU 内存的自动复用。开发者只需描述"需要什么资源"，RenderGraph 自动决定"复用哪块物理内存"。

---

## 多线程与内存交互

| 线程/上下文 | 使用的分配器 | 同步方式 |
|-------------|-------------|----------|
| **Game Thread** | `FMemory::Malloc/Free`（Binned2/3 TLS 缓存） | 无锁（TLS 热路径）|
| **Render Thread** | `FMemStackBase`（线性栈） | 单线程，无需同步 |
| **RHI Thread** | `FRHIResource` 引用计数 + 批量删除 | 原子操作 + 命令队列 |
| **Async Loading** | `FMemory::Malloc` + `FUObjectAllocator` | GC Lock + 哈希表锁 |
| **GPU** | `FRHITransientHeap` / `CreateCommittedResource` | Fence + Barrier |

**跨线程内存安全的关键设计**：
1. **游戏线程与渲染线程**：`BeginInitResource` / `BeginReleaseResource` 通过 `ENQUEUE_RENDER_COMMAND` 投递，渲染线程异步执行，UObject 通过 `IsReadyForFinishDestroy()` 等待 GPU 资源释放完成。
2. **GC 与渲染线程**：GC 的 `BeginDestroy` 阶段释放 `FRenderResource`，但真正的 GPU 内存释放延迟到渲染线程的 `DeleteResources`，通过 `FRHIResource` 引用计数避免 use-after-free。
3. **TLS 缓存 Trim**：`MarkTLSCachesAsUnusedOnCurrentThread` 让工作线程在空闲时归还缓存，减少全局内存压力。

---

## 上下层关系

### 上层调用者

| 上层 | 使用方式 |
|------|---------|
| `Gameplay 代码` | `NewObject<T>()` → FUObjectAllocator → FMemory → FMalloc |
| `AActor` | `SpawnActor` / `Destroy` → 语义层，底层走 UObject 体系 |
| `UMG / Slate` | `SNew` / `AddChild` → Slate 自有分配器 + FMemory |
| `RenderGraph` | `FRDGBuilder::CreateTexture` → 逻辑资源 → RHI GPU 内存 |

### 下层依赖

| 下层 | 作用 |
|------|------|
| `OS (Windows/Linux)` | `VirtualAlloc` / `mmap` / `vkAllocateMemory` 最终物理内存来源 |
| `D3D12/Vulkan/Metal` | 平台 GPU 驱动，管理显存堆、Fence、Barrier |

---

## 设计亮点与可迁移经验

1. **分层抽象，职责清晰**
   - Core 层负责"如何快速分配小块内存"；CoreUObject 负责"如何跟踪对象生命周期"；RenderCore 负责"如何高效复用渲染内存"；RHI 负责"如何安全操作 GPU 资源"。四层各司其职，上层不直接穿透下层。

2. **热路径零锁，冷路径聚合**
   - `FMalloc` TLS 缓存让最常见的分配/释放无锁；`FRHIResource` 批量删除避免每帧大量原子操作；`FMemStack` 线性分配让渲染线程完全无锁。

3. **延迟释放与引用计数**
   - UObject 的 `BeginDestroy/FinishDestroy` 两阶段模型、RHI 资源的 `MarkForDelete + 批量删除`、RenderGraph 的 `Transient 别名复用`，本质都是"标记→延迟→批量回收"的模式。这对任何需要异步资源释放的系统都有借鉴意义。

4. **对象头与业务数据分离**
   - `UObjectBase` 仅 40 字节对象头，`FRHITextureDesc` 仅描述符，GPU 内存由后端独立管理。这种"轻量级句柄 + 重量级后端资源"的分离，是自研引擎处理大型资源的标准模式。

5. **GC 分区：常驻 vs 动态**
   - `DisregardForGC` 区让引擎核心类完全跳过扫描，显著降低运行时开销。自研引擎可为元数据、类型系统设置类似的"永久区"。

---

## 关键源码片段

### FMalloc TLS 缓存快速路径

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 577~586 行

```cpp
UE_FORCEINLINE_HINT void* Malloc(uint32 InPoolIndex)
{
    return FreeLists[InPoolIndex].PopFromFront(InPoolIndex);
}

UE_FORCEINLINE_HINT bool Free(void* InPtr, uint32 InPoolIndex, uint32 InBinSize)
{
    return FreeLists[InPoolIndex].PushToFront(InPtr, InPoolIndex, InBinSize);
}
```

### GUObjectArray 索引分配

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectArray.cpp`，第 230~336 行

```cpp
void FUObjectArray::AllocateUObjectIndex(UObjectBase* Object, ...)
{
    if (IsOpenForDisregardForGC() && DisregardForGCEnabled())
    {
        Index = ++ObjLastNonGCIndex;  // 分配到非GC区
    }
    else
    {
        if (ObjAvailableList.Num() > 0)
            Index = ObjAvailableList.Pop();  // 复用已释放的索引
        else
            Index = ObjObjects.AddSingle();  // 在GC区末尾追加
    }
    ObjectItem->FlagsAndRefCount = (int64)(EInternalObjectFlags::PendingConstruction << 32);
    ObjectItem->SetObject(Object);
    Object->InternalIndex = Index;
}
```

### FRHIResource 引用计数与延迟删除

> 文件：`Engine/Source/Runtime/RHI/Public/RHIResources.h`，第 180~260 行

```cpp
uint32 FRHIResource::Release() const
{
    uint32 NewValue = NumReferences.Decrement();
    if (NewValue == 0)
    {
        MarkForDelete();
    }
    return NewValue;
}
```

---

## 关联阅读

- [[UE-Core-源码解析：内存分配器家族]] — FMalloc 家族的详细实现
- [[UE-CoreUObject-源码解析：GC 与对象生命周期]] — GC 可达性分析与增量 Purge
- [[UE-CoreUObject-源码解析：UObject 体系总览]] — UObjectBase 内存布局与全局注册
- [[UE-Engine-源码解析：World 与 Level 架构]] — World 中 Actor 的 Spawn/Destroy 语义
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]] — Tick 与 GC 的时序关系
- [[UE-专题：渲染一帧的生命周期]] — RenderGraph 与 RHI 命令队列的详细流程
- [[UE-专题：UObject 生命周期全链路]] — UObject 从生到死的完整流程

---

## 索引状态

- **所属 UE 阶段**：第八阶段 — 跨领域专题深度解析
- **对应 UE 笔记**：UE-专题：内存管理全链路
- **本轮分析完成度**：✅ 已完成三层分析（接口层 + 数据层 + 逻辑层 + 多线程交互 + 关联辐射）
- **更新日期**：2026-04-18
