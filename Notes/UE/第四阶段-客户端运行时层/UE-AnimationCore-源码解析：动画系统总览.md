---
title: UE-AnimationCore-源码解析：动画系统总览
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - animation
  - ik
aliases:
  - AnimationCore 模块
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

## Why：为什么要深入 AnimationCore？

AnimationCore 是 UE 动画系统最底层的运行时模块，位于 `Engine/Source/Runtime/AnimationCore`。它仅依赖 `Core` 与 `CoreUObject`，却承载了整个动画管道的**通用数学基础、数据结构定义与核心算法**（IK、约束、骨骼权重）。理解这一层，有助于我们：

1. **打通动画评估的数学本质**：IK 不再是黑箱，而是可追踪的向量运算。
2. **识别性能关键路径**：骨骼索引的强类型设计、FBoneWeights 的 uint16 量化与内联存储，都是为了压减动画评估的内存与缓存开销。
3. **为上层模块（AnimGraph、Control Rig）建立锚点**：上层所有动画节点最终都会调用 AnimationCore 中的工具函数与数据结构。

## What：AnimationCore 是什么？

### 模块定位

| 属性 | 内容 |
|------|------|
| 路径 | `Engine/Source/Runtime/AnimationCore` |
| 公共头文件 | 19 个 `.h`（Public/） |
| 实现文件 | 15 个 `.cpp`（Private/） |
| 模块依赖 | `Core`、`CoreUObject`（Public）；`Engine`（Private Include Paths） |

Build.cs 中的依赖关系极为精简，体现了该模块的"底座"定位：

```csharp
// AnimationCore.Build.cs (行 8-20)
PrivateIncludePathModuleNames.AddRange(new string[] { "Engine" });
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });
```

Public 头文件可粗分为五类：
- **数据结构**：`NodeHierarchy.h`、`NodeChain.h`、`BoneIndices.h`、`BoneWeights.h`
- **变换类型**：`EulerTransform.h`、`TransformNoScale.h`、`CommonAnimTypes.h`
- **约束/工具**：`AnimationCoreLibrary.h`、`Constraint.h`、`AnimationDataSource.h`
- **IK 算法**：`TwoBoneIK.h`、`FABRIK.h`、`CCDIK.h`、`SoftIK.h`、`SplineIK.h`、`AngularLimit.h`

---

## How：三层剥离法源码解析

### 1. 接口层：模块边界与核心类职责

#### 1.1 UAnimationDataSourceRegistry：动画数据源的 UObject 级注册表

```cpp
// Public/AnimationDataSource.h (行 19-86)
UCLASS(MinimalAPI)
class UAnimationDataSourceRegistry : public UObject
{
    GENERATED_BODY()
public:
    bool RegisterDataSource(const FName& InName, UObject* InDataSource);
    bool UnregisterDataSource(const FName& InName);
    bool ContainsSource(const FName& InName) const;
    UObject* RequestSource(const FName& InName, UClass* InExpectedClass) const;
    template<class T> T* RequestSource(const FName& InName) const;
private:
    UPROPERTY(transient)
    TMap<FName, TWeakObjectPtr<UObject>> DataSources;
    void ClearInvalidDataSource();
};
```

**设计要点**：
- 使用 `TWeakObjectPtr` 持有数据源，避免强引用导致的循环依赖或意外生命周期延长。
- `RequestSource<T>` 模板封装了 `Cast<T>`，让调用方在 C++ 层获得类型安全。
- `ClearInvalidDataSource` 会在查询时惰性清理已被 GC 的对象。

#### 1.2 核心接口类职责矩阵

| 类/结构 | 文件 | 职责 |
|---------|------|------|
| `UAnimationDataSourceRegistry` | `AnimationDataSource.h` | 按 `FName` 注册/查询动画数据源，解耦数据生产者与消费者 |
| `FNodeHierarchyData` | `NodeHierarchy.h` | 纯数据层级：节点定义 + Transform 数组 + Name→Index 映射 |
| `FNodeHierarchyWithUserData` | `NodeHierarchy.h` | 在 `FNodeHierarchyData` 之上增加**虚接口**，支持附加每节点用户数据 |
| `AnimationCore::SolveTwoBoneIK` | `TwoBoneIK.h` | 两骨骼 IK 求解（带/不带肢体长度参数的重载） |
| `AnimationCore::SolveFabrik` | `FABRIK.h` | FABRIK 多骨骼链 IK 求解 |
| `AnimationCore::SolveCCDIK` | `CCDIK.h` | CCDIK（循环坐标下降）IK 求解 |
| `AnimationCore::SolveConstraints` | `AnimationCoreLibrary.h` | 约束系统求解入口 |

---

### 2. 数据层：UObject 体系、层级结构与强类型索引

#### 2.1 FNodeHierarchyData：动画层级的最小闭环

```cpp
// Public/NodeHierarchy.h (行 37-195)
USTRUCT()
struct FNodeHierarchyData
{
    GENERATED_BODY()
    UPROPERTY() TArray<FNodeObject> Nodes;
    UPROPERTY() TArray<FTransform> Transforms;
    UPROPERTY() TMap<FName, int32> NodeNameToIndexMapping;

    int32 GetParentIndex(int32 Index) const
    {
        return GetNodeIndex(Nodes[Index].ParentName);
    }

    void SetTransform(int32 Index, const FTransform& NewTransform)
    {
        Transforms[Index] = NewTransform;
        Transforms[Index].NormalizeRotation();
    }

    int32 Add(const FName& InNodeName, const FName& InParentName, const FTransform& InTransform);
    void BuildNodeNameToIndexMapping();
};
```

**设计洞察**：
- `Nodes`、`Transforms`、`NodeNameToIndexMapping` 三者长度始终一致（`check` 保证）。
- `FNodeObject` 仅存储 `Name` 与 `ParentName`（`FName` 类型），不存储索引，意味着**层级关系通过名称间接引用**。这在序列化/反序列化时更稳定，但运行时查询需要 `BuildNodeNameToIndexMapping` 重建映射。

#### 2.2 FNodeHierarchyWithUserData：虚接口扩展机制

```cpp
// Public/NodeHierarchy.h (行 197-404)
USTRUCT()
struct FNodeHierarchyWithUserData
{
    GENERATED_BODY()
protected:
    UPROPERTY() FNodeHierarchyData Hierarchy;
public:
    virtual const FTransform& GetLocalTransform(int32 Index) const PURE_VIRTUAL(...);
    virtual const FTransform& GetGlobalTransform(int32 Index) const PURE_VIRTUAL(...);
    virtual void SetLocalTransform(int32 Index, const FTransform& NewTransform) { }
    virtual void SetGlobalTransform(int32 Index, const FTransform& NewTransform) { }

    template<typename DataType>
    const DataType& GetNodeData(int32 Index) const
    {
        return *reinterpret_cast<const DataType*>(GetUserDataImpl(Index));
    }
protected:
    virtual const void* GetUserDataImpl(int32 Index) const { return nullptr; }
    virtual int32 AddUserDataImpl(const void* InData) { return INDEX_NONE; }
    virtual bool HasUserData() const { return false; }
};
```

**为什么用虚函数 + `reinterpret_cast`？**
- 虚函数允许派生类在**不修改底层 TArray 布局**的前提下，自定义局部/全局 Transform 的计算逻辑（例如引入 LOD、缩放或约束后处理）。
- `GetNodeData<DataType>` 通过 `reinterpret_cast` 将用户数据指针强转为具体类型，这种设计在引擎底层非常常见（如渲染器的 Vertex Factory 模式），目的是**零开销泛化**：不引入模板参数污染 USTRUCT。

#### 2.3 骨骼索引的强类型设计

```cpp
// Public/BoneIndices.h (行 10-91)
struct FBoneIndexBase
{
    FBoneIndexBase() : BoneIndex(INDEX_NONE) {}
    inline int32 GetInt() const { return BoneIndex; }
    inline bool IsValid() const { return BoneIndex != INDEX_NONE; }
    inline explicit operator int32() const { return BoneIndex; }
protected:
    int32 BoneIndex;
};

struct FCompactPoseBoneIndex : public FBoneIndexBase
{
    explicit FCompactPoseBoneIndex(int32 InBoneIndex) { BoneIndex = InBoneIndex; }
    UE_BONE_INDEX_OPERATORS(FCompactPoseBoneIndex)
};

struct FMeshPoseBoneIndex : public FBoneIndexBase { ... };
struct FSkeletonPoseBoneIndex : public FBoneIndexBase { ... };
```

**强类型的工程价值**：

| 索引类型 | 语义 | 使用场景 |
|----------|------|----------|
| `FCompactPoseBoneIndex` | Compact Pose 空间索引 | 动画评估时的运行时 Pose，可能剔除未使用骨骼 |
| `FMeshPoseBoneIndex` | 骨骼网格体局部索引 | 蒙皮顶点权重引用 |
| `FSkeletonPoseBoneIndex` | 骨架资产索引 | 动画序列关键帧采样、重定向 |

通过 `explicit` 构造函数和宏生成的比较运算符，UE 在编译期阻止了不同骨骼空间索引的隐式混用。这对于大型动画系统来说是**类型安全的最后一道防线**。

#### 2.4 FBoneWeights：uint16 量化、内联存储与模板化算法

```cpp
// Public/BoneWeights.h (行 47-210)
class FBoneWeight
{
private:
    FBoneIndexType BoneIndex;   // uint16
    uint16 RawWeight;
public:
    static_assert(sizeof(FBoneWeight) == sizeof(int32), "FBoneWeight must be 32-bits");
    void SetWeight(float InWeight)
    {
        InWeight = FMath::Clamp(InWeight, 0.0f, 1.0f);
        RawWeight = static_cast<uint16>(InWeight * static_cast<float>(GetMaxRawWeight()) + 0.5f);
    }
    float GetWeight() const
    {
        return RawWeight / static_cast<float>(GetMaxRawWeight());
    }
};
```

**内存布局洞察**：
- 一个 `FBoneWeight` 恰好 4 字节（`uint16` 索引 + `uint16` 权重），与 `int32` 等宽，便于 SIMD 对齐和缓存行打包。
- 权重采用 `uint16` 量化存储（`0~65535`），在顶点和 Pose 混合中避免了浮点存储的内存膨胀。

```cpp
// Public/BoneWeights.h (行 524-578)
class FBoneWeights
{
    using BoneWeightsTempAllocatorT = TInlineAllocator<MaxInlineBoneWeightCount>;
    using BoneWeightArrayT = TArray<FBoneWeight, BoneWeightsTempAllocatorT>;
    BoneWeightArrayT BoneWeights;
};
```

- `TInlineAllocator<MaxInlineBoneWeightCount>`（`MAX_TOTAL_INFLUENCES`，通常为 8）意味着**小数量权重直接在栈/对象内联存储**，无需堆分配。

```cpp
// Public/BoneWeights.h (行 388-521)
template<typename ContainerAdapter>
class TBoneWeights
{
public:
    void SetBoneWeights(TArrayView<const FBoneWeight> BoneWeights, const FBoneWeightsSettings& InSettings = {});
    bool AddBoneWeight(FBoneWeight InBoneWeight, const FBoneWeightsSettings& InSettings = {});
    void Blend(const TBoneWeights& A, const TBoneWeights& B, float Bias, const FBoneWeightsSettings& InSettings = {});
private:
    ContainerType& Container;
};
```

`TBoneWeights` 是一个**策略模式（Policy/Adapter）模板类**：通过 `ContainerAdapter` 抽象底层容器操作，使得同一套权重算法（裁剪、排序、归一化、混合）可以复用于：
- `FBoneWeights`（完整容器）
- GPU Skin 的紧凑顶点格式
- DCC 工具链的自定义数组

这种设计避免了为每种容器重写一套权重逻辑，是 UE 底层模块"算法与存储分离"的典型范例。

#### 2.5 专用变换类型：FEulerTransform 与 FTransformNoScale

```cpp
// Public/EulerTransform.h (行 30-114)
USTRUCT(BlueprintType)
struct FEulerTransform
{
    GENERATED_BODY()
    UPROPERTY() FVector Location;
    UPROPERTY() FRotator Rotation;  // 欧拉角，便于动画师直观编辑
    UPROPERTY() FVector Scale;
    inline FTransform ToFTransform() const { return FTransform(Rotation.Quaternion(), Location, Scale); }
};
```

```cpp
// Public/TransformNoScale.h (行 16-74)
USTRUCT(BlueprintType)
struct FTransformNoScale
{
    GENERATED_BODY()
    UPROPERTY() FVector Location;
    UPROPERTY() FQuat Rotation;
    inline FTransform ToFTransform() const { return FTransform(Rotation, Location, FVector::OneVector); }
};
```

**为什么需要这些"降级版" Transform？**
- `FEulerTransform` 在约束、Control Rig、动画曲线中广泛使用，因为欧拉角比四元数更**适合逐轴插值与限制**。
- `FTransformNoScale` 在不需要缩放的动画评估路径中**减少 12 字节内存占用**，并避免不必要的缩放分量运算。

---

### 3. 逻辑层：IK 算法、约束系统与动画评估调用链

#### 3.1 Two Bone IK：基于余弦定理的解析解

```cpp
// Private/TwoBoneIK.cpp (行 70-184)
void SolveTwoBoneIK(const FVector& RootPos, const FVector& JointPos, const FVector& EndPos,
    const FVector& JointTarget, const FVector& Effector,
    FVector& OutJointPos, FVector& OutEndPos,
    double UpperLimbLength, double LowerLimbLength,
    bool bAllowStretching, double StartStretchRatio, double MaxStretchScale)
{
    FVector DesiredDelta = Effector - RootPos;
    double DesiredLength = DesiredDelta.Size();
    double MaxLimbLength = LowerLimbLength + UpperLimbLength;

    // 1. 建立 JointPlane：通过 DesiredDir 与 JointTargetDelta 的叉积得到法线
    FVector JointPlaneNormal = DesiredDir ^ JointTargetDelta;
    FVector JointBendDir = ...; // 从 JointTarget 投影到垂直于 DesiredDir 的平面

    // 2. 拉伸处理
    if (bAllowStretching) { ... }

    // 3. 超范围：直接拉直
    if (DesiredLength >= MaxLimbLength)
    {
        OutEndPos = RootPos + (MaxLimbLength * DesiredDir);
        OutJointPos = RootPos + (UpperLimbLength * DesiredDir);
    }
    else
    {
        // 4. 三角形解法：已知三边求角度
        const double TwoAB = 2.0 * UpperLimbLength * DesiredLength;
        const double CosAngle = ((UpperLimbLength*UpperLimbLength) + (DesiredLength*DesiredLength) - (LowerLimbLength*LowerLimbLength)) / TwoAB;
        const double Angle = FMath::Acos(CosAngle);
        const double JointLineDist = UpperLimbLength * FMath::Sin(Angle);
        const double ProjJointDist = FMath::Sqrt(UpperLimbLength*UpperLimbLength - JointLineDist*JointLineDist);

        OutJointPos = RootPos + (ProjJointDist * DesiredDir) + (JointLineDist * JointBendDir);
        OutEndPos = Effector;
    }
}
```

**算法本质**：将 Root→Effector 视为三角形的一边，利用**余弦定理**直接计算 Joint 在 Root-Effector 连线上的投影距离，再结合 `JointBendDir`（由 `JointTarget` 决定的弯曲方向）定位 Joint。

```cpp
// Private/TwoBoneIK.cpp (行 15-60)
void SolveTwoBoneIK(FTransform& InOutRootTransform, FTransform& InOutJointTransform, FTransform& InOutEndTransform, ...)
{
    // ... 先求解位置 ...
    // Root：通过 FQuat::FindBetweenNormals(OldDir, NewDir) 计算 DeltaRotation
    InOutRootTransform.SetRotation(DeltaRotation * InOutRootTransform.GetRotation());
    // Joint：同理
    InOutJointTransform.SetRotation(DeltaRotation * InOutJointTransform.GetRotation());
    // End：仅更新位置，保持输入旋转
    InOutEndTransform.SetTranslation(OutEndPos);
}
```

#### 3.2 FABRIK：Forward & Backward Reaching 迭代法

```cpp
// Private/FABRIK.cpp (行 13-81)
bool SolveFabrik(TArray<FFABRIKChainLink>& InOutChain, const FVector& TargetPosition,
    double MaximumReach, double Precision, int32 MaxIterations)
{
    // 情况 A：目标超出链长，直接拉直
    if (RootToTargetDistSq > FMath::Square(MaximumReach))
    {
        for (int32 LinkIndex = 1; LinkIndex < NumChainLinks; LinkIndex++)
        {
            CurrentLink.Position = ParentLink.Position + (TargetPosition - ParentLink.Position).GetUnsafeNormal() * CurrentLink.Length;
        }
        return true;
    }

    // 情况 B：迭代求解
    while ((Slop > Precision) && (IterationCount++ < MaxIterations))
    {
        // Forward Reaching：从末端向根调整，保持每段长度
        for (int32 LinkIndex = TipBoneLinkIndex - 1; LinkIndex > 0; LinkIndex--)
        {
            CurrentLink.Position = ChildLink.Position + (CurrentLink.Position - ChildLink.Position).GetUnsafeNormal() * ChildLink.Length;
        }
        // Backward Reaching：从根向末端调整
        for (int32 LinkIndex = 1; LinkIndex < TipBoneLinkIndex; LinkIndex++)
        {
            CurrentLink.Position = ParentLink.Position + (CurrentLink.Position - ParentLink.Position).GetUnsafeNormal() * CurrentLink.Length;
        }
        Slop = FMath::Abs(InOutChain[TipBoneLinkIndex].Length - FVector::Dist(...));
    }
    return bBoneLocationUpdated;
}
```

**FABRIK 的优势**：
- 相比 Jacobian 数值解法，FABRIK 是**几何迭代法**，收敛快、无矩阵求导开销。
- `FFABRIKChainLink` 存储 `Position`、`Length`、`BoneIndex`、`TransformIndex` 以及 `ChildZeroLengthTransformIndices`（处理零长度骨骼，如多个末端骨骼共享同一位置）。

#### 3.3 CCDIK：循环坐标下降

```cpp
// Private/CCDIK.cpp (行 10-137)
bool SolveCCDIK(TArray<FCCDIKChainLink>& InOutChain, const FVector& TargetPosition,
    float Precision, int32 MaxIteration, bool bStartFromTail,
    bool bEnableRotationLimit, const TArray<float>& RotationLimitPerJoints)
{
    while ((Distance > Precision) && (IterationCount++ < MaxIteration))
    {
        if (bStartFromTail)
        {
            for (int32 LinkIndex = TipBoneLinkIndex - 1; LinkIndex > 0; --LinkIndex)
                bLocalUpdated |= Local::UpdateChainLink(InOutChain, LinkIndex, ...);
        }
        else
        {
            for (int32 LinkIndex = 1; LinkIndex < TipBoneLinkIndex; ++LinkIndex)
                bLocalUpdated |= Local::UpdateChainLink(InOutChain, LinkIndex, ...);
        }
    }
}
```

每个关节的局部更新逻辑：

```cpp
// Private/CCDIK.cpp (行 14-89，局部函数内)
FVector ToEnd = TipPos - CurrentLinkTransform.GetLocation();
FVector ToTarget = TargetPos - CurrentLinkTransform.GetLocation();
ToEnd.Normalize();
ToTarget.Normalize();

double Angle = FMath::ClampAngle(FMath::Acos(FVector::DotProduct(ToEnd, ToTarget)), -Limit, Limit);
FVector RotationAxis = FVector::CrossProduct(ToEnd, ToTarget);
FQuat DeltaRotation(RotationAxis, Angle);
CurrentLinkTransform.SetRotation(DeltaRotation * CurrentLinkTransform.GetRotation());

// 级联更新子节点
for (int32 ChildLinkIndex = LinkIndex + 1; ChildLinkIndex <= TipBoneLinkIndex; ++ChildLinkIndex)
{
    ChildIterLink.Transform = ChildIterLink.LocalTransform * CurrentParentTransform;
}
```

**CCD 与 FABRIK 的取舍**：
- CCD 逐关节旋转，天然支持**旋转限制**（`RotationLimitPerJoints`），适合机械臂、关节限制严格的角色。
- FABRIK 逐位置迭代，骨骼姿态更自然（无 CCD 的"自扭"问题），但旋转限制需要额外后处理。

#### 3.4 约束系统：SolveConstraints 与 BlendHelper

AnimationCore 提供了两套约束 API：一套基于委托（旧版），一套基于 `FConstraintData`（新版）。

**旧版约束入口**：

```cpp
// Public/AnimationCoreLibrary.h (行 24-36)
namespace AnimationCore
{
    FTransform SolveConstraints(const FTransform& CurrentTransform, const FTransform& BaseTransform,
        const TArray<FTransformConstraint>& Constraints,
        const FGetGlobalTransform& OnGetGlobalTransform);
}
```

```cpp
// Public/Constraint.h (行 284-324)
USTRUCT(BlueprintType)
struct FTransformConstraint
{
    GENERATED_USTRUCT_BODY()
    UPROPERTY() FConstraintDescription Operator;   // 约束类型与轴过滤
    UPROPERTY() FName SourceNode;
    UPROPERTY() FName TargetNode;
    UPROPERTY() float Weight;
    UPROPERTY() bool bMaintainOffset;
};
```

`FConstraintDescription`（`Public/Constraint.h`，行 182-228）定义了可以独立开关的：
- `bTranslation` / `bRotation` / `bScale` / `bParent`
- 每轴过滤：`FFilterOptionPerAxis TranslationAxes`

**约束求解实现**：

```cpp
// Private/AnimationCoreLibrary.cpp (行 46-96)
FTransform SolveConstraints(const FTransform& CurrentTransform, const FTransform& BaseTransform,
    const TArray<FTransformConstraint>& Constraints, const FGetGlobalTransform& OnGetGlobalTransform)
{
    FComponentBlendHelper BlendHelper;
    for (int32 ConstraintIndex = 0; ConstraintIndex < TotalNum; ++ConstraintIndex)
    {
        FTransform ConstraintTransform = OnGetGlobalTransform.Execute(Constraint.TargetNode);
        FTransform ConstraintToParent = ConstraintTransform.GetRelativeTransform(BaseTransform);
        AccumulateConstraintTransform(ConstraintToParent, Constraint.Operator, Constraint.Weight, BlendHelper);
    }

    // 按组件混合
    if (BlendHelper.GetBlendedParent(ParentTransform)) { ... }
    else
    {
        BlendHelper.GetBlendedTranslation(BlendedTranslation);
        BlendHelper.GetBlendedRotation(BlendedRotation);
        BlendHelper.GetBlendedScale(BlendedScale);
    }
    return BlendedTransform;
}
```

**BlendHelper 的旋转混合**：

```cpp
// Private/AnimationCoreUtil.h (行 117-156)
bool GetBlendedRotation(FQuat& Output)
{
    Output = Rotations[0] * (RotationWeights[0] * MultiplyWeight);
    for (int32 Index = 1; Index < Rotations.Num(); ++Index)
    {
        FQuat BlendRotation = Rotations[Index] * (RotationWeights[Index] * MultiplyWeight);
        if ((Output | BlendRotation) < 0)   // 检测四元数符号分歧
        {
            Output.X -= BlendRotation.X;    // 取反后相减，确保最短路径混合
            Output.Y -= BlendRotation.Y;
            Output.Z -= BlendRotation.Z;
            Output.W -= BlendRotation.W;
        }
        else
        {
            Output.X += BlendRotation.X;
            Output.Y += BlendRotation.Y;
            Output.Z += BlendRotation.Z;
            Output.W += BlendRotation.W;
        }
    }
    Output.Normalize();
}
```

这是**加权平均四元数（Weighted Quaternion Average）** 的经典实现。通过点积判断两个四元数是否位于球面的对侧，若对侧则翻转符号，避免"绕远路"插值。

**新版约束（FConstraintDescriptor + FConstraintData）**：

```cpp
// Public/Constraint.h (行 509-745)
USTRUCT()
struct FConstraintDescriptor
{
    UPROPERTY() EConstraintType Type;
    FConstraintDescriptionEx* ConstraintDescription;  // 多态指针，支持 Transform / Aim
    // ...
};

USTRUCT()
struct FConstraintData
{
    UPROPERTY() FConstraintDescriptor Constraint;
    UPROPERTY() float Weight;
    UPROPERTY() bool bMaintainOffset;
    UPROPERTY() FTransform Offset;
    UPROPERTY(transient) FTransform CurrentTransform;
};
```

新版约束采用**虚函数 + 多态描述符**模式（`FConstraintDescriptionEx` 纯虚基类），支持：
- `FTransformConstraintDescription`：位移动画约束
- `FAimConstraintDescription`：Aim/LookAt 约束

```cpp
// Private/AnimationCoreLibrary.cpp (行 141-189)
FTransform SolveConstraints(const FTransform& CurrentTransform, const FTransform& CurrentParentTransform,
    const TArray<FConstraintData>& Constraints)
{
    FMultiTransformBlendHelper BlendHelperInLocalSpace;
    FTransform BlendedLocalTransform = CurrentTransform.GetRelativeTransform(CurrentParentTransform);
    for (const FConstraintData& Constraint : Constraints)
    {
        Constraint.ApplyConstraintTransform(Constraint.CurrentTransform, CurrentTransform, CurrentParentTransform, BlendHelperInLocalSpace);
    }
    return BlendedLocalTransform * CurrentParentTransform;  // 转回世界空间
}
```

#### 3.5 动画评估中的典型调用链

以 AnimGraph 中的 `FAnimNode_TwoBoneIK` 为例，AnimationCore 在调用链中的位置如下：

```
AnimInstance::EvaluateAnimation()
    └── AnimNode_SkeletalControlBase::EvaluateSkeletalControl_AnyThread()
        └── FAnimNode_TwoBoneIK::EvaluateSkeletalControl_AnyThread()
            ├── 提取 CompactPose 中的 Root/Joint/End 位置
            ├── AnimationCore::SolveTwoBoneIK(Root, Joint, End, JointTarget, Effector, ...)
            │   └── 计算 OutJointPos / OutEndPos（纯数学，无 UObject）
            ├── 将结果写回 CompactPose（FCompactPoseBoneIndex）
            └── 若启用约束：AnimationCore::SolveConstraints(...)
```

同理，FABRIK 与 CCDIK 节点也会调用对应的 `AnimationCore::SolveFabrik` / `AnimationCore::SolveCCDIK`。

**关键观察**：
- AnimationCore 的函数签名**不依赖上层动画图结构**，只接收 `FVector`、`FTransform` 或 `TArray<Link>`。
- 这种"底层无状态、上层负责组织数据"的分层，使得 AnimationCore 的 IK/约束算法可以被 Control Rig、Physics、Sequencer 等多个上层系统复用。

---

## 总结与可迁移原理

| 设计决策 | 通用原理 | 可迁移到自研引擎的场景 |
|----------|----------|------------------------|
| 强类型骨骼索引 | 编译期类型安全 | 任何存在多种坐标空间/索引空间的系统（如 ECS 的 Archetype/Chunk/Component 索引） |
| FBoneWeight 的 uint16 量化 + 4 字节打包 | 内存对齐与缓存友好 | 顶点蒙皮、BlendShape 权重、粒子影响力的批量存储 |
| TBoneWeights<Adapter> 模板策略 | 算法与存储解耦 | 需要同一算法作用于多种容器（栈数组、GPU Buffer、稀疏结构）的场景 |
| FNodeHierarchyWithUserData 虚接口 | 无侵入扩展 | 层级数据结构需要支持派生定制（如骨骼、场景图、UI 节点树） |
| FABRIK/CCD/TwoBone 三层 IK 体系 | 按链长度与精度需求选择算法 | 机器人、角色、生物的差异化 IK 需求 |
| 加权四元数平均 | 最短路径旋转混合 | 动画混合树、相机过渡、物理朝向插值 |

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
