---
title: UE-Core-源码解析：委托与事件系统
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - core
  - delegates
aliases:
  - UE-Core-委托与事件系统
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引\|UE全解析主索引]])

## Why：为什么 UE 需要自研委托系统？

游戏引擎中充满了"当某事发生时，调用一系列回调"的需求：

- 玩家生命值变化 → 更新 UI、触发音效、记录日志
- 资源加载完成 → 通知引用方继续初始化
- 编辑器属性修改 → 刷新 Details 面板、重编译材质

C++ 原生函数指针无法直接绑定成员函数（需要对象实例），而 `std::function` 在性能、内存布局、UObject 兼容性上都不够理想。UE 的委托系统提供了：

- **多种绑定方式**：静态函数、原始成员函数、Lambda、`TSharedPtr` 成员、`TWeakObjectPtr` 成员、UFunction 反射
- **Payload 支持**：绑定额外参数，调用时自动拼接
- **单播 / 多播 / 动态委托**：覆盖编译期与运行期的全部场景
- **零开销抽象**：热路径为虚函数调用 + 内联参数拼接，无 RTTI 依赖

## What：委托系统的模块地图

### 模块定位

| 文件 | 职责 |
|------|------|
| `Engine/Source/Runtime/Core/Public/Delegates/Delegate.h` | 宏定义入口、Dynamic Delegate 宏 |
| `Engine/Source/Runtime/Core/Public/Delegates/DelegateBase.h` | `TDelegateBase`、内存分配、UserPolicy |
| `Engine/Source/Runtime/Core/Public/Delegates/MulticastDelegateBase.h` | `TMulticastDelegateBase`、调用列表管理 |
| `Engine/Source/Runtime/Core/Public/Delegates/DelegateInstanceInterface.h` | `IBaseDelegateInstance`、Payload 接口 |
| `Engine/Source/Runtime/Core/Public/Delegates/DelegateInstancesImpl.h` | 所有具体实例类的完整实现 |
| `Engine/Source/Runtime/Core/Public/Delegates/DelegateSignatureImpl.inl` | `TDelegate` / `TMulticastDelegate` 的完整定义 |
| `Engine/Source/Runtime/Core/Public/Delegates/DelegateCombinations.h` | `DECLARE_DELEGATE_*` 宏组合 |
| `Engine/Source/Runtime/Core/Public/Delegates/IDelegateInstance.h` | `IDelegateInstance` 虚基类、`FDelegateHandle` |

### 类型速查

| 宏 | 展开后类型 | 用途 |
|----|-----------|------|
| `DECLARE_DELEGATE(MyDelegate)` | `TDelegate<void()>` | 单播委托 |
| `DECLARE_DELEGATE_OneParam(MyDelegate, int32)` | `TDelegate<void(int32)>` | 带参数单播 |
| `DECLARE_MULTICAST_DELEGATE(MyMulticast)` | `TMulticastDelegate<void()>` | 多播委托 |
| `DECLARE_DYNAMIC_DELEGATE(MyDynamic)` | 继承 `TBaseDynamicDelegate` 的类 | 支持反射、序列化 |
| `DECLARE_DYNAMIC_MULTICAST_DELEGATE(MyDynMulticast)` | 继承 `TBaseDynamicMulticastDelegate` 的类 | 蓝图事件 |

---

## 第 1 层：接口层（What）

### TDelegateBase：单播委托的公共接口

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateBase.h`，第 224~498 行

```cpp
template<typename ThreadSafetyMode>
class TDelegateBase : public TDelegateAccessHandlerBase<ThreadSafetyMode>, private FDelegateAllocation
{
public:
    inline void Unbind();
    inline bool IsBound() const;
    inline bool IsBoundToObject(FDelegateUserObjectConst InUserObject) const;
    inline bool IsCompactable() const;
    inline FDelegateHandle GetHandle() const;
    inline class UObject* GetUObject() const;
    SIZE_T GetAllocatedSize() const;
    ...
};
```

`TDelegateBase` 负责：
- 委托实例的**内存分配与释放**（通过 `FDelegateAllocation`）
- **读写锁访问控制**（通过 `TDelegateAccessHandlerBase`，可选线程安全策略）
- 公共查询接口（`IsBound`、`GetHandle` 等）

### TMulticastDelegateBase：多播委托的调用列表管理

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/MulticastDelegateBase.h`

```cpp
template <typename UserPolicy>
class TMulticastDelegateBase
{
public:
    template<typename... ParamTypes>
    void Broadcast(ParamTypes... Params);

    template<typename... SourceTypes>
    FDelegateHandle Add(const TDelegate<...>& InNewDelegate);

    void Remove(FDelegateHandle Handle);
    void RemoveAll(const void* InUserObject);
    void Clear();
    bool IsBound() const;
    ...
};
```

多播委托内部持有 `TArray<UnicastDelegateType>`，即**调用列表（Invocation List）**。

### 各种绑定方式的公共接口

在 `DelegateSignatureImpl.inl` 中，`TDelegate<RetValType(Params...)>` 提供了丰富的绑定方法：

```cpp
// 单播绑定
void BindStatic(...);
void BindLambda(...);
void BindRaw(UserClass* UserObject, ...);
void BindSP(const TSharedPtr<UserClass, SPMode>& UserObject, ...);
void BindUObject(UserClass* UserObject, ...);
void BindUFunction(UObject* Object, FName FunctionName);
void BindWeakLambda(UserClass* UserObject, ...);
void BindSPLambda(const TSharedPtr<UserClass, SPMode>& UserObject, ...);

// 静态工厂
static TDelegate CreateStatic(...);
static TDelegate CreateLambda(...);
static TDelegate CreateSP(...);
...
```

---

## 第 2 层：数据层（How - Structure）

### 委托实例的内存分配

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateBase.h`，第 191~219 行

```cpp
struct FDelegateAllocation
{
    FDelegateAllocatorType::ForElementType<FAlignedInlineDelegateType> DelegateAllocator;
    int32 DelegateSize = 0;
};
```

默认情况下 `FDelegateAllocatorType = FHeapAllocator`，即**堆分配**。但可以通过定义 `NUM_DELEGATE_INLINE_BYTES` 开启**内联分配**：

```cpp
#if NUM_DELEGATE_INLINE_BYTES > 0
    using FDelegateAllocatorType = TInlineAllocator<(NUM_DELEGATE_INLINE_BYTES / 16)>;
#endif
```

具体实例通过 **placement new** 构造在 `TDelegateBase` 的内存块中：

```cpp
template <typename ThreadSafetyMode>
UE_FORCEINLINE_HINT void* operator new(size_t Size, const TWriteLockedDelegateAllocation<ThreadSafetyMode>& LockedAllocation)
{
    return UE::Core::Private::DelegateAllocate(Size, LockedAllocation.Allocation);
}
```

### IBaseDelegateInstance：类型擦除的调用接口

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateInstanceInterface.h`

```cpp
template<typename FuncType, typename UserPolicy, typename... VarTypes>
class IBaseDelegateInstance : public UserPolicy::FDelegateInstanceExtras
{
public:
    virtual RetValType Execute(Params... Params) = 0;
    virtual bool ExecuteIfSafe(Params... Params) = 0;
    virtual IBaseDelegateInstance* CreateCopy() = 0;
};
```

`TDelegateBase` 以 `IDelegateInstance*` 持有实例，调用时向下转型为 `IBaseDelegateInstance<FuncType, UserPolicy, VarTypes...>` 并调用虚函数 `Execute()`。

### TCommonDelegateInstanceState：公共状态与 Payload

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateInstancesImpl.h`

```cpp
template<typename FuncType, typename UserPolicy, typename... VarTypes>
class TCommonDelegateInstanceState : public IBaseDelegateInstance<FuncType, UserPolicy, VarTypes...>
{
protected:
    TTuple<VarTypes...> Payload;        // 绑定时的额外参数
    FDelegateHandle Handle;             // 唯一句柄，用于 Remove
};
```

**Payload 机制**：绑定委托时可以传入额外参数，调用时通过 `Payload.ApplyAfter(Func, Forward<Params>(Params)...)` 自动将 Payload 拼接在委托参数**之后**传入。

### 具体实例类族

| 类名 | 绑定方式 | 安全校验 |
|------|---------|---------|
| `TBaseStaticDelegateInstance` | `BindStatic` / `CreateStatic` | 无 |
| `TBaseRawMethodDelegateInstance` | `BindRaw` | 无（原始指针） |
| `TBaseSPMethodDelegateInstance` | `BindSP` | `TWeakPtr::Pin()` |
| `TBaseUObjectMethodDelegateInstance` | `BindUObject` | `TWeakObjectPtr::Get()` |
| `TBaseUFunctionDelegateInstance` | `BindUFunction` | UObject + UFunction 有效性 |
| `TBaseFunctorDelegateInstance` | `BindLambda` | 无 |
| `TBaseSPLambdaDelegateInstance` | `BindSPLambda` | `TWeakPtr::Pin()` |
| `TWeakBaseFunctorDelegateInstance` | `BindWeakLambda` | `TWeakObjectPtr::Get()` |

### 多播调用列表的结构

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/MulticastDelegateBase.h`

```cpp
class TMulticastDelegateBase
{
    TArray<UnicastDelegateType> InvocationList;
    int32 InvocationListLockCount = 0;
    bool bNeedsCompaction = false;
    ...
};
```

- `InvocationList`：存储所有已绑定的单播委托实例
- `InvocationListLockCount`：`Broadcast` 时加锁计数，防止回调中修改列表导致迭代器失效
- `bNeedsCompaction`：`Broadcast` 中发现失效实例时标记，结束后清理空槽

---

## 第 3 层：逻辑层（How - Behavior）

### 调用链 1：单播委托的执行

```cpp
MyDelegate.Execute(Args...);
```

调用链：

```
TDelegate::Execute(Args...)
  → TDelegateBase::GetDelegateInstanceProtected()
    → 从 FDelegateAllocation 取出 IDelegateInstance*
  → static_cast<IBaseDelegateInstance*>(Instance)->Execute(Args...)
    → 具体实例类::Execute(Args...)
      → Payload.ApplyAfter(MethodPointer, Args...)
        → 调用实际绑定的函数/Lambda/成员函数
```

`Execute` 在委托未绑定时会触发 `check`，而 `ExecuteIfBound` 会先检查 `IsBound()`。

### 调用链 2：多播委托的 Broadcast

```cpp
MyMulticast.Broadcast(Args...);
```

调用链：

```
TMulticastDelegateBase::Broadcast(Args...)
  → ++InvocationListLockCount  （加锁，防止回调中修改列表）
  → 逆序遍历 InvocationList
    ├─ 对每个实例调用 ExecuteIfSafe(Args...)
    │    → 若实例是弱引用绑定（SP/UObject/WeakLambda）
    │         → Pin() / Get() 校验对象有效性
    │         → 有效则执行，无效则返回 false
    └─ 若 ExecuteIfSafe 返回 false
         → bNeedsCompaction = true
  → --InvocationListLockCount
  → 若 bNeedsCompaction 且 LockCount == 0
       → CompactInvocationList()
         → 移除失效实例（非锁定时用 RemoveAtSwap，锁定时用惰性删除）
```

**逆序遍历**的设计目的：当回调中 `Remove` 自己时，不会影响前面元素的索引。

### 调用链 3：弱引用安全校验

以 `TBaseSPMethodDelegateInstance` 为例：

> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateInstancesImpl.h`

```cpp
virtual bool ExecuteIfSafe(Params... Params) override
{
    TSharedPtr<UserClass, SPMode> SharedUserObject = WeakUserObject.Pin();
    if (SharedUserObject.IsValid())
    {
        (SharedUserObject.Get()->*MethodPtr)(Params..., Payload...);
        return true;
    }
    return false;
}
```

`TBaseUObjectMethodDelegateInstance` 同理，使用 `TWeakObjectPtr::Get()` 校验。这是 UE 委托系统**避免悬空指针回调**的核心机制。

### 调用链 4：Dynamic Delegate 的反射调用

`TBaseUFunctionDelegateInstance` 在 `Execute` 中不会直接调用函数指针，而是：

```cpp
virtual void Execute(Params... Params) override
{
    UFunction* Function = ...;
    UObject* Object = WeakUserObject.Get();
    // 组装参数到 FFrame
    Object->ProcessEvent(Function, &Parameters);
}
```

这就是 `DECLARE_DYNAMIC_DELEGATE` 能支持**蓝图绑定**的原因：它走的是 UObject 的反射事件系统，而非原生 C++ 函数调用。

---

## 上下层关系

| 上层模块 | 使用方式 |
|---------|---------|
| `Slate` | `FOnClicked`、`FOnTextChanged` 等大量单播/多播委托 |
| `UMG` | `OnPressed`、`OnValueChanged` 等蓝图暴露的动态多播委托 |
| `Gameplay` | `FOnTakeAnyDamage`、`FOnActorBeginOverlap` 等 Actor 事件 |
| `TaskGraph` | `FSimpleDelegateGraphTask` 将委托包装为任务图任务 |
| `AssetTools` | 导入完成回调、异步编译回调 |

---

## 设计亮点与可迁移经验

1. **宏驱动的类型安全**：`DECLARE_DELEGATE` 系列宏展开为具体的 `TDelegate<RetValType(Params...)>` 类型别名，既保持了 C++ 类型安全，又避免了手写模板参数的繁琐。
2. **Payload 自动拼接**：通过 `TTuple` 和 `ApplyAfter`，调用方无需关心绑定时的额外参数，接口极其简洁。
3. **Broadcast 时的锁 + Compaction**：`InvocationListLockCount` 保证回调中安全增删，`bNeedsCompaction` 实现惰性清理，兼顾安全与性能。
4. **弱引用绑定全家桶**：`BindSP`、`BindUObject`、`BindWeakLambda` 覆盖了非 UObject 和 UObject 两大对象体系，有效防止循环引用和悬空回调。
5. **编译期可配置的线程安全**：通过 `UserPolicy` 模板参数，可以插入 `FThreadSafeDelegateMode` 或 `FNotThreadSafeDelegateMode`，让对性能极度敏感的场景避免锁开销。

---

## 关键源码片段

**TDelegateBase 的实例存储与查询**
> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateBase.h`，第 443~451 行

```cpp
UE_FORCEINLINE_HINT IDelegateInstance* GetDelegateInstanceProtected()
{
    return DelegateSize ? (IDelegateInstance*)DelegateAllocator.GetAllocation() : nullptr;
}
```

**多播 Broadcast 的逆序遍历与 Compaction**
> 文件：`Engine/Source/Runtime/Core/Public/Delegates/MulticastDelegateBase.h`（核心逻辑节选）

```cpp
void Broadcast(ParamTypes... Params)
{
    FMulticastInvocationListReadLock Lock(InvocationList);
    for (int32 Index = InvocationList.Num() - 1; Index >= 0; --Index)
    {
        if (!InvocationList[Index].ExecuteIfSafe(Params...))
        {
            MarkForCompaction(Index);
        }
    }
}
```

**弱引用 SP 绑定的安全执行**
> 文件：`Engine/Source/Runtime/Core/Public/Delegates/DelegateInstancesImpl.h`（概念还原）

```cpp
virtual bool ExecuteIfSafe(Params... Params) override
{
    TSharedPtr<UserClass, SPMode> SharedUserObject = WeakUserObject.Pin();
    if (SharedUserObject.IsValid())
    {
        (SharedUserObject.Get()->*MethodPtr)(Params...);
        return true;
    }
    return false;
}
```

---

## 关联阅读

- [[UE-Core-源码解析：智能指针与引用]] — `TWeakPtr::Pin()` 与 `TWeakObjectPtr::Get()` 的实现细节
- [[UE-Core-源码解析：线程、任务与同步原语]] — `FSimpleDelegateGraphTask` 的任务图桥接
- [[UE-UMG-源码解析：UMG 蓝图与控件]] — Dynamic Multicast Delegate 在 UI 事件中的应用
- [[UE-GameplayAbilities-源码解析：GAS 技能系统]] — 大量委托驱动的 Gameplay 回调设计

---

## 索引状态

- **所属 UE 阶段**：第二阶段 - 基础层 / 2.2 内存、线程与任务
- **对应 UE 笔记**：UE-Core-源码解析：委托与事件系统
- **本轮分析完成度**：✅ 已完成三层分析（接口层、数据层、逻辑层）
