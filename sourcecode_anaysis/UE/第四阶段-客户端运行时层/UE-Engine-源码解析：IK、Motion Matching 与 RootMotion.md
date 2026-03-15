---
title: "UE Engine 模块：IK、Motion Matching 与 RootMotion"
date: 2026-04-17
tags: [ue-source, engine-architecture, animation, ik, root-motion, motion-matching]
aliases: ["UE Engine IK RootMotion MotionMatching"]
---

> [← 返回 [00-UE全解析主索引|UE全解析主索引]]

## Why：为什么要深入理解 IK、Motion Matching 与 RootMotion？

- **角色动画与物理移动的解耦**：RootMotion 让动画师直接通过骨骼根骨头的轨迹驱动角色位移，而无需在代码中硬编码速度曲线，实现"动画即真理"。
- **复杂地形与交互的适配**：IK（反向动力学）让脚部自适应斜坡、手部抓取武器或攀爬，弥补前向动画（FK）在环境交互上的不足。
- **下一代动画选择范式**：Motion Matching 通过实时搜索姿态数据库替代状态机，减少手动混合逻辑的维护成本，是现代开放世界游戏动画系统的核心方向。

## What：这三大子系统是什么？

| 子系统 | 核心职责 | 关键资产/节点 |
|--------|----------|---------------|
| **IK** | 根据末端执行器（End Effector）目标位置，反向求解关节链旋转 | `FAnimNode_TwoBoneIK`、`FAnimNode_Fabrik`、`FAnimNode_CCDIK` 等 |
| **RootMotion** | 从动画中提取根骨头的位移/旋转，并应用到角色 Capsule 上 | `UAnimSequence::bEnableRootMotion`、`FRootMotionMovementParams` |
| **Motion Matching** | 基于当前角色状态与动画数据库做最近邻搜索，选择最佳下一帧动画 | `PoseSearch` 插件（UE5 中对应 `MotionMatching` 前身） |

## How：源码级三层剥离分析

---

### 1. 接口层：类分布与模块边界

#### 1.1 IK 节点分布

> **重要发现**：`Engine/Classes/Animation` 目录下**没有专门的 IK 类**。实际 IK 节点位于 **AnimGraphRuntime** 模块，底层算法位于 **AnimationCore** 模块。

| 节点/算法 | 所在头文件 | 继承关系 |
|-----------|-----------|----------|
| `FAnimNode_TwoBoneIK` | `Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_TwoBoneIK.h` | `FAnimNode_SkeletalControlBase` |
| `FAnimNode_Fabrik` | `Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_Fabrik.h` | `FAnimNode_SkeletalControlBase` |
| `FAnimNode_CCDIK` | `Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_CCDIK.h` | `FAnimNode_SkeletalControlBase` |
| `FAnimNode_LookAt` | `Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_LookAt.h` | `FAnimNode_SkeletalControlBase` |
| `FAnimNode_LegIK` | `Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_LegIK.h` | `FAnimNode_SkeletalControlBase` |
| `SolveTwoBoneIK` / `SolveFabrik` / `SolveCCDIK` | `AnimationCore` 模块 | 纯数学求解器 |

所有 IK 节点均通过重写 `EvaluateSkeletalControl_AnyThread(...)` 参与动画线程的组件空间（Component Space）求值（详见下文逻辑层）。

#### 1.2 RootMotion 相关类分布

| 类/结构 | 所在文件 | 作用 |
|---------|---------|------|
| `UAnimSequenceBase` | `Engine/Classes/Animation/AnimSequenceBase.h` | 定义 RootMotion 虚函数接口（`HasRootMotion`、`ExtractRootMotion`、`ExtractRootMotionFromRange`） |
| `UAnimSequence` | `Engine/Classes/Animation/AnimSequence.h` | 实现具体的 RootMotion 提取逻辑，字段 `bEnableRootMotion` |
| `UAnimMontage` | `Engine/Classes/Animation/AnimMontage.h` | 支持按 Slot 播放、Section 跳转，通过 `ExtractRootMotionFromTrackRange` 提取轨道级 RootMotion |
| `UAnimCompositeBase` | `Engine/Classes/Animation/AnimCompositeBase.h` | 复合动画基类，定义 `ExtractRootMotionFromTrack` |
| `FRootMotionMovementParams` | `Engine/Classes/Animation/AnimationAsset.h` | RootMotion 的累积与消费容器 |
| `UAnimNotifyState_DisableRootMotion` | `Engine/Classes/Animation/AnimNotifies/AnimNotifyState_DisableRootMotion.h` | Notify 状态，临时禁用 Montage 的 RootMotion |

#### 1.3 Motion Matching 插件位置

- **不在 Engine 核心模块**中，而是以独立插件形式存在。
- UE 5.x 中对应插件路径为 `Engine/Plugins/Animation/PoseSearch/PoseSearch.uplugin`（Description 中明确说明 "Used in techniques such as Motion Matching"）。
- 运行时模块为 `PoseSearch`（`Runtime` 类型），编辑器模块为 `PoseSearchEditor`。
- 核心节点为 `AnimGraphNode_MotionMatching`（位于 `PoseSearchEditor/Private/AnimGraphNode_MotionMatching.cpp`）。
- **基本原理**：在动画数据库（Pose Search Database）中预计算每帧的姿态特征（Trajectory、Bone Velocity、Height 等），运行时根据当前角色状态做最近邻搜索（Nearest Neighbor Search），返回最佳匹配的动画帧索引，实现基于搜索的动画选择。

---

### 2. 数据层：核心数据结构解析

#### 2.1 RootMotion 数据结构

**`FRootMotionMovementParams`**（`Engine/Classes/Animation/AnimationAsset.h` **L709-L846**）

```cpp
USTRUCT()
struct FRootMotionMovementParams
{
    UPROPERTY() bool bHasRootMotion;
    UPROPERTY() float BlendWeight;
    UPROPERTY() FTransform RootMotionTransform;

    void Accumulate(const FTransform& InTransform);
    void AccumulateWithBlend(const FTransform& InTransform, float InBlendWeight);
    FRootMotionMovementParams ConsumeRootMotion(float Alpha);
    // ...
};
```

- `Accumulate`：将新的 RootMotion 变换按乘法顺序累积到 `RootMotionTransform`。
- `AccumulateWithBlend`：支持按权重做最短路径旋转混合（`AccumulateWithShortestRotation`）。
- `ConsumeRootMotion(Alpha)`：按 Alpha 比例"消费"掉一部分 RootMotion，返回被消费的变换，剩余部分保留在容器中。

**`FRootMotionExtractionStep`**（`Engine/Classes/Animation/AnimCompositeBase.h` **L34-L63**）

```cpp
USTRUCT()
struct FRootMotionExtractionStep
{
    UPROPERTY() TObjectPtr<UAnimSequence> AnimSequence;
    UPROPERTY() float StartPosition;
    UPROPERTY() float EndPosition;
};
```

- 用于处理复合动画（Montage/Composite）中的 RootMotion 分段提取。当遇到循环、多段动画拼接时，将范围拆分为多个连续的 `FRootMotionExtractionStep`，逐段提取后顺序累积。

**枚举定义**（`Engine/Classes/Animation/AnimEnums.h` **L9-L43**）

```cpp
namespace ERootMotionRootLock
{
    enum Type : int
    {
        RefPose,        // 使用参考姿势根骨位置
        AnimFirstFrame, // 使用动画第一帧根骨位置
        Zero            // FTransform::Identity
    };
}

namespace ERootMotionMode
{
    enum Type : int
    {
        NoRootMotionExtraction,      // 不提取
        IgnoreRootMotion,            // 提取但不应用
        RootMotionFromEverything,    // 从所有动画提取（不适合联机）
        RootMotionFromMontagesOnly   // 仅从 Montage 提取（适合联机）
    };
}
```

#### 2.2 AnimSequence 中的 RootMotion 开关

**`UAnimSequence`**（`Engine/Classes/Animation/AnimSequence.h` **L318-L336**）

```cpp
UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = RootMotion)
bool bEnableRootMotion;

UPROPERTY(EditAnywhere, Category = RootMotion)
TEnumAsByte<ERootMotionRootLock::Type> RootMotionRootLock;

UPROPERTY(EditAnywhere, Category = RootMotion)
bool bForceRootLock;

UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = RootMotion)
bool bUseNormalizedRootMotionScale;
```

- `bEnableRootMotion`：总开关，决定该序列是否参与 RootMotion 提取。
- `RootMotionRootLock`：提取时如何锁定根骨位置。
- `bUseNormalizedRootMotionScale`：提取时将 Scale 归一化为 `(1,1,1)`。

#### 2.3 Montage 的 Slot 机制

**`FSlotAnimationTrack`**（`Engine/Classes/Animation/AnimMontage.h` **L82-L94**）

```cpp
USTRUCT()
struct FSlotAnimationTrack
{
    UPROPERTY(EditAnywhere, Category=Slot) FName SlotName;
    UPROPERTY(EditAnywhere, Category=Slot) FAnimTrack AnimTrack;
};
```

- Montage 通过 `SlotAnimTracks` 数组存储多个 Slot 轨道。
- 每个 `FAnimTrack` 由 `FAnimSegment` 数组组成，支持多段动画拼接、循环、播放速率调整。
- **RootMotion 目前仅来自第一个 Slot 轨道**（源码中多处硬编码 `SlotAnimTracks[0]`）。

---

### 3. 逻辑层：执行链与算法流程

#### 3.1 RootMotion 提取流程

##### 3.1.1 AnimSequence 层提取

**`UAnimSequence::ExtractRootMotion`**（`Engine/Private/Animation/AnimSequence.cpp` **L1543-L1583**）

```cpp
FTransform UAnimSequence::ExtractRootMotion(const FAnimExtractContext& ExtractionContext) const
{
    FRootMotionMovementParams RootMotionParams;
    if (ExtractionContext.DeltaTimeRecord.Delta != 0.f)
    {
        const bool bPlayingBackwards = (ExtractionContext.DeltaTimeRecord.Delta < 0.f);
        float PreviousPosition = ExtractionContext.CurrentTime;
        float CurrentPosition = ExtractionContext.CurrentTime;
        float DesiredDeltaMove = ExtractionContext.DeltaTimeRecord.Delta;
        do
        {
            const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, GetPlayLength());
            RootMotionParams.Accumulate(ExtractRootMotionFromRange(PreviousPosition, CurrentPosition, ExtractionContext));
            if ((AdvanceType == ETAA_Finished) && ExtractionContext.bLooping)
            {
                const double ActualDeltaMove = (CurrentPosition - PreviousPosition);
                DesiredDeltaMove -= ActualDeltaMove;
                PreviousPosition = bPlayingBackwards ? GetPlayLength() : 0.f;
                CurrentPosition = PreviousPosition;
            }
            else { break; }
        } while (true);
    }
    return RootMotionParams.GetRootMotionTransform();
}
```

- 处理带循环的动画：通过 `AdvanceTime` 将时间范围拆分为多个不跨越边界的连续段，逐段调用 `ExtractRootMotionFromRange` 并累积。

**`UAnimSequence::ExtractRootMotionFromRange`**（`Engine/Private/Animation/AnimSequence.cpp` **L1585-L1633**）

```cpp
FTransform UAnimSequence::ExtractRootMotionFromRange(double StartTime, double EndTime, const FAnimExtractContext& ExtractionContext) const
{
    // 1. 获取 Skeleton 的 RefPose 根骨变换
    FTransform RootTransformRefPose = FTransform::Identity;
    if (const USkeleton* MySkeleton = GetSkeleton())
    {
        RootTransformRefPose = MySkeleton->GetReferenceSkeleton().GetRefBonePose()[0];
    }

    // 2. 分别采样起点和终点的根轨道变换
    FAnimExtractContext Context = ExtractionContext;
    Context.CurrentTime = StartTime;
    FTransform StartTransform = ExtractRootTrackTransform_Lockless(Context, nullptr);
    Context.CurrentTime = EndTime;
    FTransform EndTransform = ExtractRootTrackTransform_Lockless(Context, nullptr);

    // 3. 处理 Scale 归一化或 Additive 动画的 Scale 偏移
    if (bUseNormalizedRootMotionScale)
    {
        StartTransform.SetScale3D(FVector(1.f));
        EndTransform.SetScale3D(FVector(1.f));
    }
    else if (IsValidAdditive())
    {
        StartTransform.SetScale3D(StartTransform.GetScale3D() + FVector(1.f));
        EndTransform.SetScale3D(EndTransform.GetScale3D() + FVector(1.f));
    }

    // 4. 转换到 Component Space：End * Start^{-1}
    const FTransform RootToComponent = RootTransformRefPose.Inverse();
    StartTransform = RootToComponent * StartTransform;
    EndTransform = RootToComponent * EndTransform;
    return EndTransform.GetRelativeTransform(StartTransform);
}
```

- 核心逻辑：**DeltaTransform = EndTransform * StartTransform^{-1}**，即在 RefPose 空间下计算根骨头从起点到终点的相对变换。

##### 3.1.2 Montage 层提取

**`UAnimMontage::ExtractRootMotionFromTrackRange`**（`Engine/Private/Animation/AnimMontage.cpp` **L940-L962**）

```cpp
FTransform UAnimMontage::ExtractRootMotionFromTrackRange(float StartTrackPosition, float EndTrackPosition, const FAnimExtractContext& Context) const
{
    FRootMotionMovementParams RootMotion;
    if (SlotAnimTracks.Num() > 0)
    {
        const FAnimTrack& SlotAnimTrack = SlotAnimTracks[0].AnimTrack;
        ExtractRootMotionFromTrack(SlotAnimTrack, StartTrackPosition, EndTrackPosition, Context, RootMotion);
    }
    return RootMotion.GetRootMotionTransform();
}
```

**`UAnimCompositeBase::ExtractRootMotionFromTrack`**（`Engine/Private/Animation/AnimCompositeBase.cpp` **L710-L733**）

```cpp
void UAnimCompositeBase::ExtractRootMotionFromTrack(const FAnimTrack& SlotAnimTrack, float StartTrackPosition, float EndTrackPosition, const FAnimExtractContext& Context, FRootMotionMovementParams& RootMotion) const
{
    TArray<FRootMotionExtractionStep> RootMotionExtractionSteps;
    SlotAnimTrack.GetRootMotionExtractionStepsForTrackRange(RootMotionExtractionSteps, StartTrackPosition, EndTrackPosition);

    for (int32 StepIndex = 0; StepIndex < RootMotionExtractionSteps.Num(); StepIndex++)
    {
        const FRootMotionExtractionStep& CurrentStep = RootMotionExtractionSteps[StepIndex];
        if (CurrentStep.AnimSequence->bEnableRootMotion)
        {
            FTransform DeltaTransform = CurrentStep.AnimSequence->ExtractRootMotionFromRange(
                CurrentStep.StartPosition, CurrentStep.EndPosition, Context);
            RootMotion.Accumulate(DeltaTransform);
        }
    }
}
```

- Montage 的 RootMotion 提取分为两步：
  1. `FAnimTrack::GetRootMotionExtractionStepsForTrackRange` 将轨道时间范围拆分为多个 `FRootMotionExtractionStep`（处理循环、多段动画、反向播放）。
  2. 对每个 Step 调用底层 `UAnimSequence::ExtractRootMotionFromRange`，再顺序累积。

#### 3.2 RootMotion 消费流程

##### 3.2.1 动画实例层累积

**`UAnimInstance::UpdateAnimation` 后处理**（`Engine/Private/Animation/AnimInstance.cpp` **L737-L758**）

```cpp
if (Proxy.GetExtractedRootMotion().bHasRootMotion)
{
    FTransform ProxyTransform = Proxy.GetExtractedRootMotion().GetRootMotionTransform();
    ProxyTransform.NormalizeRotation();
    ExtractedRootMotion.Accumulate(ProxyTransform);
    Proxy.GetExtractedRootMotion().Clear();
}

// 混合 Montage 产生的延迟 RootMotion
for (const FQueuedRootMotionBlend& RootMotionBlend : RootMotionBlendQueue)
{
    const float RootMotionSlotWeight = GetSlotNodeGlobalWeight(RootMotionBlend.SlotName);
    const float RootMotionInstanceWeight = RootMotionBlend.Weight * RootMotionSlotWeight;
    ExtractedRootMotion.AccumulateWithBlend(RootMotionBlend.Transform, RootMotionInstanceWeight);
}

if (ExtractedRootMotion.bHasRootMotion)
{
    ExtractedRootMotion.MakeUpToFullWeight();
}
```

- 动画线程（Proxy）提取的 RootMotion 被转移到 Game 线程的 `ExtractedRootMotion` 容器中。
- Montage 的 RootMotion 可能延迟混合（`QueueRootMotionBlend`），在此按 Slot 权重做加权累积。
- `MakeUpToFullWeight`：如果总权重不足 1，用 Identity 补齐并归一化旋转。

##### 3.2.2 Montage 实例中的 RootMotion 提取

**`FAnimMontageInstance::Advance`**（`Engine/Private/Animation/AnimMontage.cpp` **L2593-L2614**）

```cpp
if (bExtractRootMotion && AnimInstance.IsValid() && !IsRootMotionDisabled())
{
    const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(PreviousSubStepPosition, Position, FAnimExtractContext());
    if (bBlendRootMotion)
    {
        const float Weight = Blend.GetBlendedValue();
        AnimInstance.Get()->QueueRootMotionBlend(RootMotion, Montage->SlotAnimTracks[0].SlotName, Weight);
    }
    else
    {
        OutRootMotionParams->Accumulate(RootMotion);
    }
}
```

- `IsRootMotionDisabled()` 可被 `UAnimNotifyState_DisableRootMotion` 动态切换，实现 Montage 中某段时间禁用 RootMotion。

##### 3.2.3 SkeletalMeshComponent 消费

**`USkeletalMeshComponent::ConsumeRootMotion_Internal`**（`Engine/Private/Components/SkeletalMeshComponent.cpp` **L4269-L4288**）

```cpp
FRootMotionMovementParams USkeletalMeshComponent::ConsumeRootMotion_Internal(float InAlpha)
{
    FRootMotionMovementParams RootMotion;
    if (AnimScriptInstance)
    {
        RootMotion.Accumulate(AnimScriptInstance->ConsumeExtractedRootMotion(InAlpha));
        for (UAnimInstance* LinkedInstance : LinkedInstances)
        {
            RootMotion.Accumulate(LinkedInstance->ConsumeExtractedRootMotion(InAlpha));
        }
    }
    if (PostProcessAnimInstance)
    {
        RootMotion.Accumulate(PostProcessAnimInstance->ConsumeExtractedRootMotion(InAlpha));
    }
    return RootMotion;
}
```

- `CharacterMovementComponent` 每帧调用 `ConsumeRootMotion()`，将动画提取的 RootMotion 应用到角色移动上。

#### 3.3 IK 节点在动画线程中的执行链

所有 IK 节点继承自 `FAnimNode_SkeletalControlBase`（`Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_SkeletalControlBase.h`）。

**执行入口**（`AnimNode_SkeletalControlBase.h` **L82-L88**）

```cpp
ANIMGRAPHRUNTIME_API virtual void Update_AnyThread(const FAnimationUpdateContext& Context) final;
ANIMGRAPHRUNTIME_API virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output) final;
```

- `Update_AnyThread`：更新节点状态、Alpha 混合值。
- `EvaluateComponentSpace_AnyThread`：最终调用 `EvaluateSkeletalControl_AnyThread` 执行 IK 求解。

**以 `FAnimNode_TwoBoneIK` 为例**（`AnimNode_TwoBoneIK.h` **L109-L111**）

```cpp
ANIMGRAPHRUNTIME_API virtual void EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
```

- 输入为组件空间姿势（`FCSPose<FCompactPose>`）。
- 输出为需要修改的骨骼变换数组 `OutBoneTransforms`。
- 节点内部调用 `AnimationCore` 的 `SolveTwoBoneIK` 等算法，计算新的关节旋转后写入 `OutBoneTransforms`。

**`FAnimNode_LegIK` 的特殊性**（`AnimNode_LegIK.h` **L221-L229**）

```cpp
ANIMGRAPHRUNTIME_API virtual void EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
ANIMGRAPHRUNTIME_API bool OrientLegTowardsIK(FAnimLegIKData& InLegData);
ANIMGRAPHRUNTIME_API bool DoLegReachIK(FAnimLegIKData& InLegData);
ANIMGRAPHRUNTIME_API bool AdjustKneeTwist(FAnimLegIKData& InLegData);
```

- `LegIK` 内部维护 `FIKChain`，支持两种求解模式：
  - 两骨链调用 `SolveTwoBoneIK`
  - 多骨链调用 `SolveFABRIK`
- 还支持 Knee Twist 修正（通过对比 FK 与 IK 足部的朝向差异）。

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
