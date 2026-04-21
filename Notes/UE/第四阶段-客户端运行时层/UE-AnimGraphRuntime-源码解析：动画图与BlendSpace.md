---
title: UE-AnimGraphRuntime-源码解析：动画图与 BlendSpace
date: 2026-04-17
tags: [ue-source, engine-architecture, animation, blendspace, anim-graph]
aliases: [AnimGraphRuntime 源码解析]
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-AnimGraphRuntime 源码解析：动画图与 BlendSpace

## Why：为什么要分析 AnimGraphRuntime？

- **动画系统是游戏角色的灵魂**：现代 3A 游戏中，角色动画的复杂度远超单纯播放一段序列，需要状态机、混合空间、IK、物理模拟等协同工作。
- **AnimGraphRuntime 是运行时节点库**：它承载了所有在 AnimBlueprint 中可见的"动画节点"的实际运行逻辑，是连接编辑器可视化编排与底层 pose 评估的桥梁。
- **BlendSpace 是角色移动动画的核心**：通过二维坐标（如速度和方向）平滑混合多个动画样本，实现自然过渡。理解其源码有助于优化角色移动表现、排查混合异常。
- **骨骼控制器的多线程安全设计**：SkeletalControl（如 IK、FABRIK、RigidBody）必须在任意线程安全执行，其 `EvaluateSkeletalControl_AnyThread` 模式是自研引擎可借鉴的典范。

## What：AnimGraphRuntime 是什么？

AnimGraphRuntime 是 UE 动画系统的**运行时节点库**，位于：

```
Engine/Source/Runtime/AnimGraphRuntime
```

它本身不生成动画图（由编辑器模块 `AnimGraph` 负责），也不负责顶层调度（由 `Engine` 模块的 `UAnimInstance` / `FAnimInstanceProxy` 负责），而是提供：

- **AnimNodes/**：所有在 AnimBlueprint 图里使用的节点（BlendSpacePlayer、SequenceEvaluator、LayeredBoneBlend、Slot 等）。
- **BoneControllers/**：所有在 AnimBlueprint 的 "Skeletal Control" 轨道里使用的节点（TwoBoneIK、FABRIK、CCDIK、RigidBody、AnimDynamics 等）。
- **蓝图函数库**：`UKismetAnimationLibrary`、`UBlendSpaceLibrary`、`USequencePlayerLibrary` 等，供蓝图/游戏逻辑直接调用。
- **Sequencer 专用实例**：`UAnimSequencerInstance` 及其 Proxy，用于在 Sequencer 和动画预览窗口中播放单条动画轨道。

## How：源码三层剥离分析

### 一、接口层：模块边界与 Public 头文件

#### 1.1 Build.cs 依赖关系

文件：`Engine/Source/Runtime/AnimGraphRuntime/AnimGraphRuntime.Build.cs`（第 6-38 行）

```csharp
public class AnimGraphRuntime : ModuleRules
{
    PublicDependencyModuleNames.AddRange(
        new string[] { 
            "Core", 
            "CoreUObject", 
            "Engine",
            "AnimationCore",
            "GeometryCollectionEngine",
        }
    );

    PrivateDependencyModuleNames.AddRange(
        new string[] {
            "TraceLog",
        }
    );

    AddEngineThirdPartyPrivateStaticDependencies(Target, "Eigen");
}
```

**关键依赖解读**：

- **Engine**：继承 `FAnimNode_Base`（定义在 Engine 模块）、使用 `FAnimInstanceProxy`、获取 `USkeletalMeshComponent`。
- **AnimationCore**：使用 `FBoneReference`、`FBoneContainer`、`FCompactPose` 等底层 pose 数据结构。
- **Eigen**：BoneControllers 中的数值求解（如 CCDIK、FABRIK）依赖 Eigen 矩阵运算。
- **TraceLog**：提供动画图执行期间的 trace 支持。

AnimGraphRuntime 自身 **不依赖 AnimGraph 编辑器模块**，这保证了运行时与编辑器的完全解耦，符合 UE 的 Runtime/Editor 分离原则。

#### 1.2 Public 目录结构

```
AnimGraphRuntime/Public/
├── AnimNodes/              # 动画图节点（继承 FAnimNode_Base）
│   ├── AnimNode_BlendSpacePlayer.h
│   ├── AnimNode_SequenceEvaluator.h
│   ├── AnimNode_TwoWayBlend.h
│   └── ...
├── BoneControllers/        # 骨骼控制器（继承 FAnimNode_SkeletalControlBase）
│   ├── AnimNode_SkeletalControlBase.h
│   ├── AnimNode_TwoBoneIK.h
│   ├── AnimNode_Fabrik.h
│   ├── AnimNode_CCDIK.h
│   ├── AnimNode_RigidBody.h
│   └── ...
├── AnimNotifies/           # 动画通知相关
├── RBF/                    # RBF 求解器（PoseDriver 等使用）
├── AnimSequencerInstance.h
├── AnimSequencerInstanceProxy.h
└── *Library.h              # 蓝图函数库
```

### 二、数据层：节点类体系与 Proxy 衔接

#### 2.1 动画图节点的基类层次

所有运行时动画节点的根基类定义在 Engine 模块中。

文件：`Engine/Source/Runtime/Engine/Classes/Animation/AnimNodeBase.h`（第 851-1114 行）

```cpp
USTRUCT()
struct FAnimNode_Base
{
    GENERATED_BODY()

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context);
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context);
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context);
    virtual void Evaluate_AnyThread(FPoseContext& Output);
    virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output);
    virtual void GatherDebugData(FNodeDebugData& DebugData);
    // ...
};
```

**继承链（以 BlendSpacePlayer 为例）**：

```
FAnimNode_Base
└── FAnimNode_AssetPlayerRelevancyBase
    └── FAnimNode_AssetPlayerBase        (Engine/Classes/Animation/AnimNode_AssetPlayerBase.h:14)
        └── FAnimNode_BlendSpacePlayerBase   (AnimGraphRuntime/Public/AnimNodes/AnimNode_BlendSpacePlayer.h:15)
            ├── FAnimNode_BlendSpacePlayer
            └── FAnimNode_BlendSpacePlayer_Standalone
```

#### 2.2 BlendSpace 播放器：FAnimNode_BlendSpacePlayer

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/AnimNodes/AnimNode_BlendSpacePlayer.h`

**FAnimNode_BlendSpacePlayerBase**（第 15-107 行）定义了 BlendSpace 播放的通用逻辑：

```cpp
USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_BlendSpacePlayerBase : public FAnimNode_AssetPlayerBase
{
    GENERATED_BODY()

protected:
    FBlendFilter BlendFilter;                           // 坐标变化的阻尼滤波器
    TArray<FBlendSampleData> BlendSampleDataCache;      // 样本混合权重缓存
    int32 CachedTriangulationIndex = -1;                // 三角剖分/分段缓存
    TObjectPtr<UBlendSpace> PreviousBlendSpace = nullptr;

public:
    // FAnimNode_Base 四阶段接口
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;

    // 纯虚接口，由子类提供具体资产和参数
    virtual UBlendSpace* GetBlendSpace() const PURE_VIRTUAL(...);
    virtual FVector GetPosition() const PURE_VIRTUAL(...);
    virtual float GetPlayRate() const PURE_VIRTUAL(...);
    virtual bool SetBlendSpace(UBlendSpace* InBlendSpace) PURE_VIRTUAL(...);
    virtual bool SetPosition(FVector InPosition) PURE_VIRTUAL(...);
    // ...
};
```

**FAnimNode_BlendSpacePlayer**（第 121-215 行）是蓝图图里实际使用的节点，持有可编辑的 UPROPERTY：

```cpp
USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_BlendSpacePlayer : public FAnimNode_BlendSpacePlayerBase
{
    GENERATED_BODY()

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = Coordinates)
    float X = 0.0f;

    UPROPERTY(EditAnywhere, Category = Coordinates)
    float Y = 0.0f;

    UPROPERTY(EditAnywhere, Category = Settings)
    float PlayRate = 1.0f;

    UPROPERTY(EditAnywhere, Category = Settings)
    bool bLoop = true;
#endif

    UPROPERTY(EditAnywhere, Category = Settings)
    TObjectPtr<UBlendSpace> BlendSpace = nullptr;
};
```

#### 2.3 序列求值器：FAnimNode_SequenceEvaluator

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/AnimNodes/AnimNode_SequenceEvaluator.h`

继承链与 BlendSpacePlayer 类似，最终也是继承 `FAnimNode_AssetPlayerBase`。其特点是通过 **显式时间（ExplicitTime）** 而非内部推进来求值，常用于 Sequencer、动画预览、程序化控制跳跃高度等场景。

```cpp
USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_SequenceEvaluatorBase : public FAnimNode_AssetPlayerBase
{
    // ...
    virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;

    virtual bool SetExplicitTime(float InTime) { return false; }
    virtual float GetExplicitTime() const { return 0.0f; }
    virtual bool GetTeleportToExplicitTime() const { return true; }
    // ...
};
```

#### 2.4 骨骼控制器基类：FAnimNode_SkeletalControlBase

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_SkeletalControlBase.h`（第 20-140 行）

```cpp
USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_SkeletalControlBase : public FAnimNode_Base
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
    FComponentSpacePoseLink ComponentPose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Performance)
    int32 LODThreshold;

    // Alpha 相关属性（Float / Bool / Curve 三种输入方式）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Alpha)
    EAnimAlphaInputType AlphaInputType;
    float Alpha;
    FInputScaleBias AlphaScaleBias;
    FInputAlphaBoolBlend AlphaBoolBlend;
    FName AlphaCurveName;
    FInputScaleBiasClamp AlphaScaleBiasClamp;

    // FAnimNode_Base 接口覆写
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) final;
    virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output) final;

protected:
    // 子类必须实现的核心求值函数
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms);
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) { return false; }
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones){};
};
```

**设计要点**：

- `Update_AnyThread` 和 `EvaluateComponentSpace_AnyThread` 被标记为 `final`，基类统一处理权重衰减、LOD 裁剪、子 pose 递归求值，子类只需关心骨骼变换逻辑。
- `EvaluateSkeletalControl_AnyThread` 在任意线程执行，输出 **局部骨骼变换数组** `TArray<FBoneTransform>`，由基类负责 blend 回原始 pose。

#### 2.5 Sequencer 专用实例

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/AnimSequencerInstance.h`（第 17-60 行）

```cpp
UCLASS(transient, NotBlueprintable, MinimalAPI)
class UAnimSequencerInstance : public UAnimInstance, public ISequencerAnimationSupport
{
    GENERATED_UCLASS_BODY()

    virtual void UpdateAnimTrack(UAnimSequenceBase* InAnimSequence, int32 SequenceId, float InPosition, float Weight, bool bFireNotifies);
    virtual void ConstructNodes() override;
    virtual void ResetNodes() override;
    virtual void ResetPose() override;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

public:
    static const FName SequencerPoseName;
};
```

对应的 Proxy：

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/AnimSequencerInstanceProxy.h`（第 117-200 行）

```cpp
USTRUCT()
struct FAnimSequencerInstanceProxy : public FAnimInstanceProxy
{
    GENERATED_BODY()

    virtual void Initialize(UAnimInstance* InAnimInstance) override;
    virtual bool Evaluate(FPoseContext& Output) override;
    virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

    // Sequencer 内部通过 SequenceId 管理多个动画轨道的 player
    TMap<uint32, FSequencerPlayerBase*> SequencerToPlayerMap;

    // 根节点组合器
    struct FAnimNode_ApplyAdditive SequencerRootNode;
    struct FAnimNode_MultiWayBlend FullBodyBlendNode;
    struct FAnimNode_MultiWayBlend AdditiveBlendNode;
    struct FAnimNode_PoseSnapshot SnapshotNode;
};
```

#### 2.6 FAnimInstanceProxy：动画图执行的上下文代理

文件：`Engine/Source/Runtime/Engine/Public/Animation/AnimInstanceProxy.h`（第 141-600+ 行）

`FAnimInstanceProxy` 是在动画树遍历过程中传递的上下文对象，替代了直接在 worker 线程访问 `UAnimInstance`（UObject 非线程安全）的设计：

```cpp
USTRUCT(meta = (DisplayName = "Native Variables"))
struct FAnimInstanceProxy
{
    GENERATED_USTRUCT_BODY()

    // 计数器：用于检测节点是否需要重新初始化/缓存骨骼/更新/求值
    const FGraphTraversalCounter& GetInitializationCounter() const;
    const FGraphTraversalCounter& GetCachedBonesCounter() const;
    const FGraphTraversalCounter& GetUpdateCounter() const;
    const FGraphTraversalCounter& GetEvaluationCounter() const;

    // 通过索引访问节点（用于 Linked Anim Graph、State Machine 跳转）
    FAnimNode_Base* GetMutableNodeFromIndexUntyped(int32 NodeIdx, UScriptStruct* RequiredStructType);
    template<class NodeType> NodeType* GetCheckedMutableNodeFromIndex(int32 NodeIdx) const;

    // 根节点
    FAnimNode_Base* GetRootNode();

    // Sync Group、TickRecord、Slot Weight 等运行时状态管理
    // ...
};
```

### 三、逻辑层：动画图四阶段与 BlendSpace 评估

#### 3.1 动画图四阶段生命周期

文件：`Engine/Source/Runtime/Engine/Classes/Animation/AnimNodeBase.h`（第 851-904 行）

FAnimNode_Base 定义了动画图节点的统一生命周期：

```cpp
// 1. 初始化：节点首次运行或从缓存 pose/状态机重新进入时调用
virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context);

// 2. 缓存骨骼：RequiredBones 变化（如 LOD 切换）时刷新骨骼索引
virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context);

// 3. 更新：计算 blend weight、推进时间、同步组对齐等
virtual void Update_AnyThread(const FAnimationUpdateContext& Context);

// 4a. 局部空间求值：输出 FCompactPose（local space）
virtual void Evaluate_AnyThread(FPoseContext& Output);

// 4b. 组件空间求值：输出 FCSPose（component space）
virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output);
```

**四阶段调用链（从 UAnimInstance 到叶子节点）**：

```
UAnimInstance::NativeThreadSafeUpdateAnimation()
    └── FAnimInstanceProxy::UpdateAnimationNode()
            └── RootNode->Update_AnyThread(Context)
                    └── FPoseLink::Update() → 递归到子节点 Update_AnyThread()

UAnimInstance::EvaluateAnimation()
    └── FAnimInstanceProxy::Evaluate()
            └── RootNode->Evaluate_AnyThread(PoseContext)
                    └── FPoseLink::Evaluate() → 递归到子节点 Evaluate_AnyThread()
```

**上下文对象说明**：

- `FAnimationInitializeContext`：仅携带 `FAnimInstanceProxy*` 和 `SharedContext`。
- `FAnimationCacheBonesContext`：携带 `RequiredBones` 信息，用于 `FBoneReference::Initialize()`。
- `FAnimationUpdateContext`：携带 `DeltaTime`、`CurrentWeight`、`RootMotionWeightModifier`，节点通过 `FractionalWeight()` 传递分支权重。
- `FPoseContext` / `FComponentSpacePoseContext`：携带输出 pose（`FCompactPose`）、curve（`FBlendedCurve`）、custom attributes。

#### 3.2 BlendSpace 评估流程

BlendSpace 的核心逻辑分布在 `FAnimNode_BlendSpacePlayerBase` 的 `UpdateAssetPlayer` 和 `Evaluate_AnyThread` 中。

**UpdateAssetPlayer 阶段**：

1. 获取当前 BlendSpace 资产和采样坐标（`GetBlendSpace()`、`GetPosition()`）。
2. 调用 `UpdateInternal()`：
   - 使用 `BlendFilter` 对坐标变化进行阻尼平滑。
   - 调用 `UBlendSpace::UpdateBlendSamples()` 计算当前坐标所在三角形的顶点样本及插值权重，结果写入 `BlendSampleDataCache`。
   - 更新 `CachedTriangulationIndex` 以加速下次查找。
   - 推进内部时间累加器（`InternalTimeAccumulator`），处理循环、镜像、根运动提取。
   - 创建 `FAnimTickRecord` 并注册到 `FAnimInstanceProxy` 的 Sync Group 中（如有同步组配置）。

**Evaluate_AnyThread 阶段**：

1. 从 `BlendSampleDataCache` 读取最高权重的样本（`GetHighestWeightedSample()`），用于调试和根运动。
2. 调用 `UBlendSpace::GetAnimationPose()`：
   - 根据 `BlendSampleDataCache` 中各样本的权重，分别对每个样本动画执行 `GetAnimationPose()`。
   - 使用 `FAnimationPoseData` 将多个样本 pose 按权重混合为最终 pose。
3. 输出混合后的 `FCompactPose`、`FBlendedCurve` 和 Custom Attributes 到 `FPoseContext`。

**关键源码摘录**：

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/AnimNodes/AnimNode_BlendSpacePlayer.h`（第 42-46 行）

```cpp
// FAnimNode_Base interface
ANIMGRAPHRUNTIME_API virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
ANIMGRAPHRUNTIME_API virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
ANIMGRAPHRUNTIME_API virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
ANIMGRAPHRUNTIME_API virtual void Evaluate_AnyThread(FPoseContext& Output) override;
ANIMGRAPHRUNTIME_API virtual void GatherDebugData(FNodeDebugData& DebugData) override;
```

#### 3.3 SkeletalControl 评估链

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_SkeletalControlBase.h`（第 82-126 行）

骨骼控制器的执行流程被基类严格封装：

```cpp
// FAnimNode_Base interface（均为 final，子类不可覆写）
virtual void Update_AnyThread(const FAnimationUpdateContext& Context) final;
virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output) final;
```

**Update_AnyThread 内部流程**：

1. 调用 `UpdateComponentPose_AnyThread()`：递归更新输入链接 `ComponentPose`。
2. 调用 `UpdateInternal()`：子类可覆写，用于在更新阶段准备数据。
3. 根据 `AlphaInputType` 计算最终 `ActualAlpha`（支持 Float / Bool / Curve 三种来源）。
4. LOD 检查：如果当前 LOD > `LODThreshold`，将 `ActualAlpha` 置 0。

**EvaluateComponentSpace_AnyThread 内部流程**：

1. 调用 `EvaluateComponentPose_AnyThread()`：递归求值输入链接，得到基础 component-space pose。
2. 检查 `IsValidToEvaluate()` 和 `ActualAlpha` 是否大于 0。
3. 调用子类实现的 `EvaluateSkeletalControl_AnyThread(Output, BoneTransforms)`：
   - 子类在此计算需要修改的骨骼变换，填充 `BoneTransforms` 数组。
4. 基类将 `BoneTransforms` 按 `ActualAlpha` 混合回 `Output.Pose`：
   - 使用 `FBoneTransform::BlendWith()` 或插值方式，保证原有 pose 与 skeletal control 结果平滑过渡。

**关键源码摘录**：

文件：`Engine/Source/Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_SkeletalControlBase.h`（第 119-126 行）

```cpp
// use this function to evaluate for skeletal control base
ANIMGRAPHRUNTIME_API virtual void EvaluateComponentSpaceInternal(FComponentSpacePoseContext& Context);
// Evaluate the new component-space transforms for the affected bones.
ANIMGRAPHRUNTIME_API virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms);
// return true if it is valid to Evaluate
virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) { return false; }
// initialize any bone references you have
virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones){};
```

#### 3.4 关键调用链总结

| 阶段 | 入口 | 核心逻辑 | 输出 |
|------|------|----------|------|
| **Initialize** | `UAnimInstance::InitializeAnimation()` | `FAnimInstanceProxy::Initialize()` → `RootNode->Initialize_AnyThread()` → 递归到所有子节点 | 节点状态重置、绑定 exposed inputs |
| **CacheBones** | `USkeletalMeshComponent::RefreshBoneTransforms()` | `FAnimInstanceProxy::CacheBones()` → `RootNode->CacheBones_AnyThread()` → 递归 | `FBoneReference` 解析为骨骼索引 |
| **Update** | `UAnimInstance::NativeThreadSafeUpdateAnimation()` | `FAnimInstanceProxy::UpdateAnimationNode()` → `RootNode->Update_AnyThread()` → 递归 | 计算 blend weight、推进时间、Sync Group 对齐 |
| **Evaluate** | `UAnimInstance::EvaluateAnimation()` | `FAnimInstanceProxy::Evaluate()` → `RootNode->Evaluate_AnyThread()` → 递归 | 输出 `FCompactPose` + `FBlendedCurve` |

**BlendSpace 专用调用链**：

```
UAnimInstance::EvaluateAnimation()
    └── FAnimInstanceProxy::Evaluate()
            └── FAnimNode_BlendSpacePlayer::Evaluate_AnyThread(FPoseContext&)
                    └── UBlendSpace::GetAnimationPose()
                            ├── 对每个样本：UAnimSequence::GetAnimationPose()
                            └── FAnimationPoseData::BlendPoses()
```

**SkeletalControl 专用调用链**：

```
UAnimInstance::EvaluateAnimation()
    └── FAnimInstanceProxy::Evaluate()
            └── FAnimNode_SkeletalControlBase::EvaluateComponentSpace_AnyThread()
                    ├── EvaluateComponentPose_AnyThread()  // 递归求输入 pose
                    ├── EvaluateSkeletalControl_AnyThread() // 子类实现（如 TwoBoneIK/FABRIK）
                    └── 按 ActualAlpha blend BoneTransforms 回 Output.Pose
```

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
