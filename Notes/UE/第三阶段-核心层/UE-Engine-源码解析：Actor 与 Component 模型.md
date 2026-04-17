---
title: UE-Engine-源码解析：Actor 与 Component 模型
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Engine Actor Component 模型
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：Actor 与 Component 模型

## Why：为什么要深入理解 Actor-Component？

Actor-Component 是 UE Gameplay 编程的核心范式。Actor 是场景中的"实体"，Component 是可复用的"行为/能力"。理解这套组合模型、生命周期和注册机制，是编写和调试 UE Gameplay 代码的基础。

## What：Actor 与 Component 模型是什么？

- **`AActor`**：可放置或生成在关卡中的对象。是 Component 的容器，负责网络复制、生命周期调度。
- **`UActorComponent`**：可附加到不同 Actor 上的可复用行为。管理注册、Tick、激活、初始化。
- **`USceneComponent`**：拥有变换（Transform）与附件（Attachment）能力的 Component。支持 Relative/World 空间的位置、旋转、缩放。
- **`UChildActorComponent`**：特殊的 SceneComponent，在注册时 Spawn 一个子 Actor，在注销时 Destroy 该子 Actor。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/`
- **Build.cs 文件**：`Engine.Build.cs`
- **关键目录**：
  - `Classes/GameFramework/Actor.h`
  - `Classes/Components/ActorComponent.h`
  - `Classes/Components/SceneComponent.h`
  - `Classes/Components/ChildActorComponent.h`
  - `Private/Actor.cpp`、`Private/Components/ActorComponent.cpp`、`Private/Components/SceneComponent.cpp`

---

## 接口梳理（第 1 层）

### 核心类继承关系

```
UObject
├── AActor
└── UActorComponent
    └── USceneComponent
        ├── UPrimitiveComponent
        └── UChildActorComponent
```

### 核心头文件一览

| 头文件 | 核心类 | 职责 |
|--------|--------|------|
| `Actor.h` | `AActor` | 场景实体，Component 容器，网络复制宿主 |
| `ActorComponent.h` | `UActorComponent` | 可复用行为基类，注册/Tick/激活管理 |
| `SceneComponent.h` | `USceneComponent` | 带变换的组件，支持 Attach/Detach |
| `ChildActorComponent.h` | `UChildActorComponent` | 嵌套 Actor 组件 |

### AActor 核心成员

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`

```cpp
class ENGINE_API AActor : public UObject
{
    GENERATED_BODY()
public:
    TSet<TObjectPtr<UActorComponent>> OwnedComponents;
    TObjectPtr<USceneComponent> RootComponent;
    TArray<TObjectPtr<UActorComponent>> InstanceComponents;
    TArray<TObjectPtr<UActorComponent>> BlueprintCreatedComponents;
    FActorTickFunction PrimaryActorTick;
};
```

---

## 数据结构（第 2 层）

### Actor-Component 组合模式

```
┌─────────────────────────────┐
│           AActor            │
│  ─────────────────────────  │
│  OwnedComponents: TSet<...> │
│  RootComponent: USceneComp* │
│  InstanceComponents         │
│  BlueprintCreatedComponents │
└─────────────┬───────────────┘
              │ owns
              ▼
┌─────────────────────────────┐
│      UActorComponent        │
│  ─────────────────────────  │
│  OwnerPrivate: AActor*      │
│  WorldPrivate: UWorld*      │
│  bRegistered: bool          │
│  PrimaryComponentTick       │
└─────────────┬───────────────┘
              │ extends
              ▼
┌─────────────────────────────┐
│      USceneComponent        │
│  ─────────────────────────  │
│  RelativeLocation/Rotation  │
│  ComponentToWorld           │
│  AttachParent               │
│  AttachChildren             │
└─────────────────────────────┘
```

### 生命周期状态位

| 状态位 | 含义 |
|--------|------|
| `bActorInitialized` | `PreInitializeComponents` / `PostInitializeComponents` 已调用 |
| `ActorHasBegunPlay` | `BeginPlay` 是否已开始或已完成 |
| `bHasFinishedSpawning` | `FinishSpawning` 是否已调用 |
| `bHasRegisteredAllComponents` | `PostRegisterAllComponents` 已调用 |

---

## 行为分析（第 3 层）

### Actor 生命周期流程

```
SpawnActor / Load
    ├── PostLoad (仅加载)
    ├── OnComponentCreated (native/蓝图组件)
    ├── PreRegisterAllComponents
    ├── RegisterComponent (创建渲染/物理状态)
    ├── PostRegisterAllComponents
    ├── PostActorCreated
    ├── UserConstructionScript (蓝图构造脚本)
    ├── OnConstruction
    ├── PreInitializeComponents
    ├── InitializeComponent
    ├── PostInitializeComponents
    ├── BeginPlay / DispatchBeginPlay
    ├── TickActor
    └── EndPlay → DestroyActor
```

### 关键函数 1：AActor::RegisterAllComponents

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`

```cpp
void AActor::RegisterAllComponents()
{
    PreRegisterAllComponents();
    for (UActorComponent* Component : OwnedComponents)
    {
        if (!Component->IsRegistered())
        {
            Component->RegisterComponent();
        }
    }
    PostRegisterAllComponents();
}
```

### 关键函数 2：UActorComponent::RegisterComponent

> 文件：`Engine/Source/Runtime/Engine/Private/Components/ActorComponent.cpp`

```cpp
void UActorComponent::RegisterComponent()
{
    // 加入 OwnedComponents
    // OnPreRegister()
    // OnRegister() — 创建渲染/物理状态
    // CreateRenderState_Concurrent()
    // OnCreatePhysicsState()
}
```

### 关键函数 3：AActor::DestroyActor

```cpp
void AActor::DestroyActor(bool bNetForce, bool bShouldModifyLevel)
{
    // RouteEndPlay()
    // UnregisterAllComponents()
    // MarkAsGarbage()
}
```

### Component 注册与注销 API

| API | 作用 |
|-----|------|
| `RegisterComponent()` | 注册到世界，创建渲染/物理状态 |
| `UnregisterComponent()` | 注销，销毁渲染/物理状态 |
| `ReregisterComponent()` | 先 Unregister 再 Register |
| `DestroyComponent()` | Unregister + 从 OwnedComponents 移除 + MarkAsGarbage |

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `UnrealEd` | 编辑器创建/修改 Actor 和 Component |
| `Gameplay` | 蓝图和 C++ 中 SpawnActor、AddComponent |
| `LevelStreaming` | 流送 Level 时批量注册/注销 Actor 的 Components |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `CoreUObject` | UObject 生命周期、Outer 层级 |
| `RenderCore` | 创建/销毁渲染状态 |
| `PhysicsCore` | 创建/销毁物理状态 |

---

## 设计亮点与可迁移经验

1. **组合优于继承**：Actor 本身几乎没有行为，所有能力都通过 Component 组合获得。这种模式极大地提高了代码复用率。
2. **注册即创建状态**：`RegisterComponent` 不是简单的标记，而是真正创建渲染/物理表现。这种"延迟创建"模式在流送和编辑器中非常有用。
3. **RootComponent 决定 Actor 变换**：Actor 在世界中的位置完全由 `RootComponent` 决定。其他 SceneComponent 通过 Attach 关系形成层级场景图。
4. **ChildActorComponent 的嵌套 Actor**：在 Component 层级中嵌套其他 Actor，解决了"Actor 内嵌 Actor"的组合需求（如武器挂点、载具座位）。

---

## 关键源码片段

### AActor::PostInitializeComponents

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`

```cpp
void AActor::PostInitializeComponents()
{
    bActorInitialized = true;
    // 子类可重写以在组件初始化完成后执行自定义逻辑
}
```

### UChildActorComponent::OnRegister

> 文件：`Engine/Source/Runtime/Engine/Private/Components/ChildActorComponent.cpp`

```cpp
void UChildActorComponent::OnRegister()
{
    Super::OnRegister();
    if (ChildActorClass && !ChildActor)
    {
        ChildActor = GetWorld()->SpawnActor<AActor>(ChildActorClass, ...);
        ChildActor->AttachToActor(GetOwner(), FAttachmentTransformRules::KeepRelativeTransform);
    }
}
```

---

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]]
- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]]
- [[UE-Engine-源码解析：场景图与变换传播]]
- [[UE-Engine-源码解析：GameFramework 与规则体系]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-Engine-源码解析：Actor 与 Component 模型
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17
