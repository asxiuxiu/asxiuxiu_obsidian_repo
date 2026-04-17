---
title: UE-Engine-源码解析：场景图与变换传播
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Engine 场景图 变换传播
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：场景图与变换传播

## Why：为什么要深入理解场景图与变换传播？

场景图（Scene Graph）是任何 3D 引擎的核心数据结构。UE 中 `USceneComponent` 通过 `AttachParent` / `AttachChildren` 构建的层级关系，支撑了从角色骨骼挂点到整个关卡物体层级的一切变换需求。理解变换传播的调用链、Attach/Detach 的时序和边界刷新机制，是优化场景更新性能和排查坐标异常的关键。

## What：场景图与变换传播是什么？

- **`USceneComponent`**：场景图节点基类。维护 `RelativeTransform`、`ComponentToWorld`（缓存的世界变换）、`AttachParent`、`AttachChildren`。
- **变换传播**：当父节点或自身 `RelativeTransform` 改变时，通过 `UpdateComponentToWorld` → `PropagateTransformUpdate` → `UpdateChildTransforms` 递归刷新子节点。
- **Attach/Detach**：通过 `AttachToComponent` / `DetachFromComponent` 动态调整场景图层级，并应用 `FAttachmentTransformRules` / `FDetachmentTransformRules`。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/`
- **Build.cs 文件**：`Engine.Build.cs`
- **关键目录**：
  - `Classes/Components/SceneComponent.h` / `Private/Components/SceneComponent.cpp`
  - `Classes/Components/PrimitiveComponent.h` / `Private/Components/PrimitiveComponent.cpp`
  - `Classes/Engine/EngineTypes.h` — `FAttachmentTransformRules`

---

## 接口梳理（第 1 层）

### 核心类一览

| 类名 | 路径 | 职责 |
|------|------|------|
| `USceneComponent` | `Classes/Components/SceneComponent.h` | 场景图节点，维护 RelativeTransform 和 Attach 关系 |
| `UPrimitiveComponent` | `Classes/Components/PrimitiveComponent.h` | 继承 USceneComponent，增加渲染/碰撞几何体，重写 `OnUpdateTransform` |
| `FAttachmentTransformRules` | `Classes/Engine/EngineTypes.h` | Attach 规则：KeepRelative / KeepWorld / SnapToTarget |
| `FDetachmentTransformRules` | `Classes/Engine/EngineTypes.h` | Detach 规则：KeepRelative / KeepWorld |

### USceneComponent 核心成员

> 文件：`Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h`，第 107~361 行

```cpp
class ENGINE_API USceneComponent : public UActorComponent
{
    FVector RelativeLocation;
    FRotator RelativeRotation;
    FVector RelativeScale3D;
    FTransform ComponentToWorld;
    TObjectPtr<USceneComponent> AttachParent;
    FName AttachSocketName;
    TArray<TObjectPtr<USceneComponent>> AttachChildren;
    bool bAbsoluteLocation;
    bool bAbsoluteRotation;
    bool bAbsoluteScale;
};
```

---

## 数据结构（第 2 层）

### ComponentToWorld 缓存

`ComponentToWorld` 是 `USceneComponent` 的**世界变换缓存**，由 `UpdateComponentToWorld()` 计算并填充。任何需要世界坐标的操作（如渲染、碰撞、射线检测）都直接读取这个缓存，而不是每次都递归计算。

### Attach 关系的数据结构

```cpp
USceneComponent* AttachParent;                    // 直接父节点
FName AttachSocketName;                          // 挂载到父节点的哪个 Socket
TArray<TObjectPtr<USceneComponent>> AttachChildren; // 直接子节点数组
```

### 绝对变换标志

| 标志 | 含义 |
|------|------|
| `bAbsoluteLocation` | `RelativeLocation` 实际按世界空间解释 |
| `bAbsoluteRotation` | `RelativeRotation` 实际按世界空间解释 |
| `bAbsoluteScale` | `RelativeScale3D` 实际按世界空间解释 |

---

## 行为分析（第 3 层）

### 变换传播调用链

```
SetRelativeLocationAndRotation / SetWorldLocation / MoveComponentImpl
    └── UpdateComponentToWorldWithParent
            ├── 计算 RelativeTransform → NewTransform
            ├── 比较 ComponentToWorld 是否变化
            └── PropagateTransformUpdate(bTransformChanged, Flags, Teleport)
                    ├── UpdateBounds()
                    ├── OnUpdateTransform()           // 虚回调
                    ├── TransformUpdated.Broadcast()  // 委托广播
                    ├── MarkRenderTransformDirty()    // 渲染脏标记
                    └── UpdateChildTransforms()       // 递归更新子组件
                            └── 对每个 Child: UpdateComponentToWorld(...)
```

### 关键函数 1：PropagateTransformUpdate

> 文件：`Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp`，第 953~1060 行

```cpp
void USceneComponent::PropagateTransformUpdate(bool bTransformChanged, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    if (IsDeferringMovementUpdates())
    {
        return; // 推迟到 ScopedMovement 结束
    }

    if (bTransformChanged)
    {
        UpdateBounds();
        OnUpdateTransform(UpdateTransformFlags, Teleport);
        TransformUpdated.Broadcast(...);
        MarkRenderTransformDirty();
        UpdateChildTransforms(ChildrenFlagNoPhysics, Teleport);
        UpdateNavigationData();
    }
    else
    {
        UpdateBounds();
        UpdateChildTransforms();
        MarkRenderTransformDirty();
    }
}
```

### 关键函数 2：UpdateChildTransforms

> 文件：`Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp`，第 2882~2928 行

```cpp
void USceneComponent::UpdateChildTransforms(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    for (USceneComponent* ChildComp : GetAttachChildren())
    {
        if (ChildComp->IsUsingAbsoluteLocation() && ChildComp->IsUsingAbsoluteRotation() && ChildComp->IsUsingAbsoluteScale())
        {
            continue; // 跳过完全使用 Absolute 坐标的子组件
        }
        ChildComp->UpdateComponentToWorld(UpdateTransformFlagsFromParent, Teleport);
    }
}
```

### 关键函数 3：AttachToComponent

> 文件：`Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp`，第 2296~2620 行

```cpp
bool USceneComponent::AttachToComponent(USceneComponent* Parent, const FAttachmentTransformRules& AttachmentRules, FName InSocketName)
{
    // 1. 前置校验（环检测、Static→Dynamic 非法、Template 匹配）
    // 2. DetachFromComponent(旧父节点)
    // 3. 建立父子链接
    SetAttachParent(Parent);
    SetAttachSocketName(InSocketName);
    Parent->AttachChildren.Add(this);
    // 4. 应用 AttachmentRules（KeepRelative / KeepWorld / SnapToTarget）
    // 5. UpdateComponentToWorld() 强制刷新
    // 6. 可选 WeldToImplementation
}
```

### UPrimitiveComponent::OnUpdateTransform

> 文件：`Engine/Source/Runtime/Engine/Private/Components/PrimitiveComponent.cpp`，第 1028 行

```cpp
void UPrimitiveComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
    if (bPhysicsStateCreated && !(UpdateTransformFlags & EUpdateTransformFlags::SkipPhysicsUpdate))
    {
        if (bTransformSetDirectly || !IsWelded())
        {
            SendPhysicsTransform(Teleport);   // 同步变换到物理引擎
        }
    }
}
```

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `Gameplay` | 角色移动、相机跟随、武器挂点 Attach |
| `Animation` | 骨骼 Socket 作为 Attach 目标 |
| `Editor` | 编辑器中拖动 Actor，触发 Transform 更新 |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `Core` | `FTransform`、`FVector`、`FRotator` 等数学类型 |
| `PhysicsCore` | `SendPhysicsTransform` 同步到物理体 |
| `RenderCore` | `MarkRenderTransformDirty` 触发渲染脏标记 |

---

## 设计亮点与可迁移经验

1. **缓存世界变换**：`ComponentToWorld` 作为缓存，避免了每次使用时递归计算父链，是性能优化的经典手段。
2. **脏标记驱动更新**：`MarkRenderTransformDirty` 等机制将"变换计算"与"渲染提交"解耦，只在真正需要时刷新 GPU 数据。
3. **延迟更新（ScopedMovement）**：`IsDeferringMovementUpdates()` 支持批量变换更新，避免中间状态的多次传播。
4. **AttachRules 的显式语义**：`KeepRelative` / `KeepWorld` / `SnapToTarget` 让 Attach 行为可预测，减少了坐标"跳变"的 bug。
5. **Absolute 标志的灵活性**：允许子组件在局部层级中仍使用世界坐标，适用于 UI、特效等不需要跟随父级旋转/缩放的场景。

---

## 关键源码片段

### CalcNewComponentToWorld

> 文件：`Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp`，第 715~742 行

```cpp
inline FTransform CalcNewComponentToWorld(const FTransform& NewRelativeTransform, const USceneComponent* Parent, FName SocketName) const
{
    if (Parent)
    {
        return NewRelativeTransform * Parent->GetSocketTransform(SocketName);
    }
    return NewRelativeTransform;
}
```

### DetachFromComponent

> 文件：`Engine/Source/Runtime/Engine/Private/Components/SceneComponent.cpp`，第 2651~2727 行

```cpp
void USceneComponent::DetachFromComponent(const FDetachmentTransformRules& DetachmentRules)
{
    UnWeldFromParent();
    if (AttachParent)
    {
        AttachParent->AttachChildren.Remove(this);
        AttachParent->OnChildDetached(this);
    }
    SetAttachParent(nullptr);
    SetAttachSocketName(NAME_None);
    // 应用 DetachmentRules...
}
```

---

## 关联阅读

- [[UE-Engine-源码解析：Actor 与 Component 模型]]
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]]
- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-Core-源码解析：数学库与 SIMD]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-Engine-源码解析：场景图与变换传播
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17
