---
title: UE-Core-源码解析：智能指针与引用
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - core
  - smart-pointers
aliases:
  - UE-Core-智能指针与引用
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么 UE 不直接用 std::shared_ptr？

UE 需要一套与引擎生态深度集成的智能指针：

- **自定义删除器与空基优化（EBO）**：减少 `shared_ptr` 控制块的额外开销
- **侵入式分配（MakeShared）**：对象与控制块在同一次分配中，降低缓存未命中
- **与 UObject GC 体系区分**：引擎既有基于引用计数的非 UObject 指针，也有基于 GC 索引的 `TWeakObjectPtr`
- **线程安全模式可选**：`ESPMode::ThreadSafe` / `NotThreadSafe` 可在编译期决定是否需要原子操作
- **AutoRTFM 兼容**：支持事务内存的撤销语义

## What：UE 智能指针家族概览

### 模块定位

| 项目 | 路径 |
|------|------|
| 共享/弱指针主头 | `Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h` |
| 内部引用计数 | `Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h` |
| 前向声明 | `Engine/Source/Runtime/Core/Public/Templates/SharedPointerFwd.h` |
| 独占指针 | `Engine/Source/Runtime/Core/Public/Templates/UniquePtr.h` |
| UObject 弱指针 | `Engine/Source/Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h` |

### 类型速查表

| 类型 | 所有权 | 可空 | 线程安全模式 | 主要用途 |
|------|--------|------|-------------|---------|
| `TSharedRef<T, Mode>` | 共享 | ❌ | 可选 | 保证非空的强引用 |
| `TSharedPtr<T, Mode>` | 共享 | ✅ | 可选 | 可空的强引用（类似 `shared_ptr`） |
| `TWeakPtr<T, Mode>` | 无 | ✅ | 可选 | 弱引用，需 `Pin()` 提升 |
| `TUniquePtr<T, Deleter>` | 独占 | ✅ | 不涉及 | 不可拷贝，仅可移动 |
| `TSharedFromThis<T, Mode>` | — | — | 可选 | 让对象能安全获取自身 `TSharedPtr` |
| `TWeakObjectPtr<T>` | 无 | ✅ | GC 相关 | 专用于 UObject，基于 ObjectIndex + SerialNumber |

---

## 第 1 层：接口层（What）

### TSharedPtr / TSharedRef / TWeakPtr 的公共接口

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h`

```cpp
// TSharedRef：构造时必须有效，禁止 Reset/置空
template< class ObjectType, ESPMode Mode = ESPMode::ThreadSafe >
class TSharedRef
{
public:
    ObjectType& Get() const;
    ObjectType& operator*() const;
    ObjectType* operator->() const;
    TSharedPtr<ObjectType, Mode> ToSharedPtr() const;
    TWeakPtr<ObjectType, Mode> ToWeakPtr() const;
    int32 GetSharedReferenceCount() const;
    bool IsUnique() const;
    ...
};

// TSharedPtr：可空，语义接近 std::shared_ptr
template< class ObjectType, ESPMode Mode = ESPMode::ThreadSafe >
class TSharedPtr
{
public:
    TSharedPtr() = default;
    ObjectType* Get() const;
    ObjectType& operator*() const;
    ObjectType* operator->() const;
    explicit operator bool() const;
    bool IsValid() const;
    void Reset();
    TSharedRef<ObjectType, Mode> ToSharedRef() const;  // 空时断言
    TWeakPtr<ObjectType, Mode> ToWeakPtr() const;
    ...
};

// TWeakPtr：不阻止销毁
template< class ObjectType, ESPMode Mode = ESPMode::ThreadSafe >
class TWeakPtr
{
public:
    TSharedPtr<ObjectType, Mode> Pin() const;  // 提升为强引用
    bool IsValid() const;
    void Reset();
    ...
};
```

### TUniquePtr：独占所有权

> 文件：`Engine/Source/Runtime/Core/Public/Templates/UniquePtr.h`

```cpp
template <typename T, typename Deleter = TDefaultDelete<T>>
class TUniquePtr
{
public:
    T* Get() const;
    T& operator*() const;
    T* operator->() const;
    T* Release();
    void Reset(T* InPtr = nullptr);
    explicit operator bool() const;
    ...
};
```

`TUniquePtr` 禁止拷贝构造和拷贝赋值，仅支持移动语义，与 `std::unique_ptr` 完全一致。

### TWeakObjectPtr：UObject 专用弱指针

> 文件：`Engine/Source/Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h`

```cpp
template< class T, class TWeakObjectPtrBase = FWeakObjectPtr >
class TWeakObjectPtr : private TWeakObjectPtrBase
{
public:
    T* Get(bEvenIfPendingKill = false) const;
    TStrongObjectPtr<T> Pin() const;
    bool IsValid() const;
    bool IsStale() const;
    ...
};
```

`TWeakObjectPtr` **不基于引用计数**，而是存储 `ObjectIndex + SerialNumber`，通过 `GUObjectArray` 查询对象是否仍存活。这是 UObject GC 体系与 C++ 引用计数体系的分界线。

---

## 第 2 层：数据层（How - Structure）

### 引用计数控制器：TReferenceControllerBase

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 44~335 行

```cpp
template <ESPMode Mode>
class TReferenceControllerBase
{
    using RefCountType = std::conditional_t<Mode == ESPMode::ThreadSafe, std::atomic<int32>, int32>;

public:
    RefCountType SharedReferenceCount{1};  // 强引用计数
    RefCountType WeakReferenceCount{1};    // 弱引用计数（含一个强引用对应的弱引用）

    virtual void DestroyObject() = 0;

    inline void AddSharedReference();
    bool ConditionallyAddSharedReference();
    inline void ReleaseSharedReference();
    inline void AddWeakReference();
    void ReleaseWeakReference();
    ...
};
```

控制器的生命周期规则：
- `SharedReferenceCount == 0` → 调用 `DestroyObject()` 析构被管对象，同时 `WeakReferenceCount--`
- `WeakReferenceCount == 0` → `delete this`，销毁控制器本身

### 两种控制器实现

#### 1. TReferenceControllerWithDeleter（非侵入式）

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 374~396 行

```cpp
template <typename ObjectType, typename DeleterType, ESPMode Mode>
class TReferenceControllerWithDeleter
    : private TDeleterHolder<DeleterType>, public TReferenceControllerBase<Mode>
{
    ObjectType* Object;
    virtual void DestroyObject() override
    {
        this->InvokeDeleter(Object);
    }
};
```

`TDeleterHolder` 利用**空基优化（EBO）**：如果 `DeleterType` 是空类（如默认的 `DefaultDeleter`），它不会占用额外内存。

#### 2. TIntrusiveReferenceController（侵入式，MakeShared 使用）

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 398~445 行

```cpp
template <typename ObjectType, ESPMode Mode>
class TIntrusiveReferenceController : public TReferenceControllerBase<Mode>
{
    mutable TTypeCompatibleBytes<ObjectType> ObjectStorage;

    virtual void DestroyObject() override
    {
        DestructItem((ObjectType*)&ObjectStorage);
    }

    ObjectType* GetObjectPtr() const
    {
        return (ObjectType*)&ObjectStorage;
    }
};
```

`MakeShared<Foo>(Args...)` 时，只 `new` 一次 `TIntrusiveReferenceController`，对象内嵌在 `ObjectStorage` 中。这消除了控制器的二次分配，提升缓存局部性。

### FSharedReferencer / FWeakReferencer：包装器

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 533~841 行

```cpp
template<ESPMode Mode>
class FSharedReferencer
{
    TReferenceControllerBase<Mode>* ReferenceController;
    ...
};

template<ESPMode Mode>
class FWeakReferencer
{
    TReferenceControllerBase<Mode>* ReferenceController;
    ...
};
```

`TSharedPtr` 内部持有 `FSharedReferencer<Mode>`，`TWeakPtr` 内部持有 `FWeakReferencer<Mode>`。它们负责实际的引用计数加减和生命周期管理。

---

## 第 3 层：逻辑层（How - Behavior）

### 调用链 1：TSharedPtr 构造与拷贝

```cpp
TSharedPtr<FMyType> Ptr = MakeShared<FMyType>(Args...);
```

展开后的调用链：

```
MakeShared<FMyType>(Args...)
  → NewIntrusiveReferenceController(Forward<Args>(Args)...)
    → new TIntrusiveReferenceController<FMyType, ThreadSafe>
      → placement new 构造 FMyType 于 ObjectStorage
  → TSharedPtr(FSharedReferencer(Controller))
    → FSharedReferencer 保存 Controller 指针（SharedRefCount 已为 1）
```

拷贝时：

```cpp
TSharedPtr<FMyType> Ptr2 = Ptr;
```

```
TSharedPtr 拷贝构造
  → FSharedReferencer 拷贝构造
    → ReferenceController->AddSharedReference()
      → ThreadSafe 模式：std::memory_order_relaxed 的 fetch_add
```

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 106~137 行

```cpp
inline void AddSharedReference()
{
    if constexpr (Mode == ESPMode::ThreadSafe)
    {
        #if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
            ++SharedReferenceCount;  // 生成 lock inc（比 fetch_add 更优）
        #else
            SharedReferenceCount.fetch_add(1, std::memory_order_relaxed);
        #endif
    }
    else
    {
        ++SharedReferenceCount;
    }
}
```

### 调用链 2：TWeakPtr::Pin()（弱引用提升）

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 139~209 行

```cpp
bool ConditionallyAddSharedReference()
{
    if constexpr (Mode == ESPMode::ThreadSafe)
    {
        int32 OriginalCount = SharedReferenceCount.load(std::memory_order_relaxed);
        for (;;)
        {
            if (OriginalCount == 0)
            {
                return false;  // 对象已销毁，拒绝复活
            }
            if (SharedReferenceCount.compare_exchange_weak(
                OriginalCount, OriginalCount + 1, std::memory_order_relaxed))
            {
                return true;
            }
        }
    }
    ...
}
```

`Pin()` 的核心是**条件性加引用计数**：
1. 若 `SharedReferenceCount == 0`，说明对象已析构，返回空 `TSharedPtr`
2. 否则通过 `compare_exchange_weak` 安全递增
3. 这是防止**已销毁对象的弱引用被复活**的关键机制

### 调用链 3：TSharedPtr 析构

```
TSharedPtr 析构
  → FSharedReferencer 析构
    → ReleaseSharedReferenceNoInline(ReferenceController)
      → ReferenceController->ReleaseSharedReference()
        → fetch_sub(1, std::memory_order_acq_rel)
          ├─ OldSharedCount > 1：仅减计数
          └─ OldSharedCount == 1：
               → DestroyObject() 析构对象
               → ReleaseWeakReference() 减弱计数
                 → WeakCount == 0 时 delete Controller
```

> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 211~249 行

```cpp
inline void ReleaseSharedReference()
{
    int32 OldSharedCount = SharedReferenceCount.fetch_sub(1, std::memory_order_acq_rel);
    checkSlow(OldSharedCount > 0);
    if (OldSharedCount == 1)
    {
        DestroyObject();
        ReleaseWeakReference();
    }
}
```

**`std::memory_order_acq_rel` 的用意**：确保如果本次操作触发了 `DestroyObject()`，析构函数的副作用不会被重排序到确认计数归零之前。

### TSharedFromThis：安全自引用

```cpp
class FMyType : public TSharedFromThis<FMyType>
{
};
```

当对象首次被包装为 `TSharedPtr` 时，`EnableSharedFromThis` 会检测基类并将一个**弱引用**写入对象内部的 `TWeakPtr`。后续对象可通过 `AsShared()` / `AsWeak()` 获取自身智能指针，而不会因为重复创建独立控制器导致双重释放。

---

## 上下层关系

| 上层模块 | 使用方式 | 说明 |
|---------|---------|------|
| `TArray` / `TMap` | 通常不直接用 | 容器使用原始分配器 + 移动语义，但在 `TSparseArray` 等结构中会配合 `TSharedPtr` |
| `UE::Tasks` | `TSharedRef` / `TSharedPtr` | 任务系统中的数据共享、Future 传递 |
| `Slate` | `TSharedPtr<SWidget>` | UI 控件树大量依赖共享指针管理生命周期 |
| `Delegates` | `TWeakPtr` / `TWeakObjectPtr` | 委托绑定中避免循环引用 |
| `UObject` 体系 | `TWeakObjectPtr` | 与 GC 集成，不占用引用计数 |

---

## 设计亮点与可迁移经验

1. **侵入式 MakeShared**：一次分配同时容纳对象和控制器，避免二次 new 的缓存不友好。自研引擎应优先提供 `MakeShared` / `MakeUnique`。
2. **ConditionallyAddSharedReference**：弱引用提升时使用 CAS 循环，严格禁止"复活已死对象"，这是 `std::shared_ptr` 也有的机制，但 UE 明确使用 `compare_exchange_weak` 并注释了性能理由。
3. **内存序精细控制**：强引用递增用 `relaxed`（无副作用依赖），递减用 `acq_rel`（可能触发析构），在 x64 上甚至针对 MSVC 优化为 `lock inc`。
4. **双体系并存**：
   - 非 UObject 世界：`TSharedPtr` + 引用计数
   - UObject 世界：`TWeakObjectPtr` + GC ObjectIndex
   这种区分让引擎既能享受 C++ RAII，又能支持反射、序列化、GC。
5. **AutoRTFM 兼容**：所有引用计数操作包裹在 `UE_AUTORTFM_OPEN` / `ONABORT` / `ONCOMMIT` 中，支持事务回滚。

---

## 关键源码片段

**引用计数控制器基类**
> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 44~71 行

```cpp
template <ESPMode Mode>
class TReferenceControllerBase
{
    using RefCountType = std::conditional_t<Mode == ESPMode::ThreadSafe, std::atomic<int32>, int32>;
public:
    RefCountType SharedReferenceCount{1};
    RefCountType WeakReferenceCount{1};
    virtual void DestroyObject() = 0;
    ...
};
```

**侵入式控制器（MakeShared）**
> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 398~445 行

```cpp
template <typename ObjectType, ESPMode Mode>
class TIntrusiveReferenceController : public TReferenceControllerBase<Mode>
{
    mutable TTypeCompatibleBytes<ObjectType> ObjectStorage;
    virtual void DestroyObject() override
    {
        DestructItem((ObjectType*)&ObjectStorage);
    }
    ObjectType* GetObjectPtr() const { return (ObjectType*)&ObjectStorage; }
};
```

**弱引用提升的安全 CAS**
> 文件：`Engine/Source/Runtime/Core/Public/Templates/SharedPointerInternals.h`，第 144~184 行

```cpp
bool ConditionallyAddSharedReference()
{
    int32 OriginalCount = SharedReferenceCount.load(std::memory_order_relaxed);
    for (;;)
    {
        if (OriginalCount == 0) return false;
        if (SharedReferenceCount.compare_exchange_weak(
            OriginalCount, OriginalCount + 1, std::memory_order_relaxed))
        {
            return true;
        }
    }
}
```

---

## 关联阅读

- [[UE-Core-源码解析：内存分配器家族]] — `MakeShared` 如何利用 `FMemory::Malloc`
- [[UE-Core-源码解析：委托与事件系统]] — `BindSP` / `BindWeakLambda` 如何使用 `TWeakPtr`
- [[UE-CoreUObject-源码解析：UObject 体系总览]] — `TWeakObjectPtr` 与 GC 的集成
- [[UE-专题：内存管理全链路]] — 从智能指针到 GC 的完整对象生命周期

---

## 索引状态

- **所属 UE 阶段**：第二阶段 - 基础层 / 2.2 内存、线程与任务
- **对应 UE 笔记**：UE-Core-源码解析：智能指针与引用
- **本轮分析完成度**：✅ 已完成三层分析（接口层、数据层、逻辑层）
