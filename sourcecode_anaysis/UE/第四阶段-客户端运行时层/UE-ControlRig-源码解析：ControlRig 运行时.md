---
title: UE-ControlRig-源码解析：ControlRig 运行时
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - control-rig
  - animation
  - rig-vm
aliases:
  - ControlRig 运行时
---

> [← 返回 [[00-UE全解析主索引|UE全解析主索引]]]

## Why：为什么要学习 ControlRig 运行时？

- **动画管线核心**：ControlRig 是 UE 现代动画系统的关键组件，负责在运行时通过节点图（Rig Graph）驱动骨骼、控制器与约束，实现程序化的角色动画与绑定逻辑。
- **替代传统 IK/FK**：相比传统的 Animation Blueprint 节点堆叠，ControlRig 提供了更直观的图形式绑定与解算方案，支持 Construction、Forward Solve、Inverse Solve 等多阶段事件驱动。
- **与引擎架构对齐**：ControlRig 基于 [[RigVM]] 构建，其宿主-VM-层级（Host-VM-Hierarchy）三层架构是理解 UE 脚本化运行时系统的最佳案例之一。

## What：ControlRig 运行时是什么？

ControlRig 运行时是 `Plugins/Animation/ControlRig` 插件中负责**执行 Rig 图**的核心模块。它在每一帧接收动画输入，通过 VM 执行用户定义的节点链，最终输出骨骼姿态（Pose）到 AnimGraph。其核心由以下三部分构成：

1. **宿主层（UControlRig / URigVMHost）**：管理 VM 生命周期、变量、时间、事件队列。
2. **层级层（URigHierarchy）**：管理 Bone / Control / Null / Curve / Connector 等元素的层级与变换。
3. **节点层（FRigUnit / FRigUnitMutable）**：Rig 图的最小执行单元，通过 `ExecutePin` 链式驱动。

## How：ControlRig 运行时的实现原理

---

### 一、接口层：模块边界与 Public 目录

#### 1.1 模块位置

```
Engine/Plugins/Animation/ControlRig/Source/ControlRig/
```

#### 1.2 Public 目录结构

```
Source/ControlRig/Public/
├── ControlRig.h                 # UControlRig 主类
├── AnimNode_ControlRig.h        # AnimGraph 节点
├── ControlRigComponent.h        # 场景组件封装
├── ModularRig.h                 # 模块化 Rig
├── Rigs/                        # URigHierarchy、元素定义
├── Units/                       # RigUnit 节点定义
│   ├── RigUnit.h
│   ├── RigUnitContext.h
│   └── Execution/               # 事件入口节点
├── Math/                        # 数学与物理模拟
├── Sequencer/                   # 序列器集成
└── Tools/                       # Pose、Mapping 工具
```

#### 1.3 Build.cs 依赖关系

**文件**：`Engine/Plugins/Animation/ControlRig/Source/ControlRig/ControlRig.Build.cs`

- **Private 依赖**：`Core`, `CoreUObject`, `Engine`, `AnimGraphRuntime`, `MovieScene`, `MovieSceneTracks`, `PropertyPath`, `TimeManagement`, `DeveloperSettings`, `SlateCore`
- **Public 依赖**：`AnimationCore`, `LevelSequence`, `RigVM`, `RHI`, `Constraints`
- **Editor 额外**：`Slate`, `RigVMDeveloper`, `AnimGraph`, `Json`, `Serialization`, `UnrealEd`, `BlueprintGraph` 等

> 可见 ControlRig 是动画管线的**中枢节点**，同时对接了动画图（AnimGraphRuntime）、序列器（MovieScene）、虚拟机（RigVM）和渲染层（RHI）。

---

### 二、数据层：核心类与数据结构

#### 2.1 UControlRig — Rig 图运行宿主

**文件**：`Public/ControlRig.h`（第 59–1107 行）

```cpp
// ControlRig.h:59
UCLASS(MinimalAPI, Blueprintable, Abstract, editinlinenew)
class UControlRig : public URigVMHost, public INodeMappingProviderInterface, public IRigHierarchyProvider, public IMeshDeformerProducer
{
    GENERATED_UCLASS_BODY()
    // ...
};
```

- 继承自 `URigVMHost`，因此天然拥有 VM 生命周期管理能力（`InitializeVM`、`ExecuteVM`）。
- 通过 `GetHierarchy()` 持有 `URigHierarchy* DynamicHierarchy`（第 480 行）。
- 关键执行接口：
  - `Evaluate_AnyThread()`（第 168 行）— 每帧评估入口。
  - `Execute(const FName& InEventName)`（第 275 行）— 事件执行。
  - `Execute_Internal(const FName& InEventName)`（第 276 行）— 内部真正调用 VM。

#### 2.2 URigHierarchy — 层级容器

**文件**：`Public/Rigs/RigHierarchy.h`（第 166 行起）

```cpp
// Rigs/RigHierarchy.h:166
UCLASS(MinimalAPI, BlueprintType)
class URigHierarchy : public UObject
{
    GENERATED_BODY()
    // ...
};
```

`URigHierarchy` 统一管理五类元素，每种元素继承自 `FRigBaseElement`：

| 元素类型 | 结构体 | 说明 |
|---------|--------|------|
| Bone | `FRigBoneElement` | 骨骼，继承 `FRigSingleParentElement` |
| Control | `FRigControlElement` | 控制器，可操纵、带 Gizmo |
| Null | `FRigNullElement` | 空节点，用于空间定位与父子关系 |
| Curve | `FRigCurveElement` | 曲线值（如 Morph Target 权重） |
| Connector | `FRigConnectorElement` | 模块化 Rig 的连接点 |

**文件**：`Public/Rigs/RigHierarchyElements.h`

```cpp
// Rigs/RigHierarchyElements.h:802
USTRUCT(BlueprintType)
struct FRigBaseElement { /* ... BaseElement, TransformElement, BoneElement... */ };

// Rigs/RigHierarchyElements.h:1313
struct FRigBoneElement : public FRigSingleParentElement { /* ... */ };

// Rigs/RigHierarchyElements.h:1355
struct FRigNullElement final : public FRigMultiParentElement { /* ... */ };

// Rigs/RigHierarchyElements.h:1725
struct FRigControlElement final : public FRigMultiParentElement { /* ... */ };

// Rigs/RigHierarchyElements.h:1838
struct FRigCurveElement final : public FRigBaseElement { /* ... */ };
```

`FRigControlElement` 额外持有 `FRigControlSettings Settings` 和 `FRigPreferredEulerAngles PreferredEulerAngles`，用于处理控制器的空间类型、轴约束与万向节翻转。

#### 2.3 FRigUnit / FRigUnitMutable — 节点基类

**文件**：`Public/Units/RigUnit.h`

```cpp
// Units/RigUnit.h:57
USTRUCT(BlueprintType, meta=(Abstract, NodeColor = "0.1 0.1 0.1", ExecuteContext="FControlRigExecuteContext"))
struct FRigUnit : public FRigVMStruct
{
    GENERATED_BODY()
    static FName GetMethodName()
    {
        static const FLazyName MethodName = FRigVMStruct::ExecuteName;
        return MethodName;
    }
};

// Units/RigUnit.h:108
USTRUCT(BlueprintType, meta = (Abstract))
struct FRigUnitMutable : public FRigUnit
{
    GENERATED_BODY()
    UPROPERTY(DisplayName = "Execute", Transient, meta = (Input, Output))
    FRigVMExecutePin ExecutePin;  // 用于链式执行多个 mutable 单元
};
```

- `FRigUnit` 是所有 Rig 节点的基类，继承自 `FRigVMStruct`，意味着每个 RigUnit 都会被 RigVM 识别为可执行结构体。
- `FRigUnitMutable` 增加了 `ExecutePin`，用于在图中将多个带副作用的节点串联成执行链。

#### 2.4 FAnimNode_ControlRig — AnimGraph 集成节点

**文件**：`Public/AnimNode_ControlRig.h`

```cpp
// AnimNode_ControlRig.h:19
USTRUCT()
struct FAnimNode_ControlRig : public FAnimNode_ControlRigBase
{
    GENERATED_BODY()
    virtual void Evaluate_AnyThread(FPoseContext & Output) override;
    // ...
    UPROPERTY(EditAnywhere, Category = ControlRig)
    TSubclassOf<UControlRig> ControlRigClass;
    UPROPERTY(transient)
    TObjectPtr<UControlRig> ControlRig;
};
```

`FAnimNode_ControlRig` 是 AnimGraph 与 ControlRig 的**桥梁**：
- 在 `Initialize_AnyThread` 中实例化 `UControlRig`。
- 在 `Evaluate_AnyThread` 中通过 `UpdateInput` 将骨骼姿态写入 Hierarchy，触发 `ControlRig->Evaluate_AnyThread()`，再通过 `UpdateOutput` 读回姿态。

#### 2.5 FControlRigExecuteContext — 执行上下文

**文件**：`Public/Units/RigUnitContext.h`

```cpp
// Units/RigUnitContext.h:123
USTRUCT(BlueprintType)
struct FControlRigExecuteContext : public FRigVMExecuteContext
{
    GENERATED_BODY()
    // ...
    FRigUnitContext UnitContext;
    URigHierarchy* Hierarchy;
    UControlRig* ControlRig;
    // RigModulePrefix、AssetUserData、Instruction Records 等
};
```

上下文携带了当前执行所需的全部运行时状态：Hierarchy 指针、ControlRig 指针、模块前缀、动画属性容器、交互类型等。每个 `FRigUnit::Execute()` 都通过上下文访问外部数据。

---

### 三、逻辑层：执行流程与事件驱动模型

#### 3.1 执行流程总览

```
FAnimNode_ControlRigBase::Evaluate_AnyThread()
    ├── ExecuteConstructionIfNeeded()          //  Construction Event
    └── ExecuteControlRig()
            ├── UpdateInput()                  //  Pose -> Hierarchy
            ├── ControlRig->Evaluate_AnyThread()
            │       └── UControlRig::Execute(InEventName)
            │               ├── Construction: Execute_Internal(PrepareForExecution)
            │               └── Forward/Interaction/Inverse: Execute_Internal(EventName)
            │                       └── VM->ExecuteVM(Context, InEventName)
            └── UpdateOutput()                 //  Hierarchy -> Pose
```

#### 3.2 UControlRig::Evaluate_AnyThread 的实现

**文件**：`Private/ControlRig.cpp`（第 516–662 行）

```cpp
// Private/ControlRig.cpp:516
void UControlRig::Evaluate_AnyThread()
{
    if (bIsAdditive)
    {
        // 1. 加性 Rig：先 backward solve，再叠加控制值，最后 forward solve
        {
            UE::TScopeLock EvaluateLock(GetEvaluateMutex());
            
            // 重置 control 到初始姿态
            Hierarchy->ForEach([...](FRigBaseElement* Element) -> bool { ... });
            
            // Backwards solve
            Execute(FRigUnit_InverseExecution::EventName);
            
            // 应用 additive controls
            for (TPair<FRigElementKey, FRigSetControlValueInfo>& Value : ControlValues)
            {
                // 叠加变换到骨骼动画姿态上
            }
            
            // Interaction solve
            if (InteractionType != (uint8)EControlRigInteractionType::None)
            {
                Execute(FRigUnit_InteractionExecution::EventName);
            }
            
            // Forward solve
            Execute(FRigUnit_PreBeginExecution::EventName);
            Execute(FRigUnit_BeginExecution::EventName);
            Execute(FRigUnit_PostBeginExecution::EventName);
        }
    }
    else
    {
        // 非加性：直接走 URigVMHost 的默认事件队列
        Super::Evaluate_AnyThread();
    }
}
```

> **关键洞察**：加性（Additive）Rig 需要在 Forward Solve 之前运行 Backward Solve，以便从动画姿态反推出 control 的 additive 偏移。

#### 3.3 UControlRig::Execute 与 Execute_Internal

**文件**：`Private/ControlRig.cpp`

`Execute`（第 871–960 行）：

```cpp
// Private/ControlRig.cpp:871
bool UControlRig::Execute(const FName& InEventName)
{
    if(!CanExecute()) return false;
    
    // 嵌套 rig 不应直接调用 Execute，除非 PrepareForExecution
    if (InEventName != FRigUnit_PrepareForExecution::EventName)
    {
        ensureMsgf(GetTypedOuter<UControlRig>() == nullptr, ...);
    }
    
    // 初始化 VM（若需要）
    if(bRequiresInitExecution)
    {
        if(!InitializeVM(InEventName)) return false;
    }
    
    // 构建上下文
    FRigVMExtendedExecuteContext& ExtendedExecuteContext = GetRigVMExtendedExecuteContext();
    FControlRigExecuteContext& PublicContext = ExtendedExecuteContext.GetPublicDataSafe<FControlRigExecuteContext>();
    PublicContext.SetDeltaTime(DeltaTime);
    PublicContext.SetAbsoluteTime(AbsoluteTime);
    
    // ... 事件预处理（Construction / Forward / Interaction 等分支）
    
    return Execute_Internal(InEventName);
}
```

`Execute_Internal`（第 1681–1779 行）：

```cpp
// Private/ControlRig.cpp:1681
bool UControlRig::Execute_Internal(const FName& InEventName)
{
    if(!SupportsEvent(InEventName)) return false;
    
    if(bRequiresInitExecution)
    {
        if(!InitializeVM(InEventName)) return false;
    }
    
    if(IsRigModule() && InEventName != FRigUnit_ConnectorExecution::EventName)
    {
        if(!AllConnectorsAreResolved(&ConnectorWarning)) return false;
    }
    
    if (VM)
    {
        FRigVMExtendedExecuteContext& Context = GetRigVMExtendedExecuteContext();
        URigHierarchy* Hierarchy = GetHierarchy();
        FRigHierarchyExecuteContextBracket HierarchyContextGuard(Hierarchy, &Context);
        
        FControlRigExecuteContext& PublicContext = Context.GetPublicDataSafe<FControlRigExecuteContext>();
        FControlRigExecuteContextRigModuleGuard RigModuleGuard(PublicContext, this);
        
        const bool bSuccess = VM->ExecuteVM(Context, InEventName) != ERigVMExecuteResult::Failed;
        return bSuccess;
    }
    return false;
}
```

> `Execute_Internal` 是**真正触发 VM 执行**的入口。它会设置 Hierarchy 上下文守卫、RigModule 前缀守卫，最终调用 `VM->ExecuteVM(Context, InEventName)`。

#### 3.4 事件驱动模型与典型入口节点

ControlRig 采用**多事件队列**模型，不同事件对应不同的解算阶段：

| 事件 | 入口节点 | 用途 |
|------|---------|------|
| Construction | `FRigUnit_PrepareForExecution` | 创建/配置元素、重置 Hierarchy 到初始姿态 |
| PostConstruction | `FRigUnit_PostPrepareForExecution` | Construction 之后的二次配置 |
| Forward Solve | `FRigUnit_BeginExecution` | 主解算：根据 control 驱动骨骼 |
| Pre Forward Solve | `FRigUnit_PreBeginExecution` | Forward Solve 前的预处理 |
| Post Forward Solve | `FRigUnit_PostBeginExecution` | Forward Solve 后的后处理 |
| Interaction | `FRigUnit_InteractionExecution` | 用户交互（拖拽 control）时触发 |
| Backwards Solve | `FRigUnit_InverseExecution` | 从骨骼姿态反推 control 值（加性 rig） |

**文件**：`Public/Units/Execution/RigUnit_BeginExecution.h`

```cpp
// Units/Execution/RigUnit_BeginExecution.h:14
USTRUCT(meta=(DisplayName="Forwards Solve", Category="Events", ...))
struct FRigUnit_BeginExecution : public FRigUnit
{
    virtual FName GetEventName() const override { return EventName; }
    static inline const FLazyName EventName = FLazyName(TEXT("Forwards Solve"));
    UPROPERTY(EditAnywhere, Transient, DisplayName = "Execute", meta = (Output))
    FRigVMExecutePin ExecutePin;
};
```

每个事件节点输出一个 `FRigVMExecutePin`，后续 mutable 节点通过该 Pin 串联执行。

#### 3.5 RigUnit 节点链式执行（ExecutePin）

在 RigVM 的编译结果中，mutable 节点按依赖拓扑排序，`FRigVMExecutePin` 充当**执行流边（control edge）**。VM 在执行时按顺序调用每个节点的 `Execute()` 函数，保证带副作用的节点不会并发冲突。

```
[BeginExecution] --ExecutePin--> [RigUnit_Mutable_A] --ExecutePin--> [RigUnit_Mutable_B] --ExecutePin--> ...
```

#### 3.6 与 AnimGraph 的集成

**文件**：`Private/AnimNode_ControlRigBase.cpp`（第 172–331 行）

```cpp
// Private/AnimNode_ControlRigBase.cpp:172
void FAnimNode_ControlRigBase::Evaluate_AnyThread(FPoseContext& Output)
{
    FPoseContext SourcePose(Output);
    if (Source.GetLinkNode())
    {
        Source.Evaluate(SourcePose);
    }
    else
    {
        SourcePose.ResetToRefPose();
    }
    
    ExecuteConstructionIfNeeded();
    
    if (CanExecute() && FAnimWeight::IsRelevant(InternalBlendAlpha) && GetControlRig())
    {
        if (FAnimWeight::IsFullWeight(InternalBlendAlpha))
        {
            ExecuteControlRig(SourcePose);
            Output = SourcePose;
        }
        else 
        {
            // 按权重做加性混合
            FPoseContext ControlRigPose(SourcePose);
            ExecuteControlRig(ControlRigPose);
            // ConvertToAdditive + AccumulateAdditivePose
        }
    }
    else
    {
        Output = SourcePose;
    }
}
```

`ExecuteControlRig` 的详细流程（第 231–331 行）：

```cpp
void FAnimNode_ControlRigBase::ExecuteControlRig(FPoseContext& InOutput)
{
    if (UControlRig* ControlRig = GetControlRig())
    {
        UE::TScopeLock LockRig(ControlRig->GetEvaluateMutex());
        
        // 1. 将动画属性容器挂载到 ControlRig
        UControlRig::FAnimAttributeContainerPtrScope AttributeScope(ControlRig, MeshAttributeContainer);
        
        // 2. 将 Pose / Curve 写入 Hierarchy
        UpdateInput(ControlRig, InOutput);
        
        if (bExecute)
        {
            // 3. 设置事件队列并执行
            ControlRig->SetEventQueue(EventNames);
            ControlRig->Evaluate_AnyThread();
        }
        
        // 4. 将 Hierarchy 结果写回 Pose
        UpdateOutput(ControlRig, InOutput);
        InOutput.CustomAttributes.CopyFrom(MeshAttributeContainer, ...);
    }
}
```

> **线程安全**：AnimNode 与 ControlRig 之间通过 `ControlRig->GetEvaluateMutex()` 加锁，避免多线程评估时 Hierarchy 被并发修改。

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
