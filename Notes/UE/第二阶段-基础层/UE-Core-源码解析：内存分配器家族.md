---
title: UE-Core-源码解析：内存分配器家族
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - core
  - memory
aliases:
  - UE-Core-内存分配器家族
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么要理解 UE 的内存分配器？

游戏引擎是内存密集型应用，每秒可能产生数万次分配请求。通用系统分配器（`malloc`）在碎片控制、线程竞争、对齐需求上往往无法满足引擎要求。UE 通过自研的 `FMalloc` 家族，实现了：

- **按大小分箱（Binned）**：消除小对象分配的内部碎片
- **TLS 线程缓存**：降低多线程竞争，提升局部性
- **调试与剖分能力**：Stomp、LeakDetection、FrameProfiler 等辅助工具
- **平台抽象**：同一接口下可切换 Ansi、Jemalloc、Mimalloc 等后端

理解这套体系，是自研引擎内存子系统的必修功课。

## What：UE 内存分配器的整体架构

### 模块定位

| 项目 | 路径 |
|------|------|
| 核心接口 | `Engine/Source/Runtime/Core/Public/HAL/MemoryBase.h` |
| 全局入口 | `Engine/Source/Runtime/Core/Public/HAL/UnrealMemory.h` |
| Binned 公共基类 | `Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h` |
| Binned2 / Binned3 | `Engine/Source/Runtime/Core/Public/HAL/MallocBinned2.h` / `MallocBinned3.h` |
| 线程安全代理 | `Engine/Source/Runtime/Core/Public/HAL/MallocThreadSafeProxy.h` |
| 调试分配器 | `Engine/Source/Runtime/Core/Public/HAL/MallocStomp.h` |
| 实现文件 | `Engine/Source/Runtime/Core/Private/HAL/` 下同名 `.cpp` |

### 核心类图（简化）

```
FMalloc (抽象基类)
├── FMallocAnsi              → 直接包装系统 malloc/free
├── FMallocBinned            → 第一代分箱分配器（legacy）
├── FMallocBinnedCommonBase  → Binned2/3 公共基类
│   └── TMallocBinnedCommon<AllocType, NumSmallPools, MaxSmallPoolSize>
│       ├── FMallocBinned2
│       └── FMallocBinned3
├── FMallocStomp             → 调试检测（页保护、sentinel）
├── FMallocThreadSafeProxy   → 装饰器：用锁包装非线程安全分配器
└── ...（Jemalloc、Mimalloc、Libpas 等）

FMemory (struct)             → 全局静态入口，封装 GMalloc
```

---

## 第 1 层：接口层（What）

### FMalloc：所有分配器的统一接口

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MemoryBase.h`，第 96~294 行

```cpp
class FMalloc : public FUseSystemMallocForNew, public FExec
{
public:
    virtual void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void  Free(void* Original) = 0;

    virtual void* MallocZeroed(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    virtual SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment);
    virtual bool   GetAllocationSize(void* Original, SIZE_T& SizeOut);

    // TLS 缓存生命周期管理
    virtual void SetupTLSCachesOnCurrentThread();
    virtual void MarkTLSCachesAsUsedOnCurrentThread();
    virtual void MarkTLSCachesAsUnusedOnCurrentThread();
    virtual void ClearAndDisableTLSCachesOnCurrentThread();

    // 统计与诊断
    virtual void UpdateStats();
    virtual void DumpAllocatorStats(class FOutputDevice& Ar);
    virtual bool ValidateHeap();
    virtual bool IsInternallyThreadSafe() const;
    ...
};
```

`FMalloc` 的设计要点：
- **对齐语义**：`DEFAULT_ALIGNMENT = 0` 表示按引擎规则（≥16 字节按 16 对齐，<16 按 8 对齐）
- **Try 变体**：`TryMalloc` / `TryRealloc` 允许在内存不足时返回 `nullptr` 而非崩溃
- **量化大小**：`QuantizeSize` 让容器可按最优大小扩容，减少碎片
- **TLS 缓存钩子**：上层通过 `SetupTLSCachesOnCurrentThread` 等接口通知分配器线程的睡眠/唤醒状态，便于批量回收

### FMemory：全局统一入口

> 文件：`Engine/Source/Runtime/Core/Public/HAL/UnrealMemory.h`，第 93~285 行

```cpp
struct FMemory
{
    static FORCENOINLINE CORE_API void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static FORCENOINLINE CORE_API void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static FORCENOINLINE CORE_API void  Free(void* Original);
    static FORCENOINLINE CORE_API SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);

    // 平台内存操作封装
    static UE_FORCEINLINE_HINT void* Memmove(void* Dest, const void* Src, SIZE_T Count);
    static UE_FORCEINLINE_HINT void* Memset(void* Dest, uint8 Char, SIZE_T Count);
    static UE_FORCEINLINE_HINT void* Memzero(void* Dest, SIZE_T Count);
    static UE_FORCEINLINE_HINT void* Memcpy(void* Dest, const void* Src, SIZE_T Count);
    ...
};
```

`FMemory` 是**绝大多数 UE 代码的调用点**。它内部转发给 `GMalloc`（全局 `FMalloc*`），但额外提供：
- `AutoRTFM` 事务内存兼容
- `MemoryTrace` 埋点（用于内存分析工具）
- `External` 变体（在崩溃上下文或启动早期使用的安全路径）

### FMallocThreadSafeProxy：装饰器模式保证线程安全

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocThreadSafeProxy.h`，第 13~144 行

```cpp
class FMallocThreadSafeProxy : public FMalloc
{
private:
    FMalloc* UsedMalloc;
    FCriticalSection SynchronizationObject;

public:
    virtual void* Malloc(SIZE_T Size, uint32 Alignment) override
    {
        FScopeLock ScopeLock(&SynchronizationObject);
        return UsedMalloc->Malloc(Size, Alignment);
    }
    ...
    virtual bool IsInternallyThreadSafe() const override { return true; }
};
```

这是一个典型的**装饰器（Decorator）**。当底层分配器（如某些平台特定实现）未实现内部线程安全时，UE 会在启动时将其包装为 `FMallocThreadSafeProxy`。

---

## 第 2 层：数据层（How - Structure）

### Binned 分配器的核心数据结构

UE 的默认生产环境分配器通常是 `FMallocBinned2` 或 `FMallocBinned3`。它们共享 `TMallocBinnedCommon` 模板基类，核心设计思想是：

1. **小对象按 Bin 分配**：预定义一组固定大小（如 8, 16, 24, 32, ... 直到 28672 字节），每个大小对应一个 PoolIndex
2. **每个 Pool 由 OS 页组成**：从操作系统申请 64KB / 128KB 等大页，再切分为等大小 Block
3. **TLS 线程缓存**：每个工作线程维护独立的 `FPerThreadFreeBlockLists`，无锁回收/复用
4. **大对象直接走 OS**：超过 `MAX_SMALL_POOL_SIZE` 的请求直接调用 `VirtualAlloc` / `mmap`

#### FBundleNode：空闲块链表节点

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 187~202 行

```cpp
struct FBundleNode
{
    uint64 NextNodeInCurrentBundle : 48;  // 下一个节点指针
    uint64 Count : 8;                     // 当前 bundle 中的节点数
    uint64 Reserved : 8;                  // ARM TBI/MTE 预留
};
```

空闲块本身就被当作链表节点复用，**零额外开销**。

#### FPerThreadFreeBlockLists：线程本地缓存

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 502~649 行

```cpp
struct FPerThreadFreeBlockLists
{
    UE_FORCEINLINE_HINT void* Malloc(uint32 InPoolIndex)
    {
        return FreeLists[InPoolIndex].PopFromFront(InPoolIndex);
    }

    UE_FORCEINLINE_HINT bool Free(void* InPtr, uint32 InPoolIndex, uint32 InBinSize)
    {
        return FreeLists[InPoolIndex].PushToFront(InPtr, InPoolIndex, InBinSize);
    }
    ...
private:
    UE::FWordMutex Mutex;                 // 用于 trim 时同步
    uint64 MemoryTrimEpoch = 0;
    FFreeBlockList FreeLists[NUM_SMALL_POOLS];
    bool bLockedByOwnerThread = false;
};
```

每个线程通过 TLS Slot（`BinnedTlsSlot`）获取自己的缓存。`Malloc` 和 `Free` 在热路径上是**完全无锁**的。只有当线程缓存满/空，或进行全局 trim 时，才需要加锁与全局回收器交互。

#### FPtrToPoolMapping：指针 → Pool 元数据映射

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 207~263 行

```cpp
struct FPtrToPoolMapping
{
    uint64 PtrToPoolPageBitShift;
    uint64 HashKeyShift;
    uint64 PoolMask;
    uint64 MaxHashBuckets;
    uint64 AddressSpaceBase;

    inline void GetHashBucketAndPoolIndices(const void* InPtr,
        uint32& OutBucketIndex, UPTRINT& OutBucketCollision, uint32& OutPoolIndex) const
    {
        const UPTRINT Ptr = (UPTRINT)InPtr - AddressSpaceBase;
        OutBucketCollision = Ptr >> HashKeyShift;
        OutBucketIndex = uint32(OutBucketCollision & (MaxHashBuckets - 1));
        OutPoolIndex = uint32((Ptr >> PtrToPoolPageBitShift) & PoolMask);
    }
};
```

`Free` 时需要通过指针反查它属于哪个 Pool、哪个 Bin。Binned 使用**哈希 + 位运算**而非维护每个指针的头部信息，从而避免内存膨胀。对于大对象，则直接在 `FPoolHashBucket` 链表中查找 `FPoolInfo`。

### 各分配器的数据差异

| 分配器 | 小对象上限 | 页大小 | TLS 缓存 | 核心差异 |
|--------|-----------|--------|---------|---------|
| FMallocBinned | 约 32KB | 可变 | 有 | 第一代，粗/细粒度锁 |
| FMallocBinned2 | 约 32KB | 64KB | 有 | 64KB 大页对齐，支持 fork |
| FMallocBinned3 | 约 128KB | 1GB VM 池 | 有 | 每 Bin 独立预占 1GB 虚拟地址池，位树管理 Block |
| FMallocAnsi | 无 | 系统页 | 无 | 直接透传 malloc/free |
| FMallocStomp | 无 | 页保护 | 无 | 每分配一页，用 guard page 检测越界 |

---

## 第 3 层：逻辑层（How - Behavior）

### 调用链 1：FMemory::Malloc → TMallocBinnedCommon::Malloc

```
FMemory::Malloc
  → GMalloc->Malloc(Size, Alignment)
    → TMallocBinnedCommon::Malloc
      ├─ 小对象路径（Size ≤ MAX_SMALL_POOL_SIZE）
      │    → BoundSizeToPoolIndex(Size) 得 PoolIndex
      │    → FPerThreadFreeBlockLists::Malloc(PoolIndex)
      │         → PopFromFront (无锁，热路径)
      │    ├─ 若 TLS 缓存空
      │    │    → 从全局 Recycler 获取 Bundle
      │    │    → 若仍空，向 OS 申请新 Page/Block
      │    └─ 返回对齐后的 Block 指针
      └─ 大对象路径
           → 直接 OS 分配（VirtualAlloc / mmap）
           → Internal::GetOrCreatePoolInfo 记录元数据
```

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 871~878 行（BoundSizeToPoolIndex）

```cpp
inline uint32 BoundSizeToPoolIndex(SIZE_T Size, const uint8(&MemSizeToPoolIndex)[SIZE_TO_POOL_INDEX_NUM]) const
{
    const auto Index = ((Size + UE_MBC_MIN_SMALL_POOL_ALIGNMENT - 1) >> UE_MBC_BIN_SIZE_SHIFT);
    const uint32 PoolIndex = uint32(MemSizeToPoolIndex[Index]);
    return PoolIndex;
}
```

大小到 PoolIndex 的映射通过**预计算查表**完成，O(1)。

### 调用链 2：Free → TLS 缓存回收 → 全局 Trim

```
FMemory::Free(Ptr)
  → TMallocBinnedCommon::Free(Ptr)
    → 小对象：PtrToPoolMapping 反查 PoolIndex
      → FPerThreadFreeBlockLists::Free(Ptr, PoolIndex, BinSize)
        → PushToFront (无锁，热路径)
          ├─ 若 PartialBundle 未满，直接入链
          └─ 若 PartialBundle 满
               → 提升为 FullBundle
               → 若 FullBundle 也满，向全局 Recycler 归还或释放给 OS
    → 大对象：FindPoolInfo → OS 释放
```

### 调用链 3：Trim 与线程缓存生命周期

> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocBinnedCommon.h`，第 780~834 行

```cpp
virtual void MarkTLSCachesAsUnusedOnCurrentThread() override
{
    // 只 flush 新 epoch 的缓存
    const bool bNewEpochOnly = true;
    ((AllocType*)this)->FlushCurrentThreadCacheInternal(bNewEpochOnly);
    FPerThreadFreeBlockLists::UnlockTLS();  // 标记为不可用，trim 时不会竞态
}
```

引擎在渲染线程、工作线程进入空闲（如切后台）时会调用 `MarkTLSCachesAsUnusedOnCurrentThread`，此时分配器会：
1. 把当前线程缓存中的 Block 归还到全局 Recycler 或直接释放给 OS
2. 通过 `MemoryTrimEpoch` 避免重复清理
3. 解锁 TLS，确保其他线程执行全局 trim 时不会读到半一致状态

---

## 上下层关系

| 上层调用者 | 使用方式 | 说明 |
|-----------|---------|------|
| `FMemory` | 全局入口 | 99% 的引擎代码通过 `FMemory::Malloc/Free` 分配 |
| `TArray` / `TMap` | 容器内部 | 容器通过 `FMemory` 或 `FDefaultAllocator` 间接调用 |
| `NewObject` / `ConstructObject` | UObject 分配 | 在 `CoreUObject` 中调用 `FMemory`，但附加 GC 追踪 |
| `FMallocThreadSafeProxy` | 包装层 | 对非线程安全分配器加锁 |
| `MallocStomp` / `MallocLeakDetection` | 调试包装 | 在开发阶段插入 GMalloc 链前端，检测越界/泄漏 |

---

## 设计亮点与可迁移经验

1. **抽象 + 链式装饰**：`FMalloc` 基类定义统一语义，通过 `ThreadSafeProxy`、`Stomp`、`LeakDetection` 等装饰器按需组合，避免在核心分配器中堆砌调试代码。
2. **热路径零锁**：TLS 缓存让最常见的 `Malloc/Free` 完全无锁，仅退化为全局交互时才加锁。
3. **指针元数据外置**：通过 `FPtrToPoolMapping` 和页对齐约定，把分配元数据从每个 Block 头部剥离，显著降低小对象开销。
4. **量化大小（QuantizeSize）**：让容器在扩容时知道"最优请求大小"，减少内部碎片。这对自研引擎的 `vector`/`array` 设计非常有借鉴意义。
5. **Trim Epoch 机制**：用单调递增的 epoch 标记哪些线程缓存需要回收，避免无差别扫描所有线程。

---

## 关键源码片段

**FMalloc 接口定义**
> 文件：`Engine/Source/Runtime/Core/Public/HAL/MemoryBase.h`，第 96~140 行

```cpp
class FMalloc : public FUseSystemMallocForNew, public FExec
{
public:
    virtual void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void  Free(void* Original) = 0;
    ...
};
```

**TLS 缓存快速路径**
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

**线程安全代理装饰器**
> 文件：`Engine/Source/Runtime/Core/Public/HAL/MallocThreadSafeProxy.h`，第 40~65 行

```cpp
virtual void* Malloc(SIZE_T Size, uint32 Alignment) override
{
    FScopeLock ScopeLock(&SynchronizationObject);
    return UsedMalloc->Malloc(Size, Alignment);
}
```

---

## 关联阅读

- [[UE-Core-源码解析：基础类型与宏体系]] — `DEFAULT_ALIGNMENT`、平台抽象宏的由来
- [[UE-Core-源码解析：字符串与容器]] — `TArray` 如何利用 `FMemory` 和 `QuantizeSize`
- [[UE-CoreUObject-源码解析：GC 与对象生命周期]] — UObject 的内存如何与 `FMalloc` 交互
- [[UE-专题：内存管理全链路]] — 从 Core 到 CoreUObject 到 RenderCore 的完整内存链路

---

## 索引状态

- **所属 UE 阶段**：第二阶段 - 基础层 / 2.2 内存、线程与任务
- **对应 UE 笔记**：UE-Core-源码解析：内存分配器家族
- **本轮分析完成度**：✅ 已完成三层分析（接口层、数据层、逻辑层）
