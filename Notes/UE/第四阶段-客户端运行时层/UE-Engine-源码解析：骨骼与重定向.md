---
title: UE-Engine-源码解析：骨骼与重定向
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - animation
  - skeleton
  - retargeting
aliases:
  - UE Engine Skeleton & Retargeting
---

> [← 返回 [[00-UE全解析主索引|UE全解析主索引]]]

---

## Why：为什么要深入骨骼与重定向？

在大型商业化项目中，动画资产往往跨角色复用：一个标准的待机动画可能需要同时适配 1.8m 的人类角色与 2.5m 的怪物角色。若不做任何处理，直接播放会导致肢体穿插、滑步、重心偏移等严重问题。**骨骼重定向（Retargeting）** 正是解决这一问题的核心机制。

理解 UE 的骨骼与重定向源码，能够帮助我们：
- **排查运行时动画错位**：根骨偏移、肢体长度不匹配、镜像错误。
- **优化动画性能**：理解 `FBoneContainer` 的 Compact Pose、LOD 剔除、RequiredBones 机制。
- **设计自研引擎的动画管线**：UE 的 Skeleton-centric 架构（以 Skeleton 为资产中心，而非 Mesh）是一种非常成熟的设计范式，值得借鉴。

---

## What：骨骼与重定向是什么？

### 核心概念一览

| 概念 | 说明 |
|------|------|
| **USkeleton** | 动画骨骼资产，定义骨骼层级、Reference Pose、Virtual Bones、Sockets、Retarget Sources 等。 |
| **FReferenceSkeleton** | USkeleton 内部的引用骨骼结构，保存 Raw/Final 骨骼信息及变换。 |
| **FBoneContainer** | 运行时骨骼容器，维护 Required Bones、Compact Pose 索引映射、曲线过滤、Retarget 缓存。 |
| **UAnimInstance** | 动画蓝图运行时实例，挂载于 `SkeletalMeshComponent`，驱动动画图评估。 |
| **AnimRetargetSources** | 存储在 USkeleton 中的重定向源姿势（T-Pose/A-Pose），用于比例对齐。 |
| **UMirrorDataTable** | 镜像数据表，定义骨骼/曲线/Notify 的左右名称映射规则，供运行时镜像。 |

---

## How：源码级拆解

以下按 **接口层 → 数据层 → 逻辑层** 三层剥离法展开。

---

### 1. 接口层：Skeleton 资产体系与 AnimInstance

#### 1.1 USkeleton：动画骨骼资产核心

`USkeleton` 是连接 `USkeletalMesh` 与 `UAnimationAsset` 的桥梁。它存储了骨骼层级、Reference Pose、Virtual Bones、Sockets、Retarget Sources 等关键数据。

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h:293-355
UCLASS(hidecategories=Object, MinimalAPI, BlueprintType)
class USkeleton : public UObject, public IInterface_AssetUserData, public IInterface_PreviewMeshProvider
{
    // ...
protected:
    /** Skeleton bone tree - each contains name and parent index **/
    UPROPERTY(VisibleAnywhere, Category=Skeleton)
    TArray<struct FBoneNode> BoneTree;

    /** Reference Skeleton */
    FReferenceSkeleton ReferenceSkeleton;

    /** Guid for skeleton */
    FGuid Guid;

    /** Guid for virtual bones */
    UPROPERTY()
    FGuid VirtualBoneGuid;

    /** Array of this skeletons virtual bones */
    UPROPERTY()
    TArray<FVirtualBone> VirtualBones;

    /** The list of compatible skeletons */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = CompatibleSkeletons)
    TArray<TSoftObjectPtr<USkeleton>> CompatibleSkeletons;

    /** Should we use the per bone translational retarget mode from the source skeleton */
    UPROPERTY(EditAnywhere, Category = CompatibleSkeletons)
    bool bUseRetargetModesFromCompatibleSkeleton = false;

public:
    UPROPERTY()
    TArray<TObjectPtr<class USkeletalMeshSocket>> Sockets;

    /** Serializable retarget sources for this skeleton **/
    TMap< FName, FReferencePose > AnimRetargetSources;
    // ...
};
```

关键设计洞察：
- `BoneTree` 只保存骨骼名称与父索引（以及重定向模式），真正的变换数据在 `ReferenceSkeleton` 中。
- `CompatibleSkeletons` 允许一个 Skeleton 使用其他 Skeleton 制作的动画资产（通过 `TSoftObjectPtr` 按需加载）。
- `AnimRetargetSources` 以 `FName → FReferencePose` 映射保存多套重定向源姿势（如 T-Pose、A-Pose），供不同体型角色使用。

#### 1.2 FReferenceSkeleton：引用骨骼的内存表示

**源码位置**：`Engine/Source/Runtime/Engine/Public/ReferenceSkeleton.h`

```cpp
// Engine/Source/Runtime/Engine/Public/ReferenceSkeleton.h:99-133
struct FReferenceSkeleton
{
    FReferenceSkeleton(bool bInOnlyOneRootAllowed = true)
        :bOnlyOneRootAllowed(bInOnlyOneRootAllowed)
    {}

private:
    // RAW BONES: Bones that exist in the original asset
    TArray<FMeshBoneInfo>    RawRefBoneInfo;
    TArray<FTransform>       RawRefBonePose;

    // FINAL BONES: Bones for this skeleton including user added virtual bones
    TArray<FMeshBoneInfo>    FinalRefBoneInfo;
    TArray<FTransform>       FinalRefBonePose;

    /** TMap to look up bone index from bone name. */
    TMap<FName, int32>       RawNameToIndexMap;
    TMap<FName, int32>       FinalNameToIndexMap;

    // cached data to allow virtual bones to be built into poses
    TArray<FBoneIndexType>   RequiredVirtualBones;
    TArray<FVirtualBoneRefData> UsedVirtualBoneData;
    // ...
};
```

`FReferenceSkeleton` 采用 **Raw / Final 双轨制**：
- `Raw*`：原始资产中的骨骼（如从 DCC 导入的骨骼）。
- `Final*`：包含用户添加的 `VirtualBones` 后的最终骨骼。

运行时评估统一使用 `FinalRefBoneInfo / FinalRefBonePose`，通过 `FindBoneIndex` 进行名称到索引的 O(1) 查找。

#### 1.3 FSmartName：稳定 UID 映射（历史遗留但仍在结构中）

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/SmartName.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/SmartName.h:210-224
USTRUCT()
struct FSmartName
{
    GENERATED_USTRUCT_BODY();

    UPROPERTY(VisibleAnywhere, Category=FSmartName)
    FName DisplayName;

    SmartName::UID_Type    UID;

    FSmartName()
        : DisplayName(NAME_None)
        , UID(SmartName::MaxUID)
    {}
    // ...
};
```

> ⚠️ 在 UE 5.3+ 中，`FSmartName` 及其 `UID` 机制已被标记为 **Deprecated**，曲线和姿势名称已逐步回退到直接使用 `FName`。但在阅读旧代码或 `UPoseAsset` 时仍会看到 `FSmartName` 的身影。

#### 1.4 UAnimInstance 与 SkeletalMeshComponent 的关系

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/AnimInstance.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/AnimInstance.h:351-363
UCLASS(transient, Blueprintable, hideCategories=AnimInstance, BlueprintType, Within=SkeletalMeshComponent, MinimalAPI)
class UAnimInstance : public UObject
{
    // ...
    /** This is used to extract animation. If Mesh exists, this will be overwritten by Mesh->Skeleton */
    UPROPERTY(transient)
    TObjectPtr<USkeleton> CurrentSkeleton;
    // ...
};
```

`UAnimInstance` 的生命周期由 `USkeletalMeshComponent` 托管（`Within=SkeletalMeshComponent`）。
- 初始化时，`SkeletalMeshComponent` 会根据 `SkeletalMesh` 的 `Skeleton` 覆盖 `CurrentSkeleton`。
- 动画评估时，`UAnimInstance` 通过 `FAnimInstanceProxy` 与组件解耦，支持多线程更新。

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h:89,1025
class USkeletalMeshComponent : public USkinnedMeshComponent
{
    // The anim instance we are evaluating
    UAnimInstance* AnimInstance;
    // ...
    UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
    ENGINE_API class UAnimInstance * GetAnimInstance() const;
};
```

---

### 2. 数据层：内存布局、强类型索引与 Pose 资产

#### 2.1 FBoneContainer：运行时的骨骼索引中枢

**源码位置**：`Engine/Source/Runtime/Engine/Public/BoneContainer.h`

```cpp
// Engine/Source/Runtime/Engine/Public/BoneContainer.h:191-248
struct FBoneContainer
{
private:
    /** Array of RequiredBonesIndices. In increasing order. */
    TArray<FBoneIndexType>    BoneIndicesArray;
    /** Array sized by Current RefPose. true if Bone is contained in RequiredBones array. */
    TBitArray<>               BoneSwitchArray;

    /** Asset BoneIndicesArray was made for. Typically a SkeletalMesh. */
    TWeakObjectPtr<UObject>   Asset;
    TWeakObjectPtr<USkeletalMesh> AssetSkeletalMesh;
    TWeakObjectPtr<USkeleton> AssetSkeleton;

    /** Pointer to RefSkeleton of Asset. */
    const FReferenceSkeleton* RefSkeleton;

    /** Mapping table between Skeleton Bone Indices and Pose Bone Indices. */
    TArray<int32> SkeletonToPoseBoneIndexArray;
    TArray<int32> PoseToSkeletonBoneIndexArray;

    // Look up from skeleton to compact pose format
    TArray<int32> CompactPoseToSkeletonIndex;
    TArray<FCompactPoseBoneIndex> SkeletonToCompactPose;
    TArray<FCompactPoseBoneIndex> CompactPoseParentBones;

    // Array of cached virtual bone data
    TArray<FVirtualBoneCompactPoseData> VirtualBoneCompactPoseData;
    // ...
};
```

`FBoneContainer` 的核心职责：
1. **LOD 骨骼裁剪**：通过 `BoneIndicesArray` 存储当前 LOD 所需的骨骼索引（RequiredBones），非必需骨骼不参与计算。
2. **索引空间映射**：维护 Skeleton Pose ↔ Mesh Pose ↔ Compact Pose 的三重映射。
3. **Retarget 缓存**：按需构建 `FRetargetSourceCachedData`，加速 OrientAndScale 重定向。

#### 2.2 强类型骨骼索引

UE 为了防止不同索引空间混用，引入了强类型包装：

**源码位置**：`Engine/Source/Runtime/Engine/Public/BoneContainer.h`

```cpp
// Engine/Source/Runtime/Engine/Public/BoneContainer.h:567-637
FMeshPoseBoneIndex GetMeshPoseIndexFromSkeletonPoseIndex(const FSkeletonPoseBoneIndex& SkeletonIndex) const;
FSkeletonPoseBoneIndex GetSkeletonPoseIndexFromMeshPoseIndex(const FMeshPoseBoneIndex& MeshIndex) const;
FSkeletonPoseBoneIndex GetSkeletonPoseIndexFromCompactPoseIndex(const FCompactPoseBoneIndex& BoneIndex) const;
FCompactPoseBoneIndex GetCompactPoseIndexFromSkeletonPoseIndex(const FSkeletonPoseBoneIndex& SkeletonIndex) const;
```

| 索引类型 | 含义 |
|----------|------|
| `FSkeletonPoseBoneIndex` | USkeleton / FReferenceSkeleton 空间中的索引 |
| `FMeshPoseBoneIndex` | USkeletalMesh 的 RefPose 空间中的索引 |
| `FCompactPoseBoneIndex` | 当前 LOD 裁剪后的紧凑索引（动画图实际使用的索引） |

这种强类型设计在编译期就能拦截大量因索引空间混淆导致的 Bug，是自研引擎非常值得借鉴的模式。

#### 2.3 UPoseAsset 与 RetargetSource

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/PoseAsset.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/PoseAsset.h:185-216
UCLASS(MinimalAPI, BlueprintType)
class UPoseAsset : public UAnimationAsset
{
    // ...
private:
    UPROPERTY()
    struct FPoseDataContainer PoseContainer;

    UPROPERTY(Category = Additive, EditAnywhere)
    bool bAdditivePose;

    UPROPERTY()
    int32 BasePoseIndex;

public:
    /** Base pose to use when retargeting */
    UPROPERTY(Category=Animation, EditAnywhere)
    FName RetargetSource;

    /** When using RetargetSourceAsset, use the post stored here */
    UPROPERTY()
    TArray<FTransform> RetargetSourceAssetReferencePose;
    // ...
};
```

`UPoseAsset` 存储一组可混合的姿势（如面部表情、手部姿势）。它同样可以指定 `RetargetSource`，在评估时从 `USkeleton::AnimRetargetSources` 中查找对应的基础姿势，以正确计算 Delta 或进行比例适配。

---

### 3. 逻辑层：评估流程与重定向机制

#### 3.1 骨骼变换评估流程：UAnimInstance → FAnimInstanceProxy → Pose

动画评估的主流程在 `USkeletalMeshComponent` 的 Tick 中被触发，核心路径如下：

**源码位置**：`Engine/Source/Runtime/Engine/Private/Animation/AnimInstance.cpp`

```cpp
// Engine/Source/Runtime/Engine/Private/Animation/AnimInstance.cpp:871-932
void UAnimInstance::PreEvaluateAnimation()
{
    GetProxyOnGameThread<FAnimInstanceProxy>().PreEvaluateAnimation(this);
}

void UAnimInstance::ParallelEvaluateAnimation(bool bForceRefPose, const USkeletalMesh* InSkeletalMesh, FParallelEvaluationData& OutEvaluationData)
{
    FAnimInstanceProxy& Proxy = GetProxyOnAnyThread<FAnimInstanceProxy>();
    OutEvaluationData.OutPose.SetBoneContainer(&Proxy.GetRequiredBones());

    if( !bForceRefPose )
    {
        FPoseContext EvaluationContext(&Proxy);
        EvaluationContext.ResetToRefPose();
            
        Proxy.EvaluateAnimation(EvaluationContext);

        OutEvaluationData.OutCurve.CopyFrom(EvaluationContext.Curve);
        OutEvaluationData.OutPose.CopyBonesFrom(EvaluationContext.Pose);
        OutEvaluationData.OutAttributes.CopyFrom(EvaluationContext.CustomAttributes);
    }
    else
    {
        OutEvaluationData.OutPose.ResetToRefPose();
    }
}

void UAnimInstance::PostEvaluateAnimation()
{
    LLM_SCOPE(ELLMTag::Animation);
    NativePostEvaluateAnimation();
    BlueprintPostEvaluateAnimation();
    GetProxyOnGameThread<FAnimInstanceProxy>().PostEvaluate(this);
}
```

流程解析：
1. **PreEvaluateAnimation**：在游戏线程准备评估上下文，复制必要数据到 Proxy。
2. **ParallelEvaluateAnimation**：在工作线程执行，创建 `FPoseContext`，调用 `Proxy.EvaluateAnimation(EvaluationContext)` 驱动动画图（AnimGraph）评估，输出 `FCompactPose`。
3. **PostEvaluateAnimation**：评估完成后，在游戏线程执行 `NativePostEvaluateAnimation` 与 `BlueprintPostEvaluateAnimation`，并更新曲线到材质/变形目标。

**源码位置**：`Engine/Source/Runtime/Engine/Public/Animation/AnimInstanceProxy.h`

```cpp
// Engine/Source/Runtime/Engine/Public/Animation/AnimInstanceProxy.h:684-694
ENGINE_API void EvaluateAnimation(FPoseContext& Output);
ENGINE_API void EvaluateAnimation_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode);
ENGINE_API void EvaluateAnimationNode(FPoseContext& Output);
ENGINE_API void EvaluateAnimationNode_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode);
```

`FAnimInstanceProxy::EvaluateAnimation` 会递归调用动画图中各 `FAnimNode_Base` 的 `Evaluate_AnyThread`，最终输出一个 `FCompactPose`。

#### 3.2 重定向时如何利用 AnimRetargetSources

重定向的核心问题：动画是在 **源 Skeleton** 的比例下制作的，而当前角色（目标 Skeleton/Mesh）的比例可能不同。UE 通过 `AnimRetargetSources` 与 `EBoneTranslationRetargetingMode` 解决这一问题。

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h:69-86
UENUM()
namespace EBoneTranslationRetargetingMode
{
    enum Type : int
    {
        Animation,         // 使用动画数据中的位移
        Skeleton,          // 使用 Skeleton 的固定位移
        AnimationScaled,   // 按 Skeleton 比例缩放动画位移
        AnimationRelative, // 将源与目标 Reference Pose 的差值作为 Additive
        OrientAndScale,    // 计算源到目标的方向旋转与缩放
    };
}
```

运行时，`FBoneContainer` 会根据 `RetargetSourceName` 懒加载并缓存 OrientAndScale 数据：

**源码位置**：`Engine/Source/Runtime/Engine/Private/BoneContainer.cpp`

```cpp
// Engine/Source/Runtime/Engine/Private/BoneContainer.cpp:404-504
const FRetargetSourceCachedData& FBoneContainer::GetRetargetSourceCachedData(
    const FName& InSourceName,
    const FSkeletonRemapping& InRemapping,
    const TArray<FTransform>& InRetargetTransforms) const
{
    // ... 构造 LUTKey 查找缓存 ...
    FRetargetSourceCachedData* RetargetSourceCachedData = RetargetSourceCachedDataLUT.Find(LUTKey);
    if (!RetargetSourceCachedData)
    {
        RetargetSourceCachedData = &RetargetSourceCachedDataLUT.Add(LUTKey);
        const TArray<FTransform>& AuthoredOnRefSkeleton = InRetargetTransforms;
        const TArray<FTransform>& PlayingOnRefSkeleton = GetRefPoseArray();

        for (int32 CompactBoneIndex = 0; CompactBoneIndex < CompactPoseNumBones; CompactBoneIndex++)
        {
            // ... 获取 Source/Target SkeletonBoneIndex ...
            const EBoneTranslationRetargetingMode::Type RetargetMode = FAnimationRuntime::GetBoneTranslationRetargetingMode(...);

            if (RetargetMode == EBoneTranslationRetargetingMode::OrientAndScale)
            {
                const FVector SourceSkelTrans = AuthoredOnRefSkeleton[SourceSkeletonBoneIndex].GetTranslation();
                const FVector TargetSkelTrans = PlayingOnRefSkeleton[BoneIndicesArray[CompactBoneIndex]].GetTranslation();

                const FQuat DeltaRotation = FQuat::FindBetweenNormals(SourceSkelTransDir, TargetSkelTransDir);
                const float Scale = TargetSkelTransLength / SourceSkelTransLength;
                const int32 OrientAndScaleIndex = RetargetSourceCachedData->OrientAndScaleData.Add(
                    FOrientAndScaleRetargetingCachedData(DeltaRotation, Scale, SourceSkelTrans, TargetSkelTrans));

                RetargetSourceCachedData->CompactPoseIndexToOrientAndScaleIndex[CompactBoneIndex] = OrientAndScaleIndex;
            }
        }
    }
    return *RetargetSourceCachedData;
}
```

重定向执行逻辑：
1. **查找 RetargetSource**：动画资产（如 `UAnimSequence`）可能指定了一个 `RetargetSource`（如 `"Male_TPose"`）。
2. **构建缓存**：`FBoneContainer` 在首次评估时，比较 `AuthoredOnRefSkeleton`（源）与 `PlayingOnRefSkeleton`（目标）的 Reference Pose，计算每根骨骼的 `DeltaRotation` 与 `Scale`，存入 `FRetargetSourceCachedData`。
3. **运行时应用**：在 `FAnimationRuntime::ConvertBoneTransform` 等函数中，根据骨骼的 `EBoneTranslationRetargetingMode` 选择不同的位移计算方式。对于 `OrientAndScale`，直接从缓存中取出旋转与缩放，应用到动画位移上。

> 设计亮点：`RetargetSourceCachedDataLUT` 采用 **Lazy + Per-Instance 缓存** 策略，避免在初始化时一次性计算所有重定向数据，也避免了多角色共享时的缓存冲突。

#### 3.3 UMirrorDataTable 镜像重定向原理

镜像动画是重定向的一个特殊分支。UE 通过 `UMirrorDataTable` 定义骨骼、曲线、Notify 的左右映射规则，并在动画图评估时动态替换。

**源码位置**：`Engine/Source/Runtime/Engine/Classes/Animation/MirrorDataTable.h`

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/MirrorDataTable.h:100-238
UCLASS(MinimalAPI, BlueprintType)
class UMirrorDataTable : public UDataTable
{
    // ...
public:
    UPROPERTY(EditAnywhere, Category = CreateTable)
    TArray<FMirrorFindReplaceExpression> MirrorFindReplaceExpressions;

    UPROPERTY(EditAnywhere, Category = Mirroring)
    TEnumAsByte<EAxis::Type> MirrorAxis;

    UPROPERTY(EditAnywhere, Category = Mirroring)
    bool  bMirrorRootMotion = true;
    
    UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = Skeleton)
    TObjectPtr<USkeleton> Skeleton; 

    // Index of the mirror bone for a given bone index in the reference skeleton
    TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> BoneToMirrorBoneIndex;

    TMap<FName, FName> CurveToMirrorCurveMap;
    TMap<FName, FName> AnimNotifyToMirrorAnimNotifyMap;
    TMap<FName, FName> SyncToMirrorSyncMap;
    // ...
};
```

`UMirrorDataTable` 的核心数据结构：
- `MirrorFindReplaceExpressions`：定义名称查找替换规则（Prefix/Suffix/RegularExpression），如 `l_` ↔ `r_`。
- `BoneToMirrorBoneIndex`：从 ReferenceSkeleton 索引到镜像骨骼索引的映射。
- `CurveToMirrorCurveMap`、`AnimNotifyToMirrorAnimNotifyMap`、`SyncToMirrorSyncMap`：曲线、Notify、同步标记的镜像映射。

**运行时应用**：

在 `FAnimInstanceProxy` 中，可以通过 `SetSyncMirror` 将镜像表同步到动画系统中；在具体的 AnimNode（如 `FAnimNode_Mirror`）中，会调用 `UMirrorDataTable::FillCompactPoseAndComponentRefRotations` 生成 CompactPose 级别的快速查找表：

```cpp
// Engine/Source/Runtime/Engine/Classes/Animation/MirrorDataTable.h:160-163
ENGINE_API void FillCompactPoseAndComponentRefRotations(
    const FBoneContainer& BoneContainer,
    TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex>& OutCompactPoseMirrorBones,
    TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex>& OutComponentSpaceRefRotations) const;
```

`FillCompactPoseAndComponentRefRotations` 的作用：
1. 将 `BoneToMirrorBoneIndex`（Skeleton Pose 空间）转换为 `OutCompactPoseMirrorBones`（Compact Pose 空间）。
2. 预计算每根骨骼在 Component Space 下的 Reference Rotation，供镜像时进行四元数翻折（通常绕镜像轴翻转）。

运行时镜像节点的评估逻辑（在 AnimNode 层）：
- 遍历 Compact Pose 中的每根骨骼。
- 通过 `OutCompactPoseMirrorBones` 找到对应的镜像骨骼索引。
- 交换两根骨骼的 Local Transform（或翻转旋转）。
- 对 Root Motion 和曲线执行同样的映射与符号翻转。

---

## 小结与可迁移原理

| UE 设计 | 可迁移到自研引擎的原理 |
|---------|------------------------|
| **Skeleton-centric 资产架构** | 将骨骼资产与 Mesh、Animation 解耦，实现跨角色动画复用。 |
| **FBoneContainer + 强类型索引** | 通过 `CompactPoseBoneIndex` / `SkeletonPoseBoneIndex` 等强类型包装，避免索引空间混用；通过 `RequiredBones` 实现 LOD 级裁剪。 |
| **Lazy Retarget Cache** | 重定向数据按 `(Skeleton, RetargetSource)` 懒加载缓存，降低初始化开销。 |
| **OrientAndScale 模式** | 对肢体长度差异大的角色，用方向+缩放重定向比简单比例缩放更自然。 |
| **MirrorDataTable** | 将镜像规则从硬编码节点抽离为数据资产，方便美术调试与扩展。 |

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
