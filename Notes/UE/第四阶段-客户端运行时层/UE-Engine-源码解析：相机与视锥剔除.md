---
title: UE-Engine-源码解析：相机与视锥剔除
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - camera
  - culling
aliases:
  - UE Engine 相机与视锥剔除
  - UE 相机系统源码解析
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：相机与视锥剔除

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/Classes/Camera/`、`Engine/Source/Runtime/Engine/Public/SceneView.h`
- **核心类分布**：
  - `Engine/Classes/Camera/`：`UCameraComponent`、`APlayerCameraManager`、`ACameraActor`
  - `Engine/Classes/Engine/SceneView.h`：`FSceneView`、`FViewMatrices`
  - `Engine/Public/ConvexVolume.h`：`FConvexVolume`
  - `Renderer/Private/SceneVisibility.cpp`：视锥剔除实现
- **核心依赖**：`Core`、`CoreUObject`、`RenderCore`、`Renderer`、`RHI`

> **分工定位**：Engine 模块的 Camera 子系统负责**游戏线程侧的相机 POV 计算**（位置、旋转、FOV、投影矩阵）。这些参数通过 `FSceneView` 跨线程传递到渲染线程，再由渲染器执行视锥剔除（Frustum Culling）和实际绘制。

---

## 接口梳理（第 1 层）

### Camera 文件夹核心类

| 类/结构 | 职责 |
|---|---|
| `ACameraActor` | 可放置关卡中的摄像机 Actor，内部持有 `UCameraComponent` |
| `UCameraComponent` | 摄像机视角与设置的核心组件（FOV、正交宽度、裁剪面、宽高比、Overscan、PostProcess 覆盖等） |
| `APlayerCameraManager` | 为特定玩家管理最终相机 POV，负责 ViewTarget 插值、镜头抖动、后处理混合、Fade 等 |
| `UCameraModifier` / `UCameraModifier_CameraShake` | 相机修饰器基类与镜头抖动手持修饰器 |
| `UCameraShakeBase` | 镜头震动的基类 |
| `CameraTypes.h` | 定义 `FMinimalViewInfo`、`ECameraProjectionMode` 等基础数据结构 |

### 渲染视图核心类

| 类/结构 | 文件 | 职责 |
|---|---|---|
| `FSceneView` | `Engine/Public/SceneView.h` | 渲染线程可见的"视图"描述，含 ViewMatrices、ViewFrustum、UnscaledViewRect 等 |
| `FViewInfo` | `Renderer/Private/SceneRendering.h` | 渲染器内部视图信息，继承自 `FSceneView`，含 VisibilityMap、DynamicMeshElements 等 |
| `FConvexVolume` | `Engine/Public/ConvexVolume.h` | 由一组 `FPlane` 构成的凸体，提供 `IntersectBox`、`IntersectSphere` 等测试 |

---

## 数据结构（第 2 层）

### FMinimalViewInfo — 相机 POV 缓存

> 文件：`Engine/Source/Runtime/Engine/Classes/Camera/CameraTypes.h`

```cpp
USTRUCT(BlueprintType)
struct FMinimalViewInfo
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    FVector Location;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    FRotator Rotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    float FOV;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    float OrthoWidth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    float AspectRatio;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
    TEnumAsByte<ECameraProjectionMode::Type> ProjectionMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=PostProcess)
    FPostProcessSettings PostProcessSettings;
};
```

`APlayerCameraManager` 每 Tick 将最终相机参数计算到 `FMinimalViewInfo`，并存入 `CameraCachePrivate`。后续 `ULocalPlayer::GetViewPoint` 直接读取该缓存，避免重复计算。

### APlayerCameraManager — 玩家相机管理器

> 文件：`Engine/Source/Runtime/Engine/Classes/Camera/PlayerCameraManager.h`，第 187~250 行

```cpp
UCLASS(notplaceable, transient, BlueprintType, Blueprintable, Config=Engine, MinimalAPI)
class APlayerCameraManager : public AActor
{
    UPROPERTY(transient)
    TObjectPtr<class APlayerController> PCOwner;

    virtual void UpdateCamera(float DeltaTime);
    virtual void DoUpdateCamera(float DeltaTime);
    virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime);
    virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);

    FCameraCacheEntry CameraCachePrivate;
    FTViewTarget ViewTarget;
    FTViewTarget PendingViewTarget;
    TArray<TObjectPtr<UCameraModifier>> ModifierList;
};
```

核心职责：
- **ViewTarget 管理**：`ViewTarget.Target` 是当前跟随的 Actor，`PendingViewTarget` 是切换中的目标
- **混合插值**：`BlendTimeToGo` / `BlendParams` 控制相机切换时的平滑过渡
- **Camera Modifiers**：应用镜头抖动、后效、Fade、ColorScale 等
- **网络同步**：`ServerUpdateCamera` 处理客户端相机位置同步

### FSceneView — 跨线程视图描述

> 文件：`Engine/Source/Runtime/Engine/Public/SceneView.h`

```cpp
class FSceneView
{
    const FSceneViewFamily* Family;
    FSceneViewStateInterface* State;
    FViewMatrices ViewMatrices;
    FVector ViewLocation;
    FRotator ViewRotation;
    FConvexVolume ViewFrustum;
    FIntRect UnscaledViewRect;
    float FOV;
    bool bCameraCut;
    TSet<FPrimitiveComponentId> HiddenPrimitives;
};
```

`FSceneView` 由游戏线程构建，通过 `FSceneViewFamily` 投递到渲染线程。它是**游戏线程与渲染线程关于"相机状态"的唯一契约**。

### FConvexVolume — 视锥体数学表示

> 文件：`Engine/Source/Runtime/Engine/Public/ConvexVolume.h`

```cpp
struct FConvexVolume
{
    TArray<FPlane, TInlineAllocator<6>> Planes;
    TArray<FPlane4f, TInlineAllocator<8>> PermutedPlanes;

    bool IntersectBox(const FVector& Origin, const FVector& Extent) const;
    bool IntersectSphere(const FVector& Origin, float Radius) const;
};
```

`PermutedPlanes` 是为了 SIMD 优化而重新排列的平面数组，支持 `IntersectBox8Plane` 一次性测 8 个平面。

---

## 行为分析（第 3 层）

### 相机变换传递到渲染线程的完整流程

#### 游戏线程侧

```mermaid
graph LR
    A[APlayerCameraManager::UpdateCamera] --> B[计算 FMinimalViewInfo]
    B --> C[存入 CameraCachePrivate]
    C --> D[ULocalPlayer::GetViewPoint]
    D --> E[ULocalPlayer::CalcSceneView]
    E --> F[构建 FSceneViewInitOptions]
    F --> G[创建 FSceneView]
```

1. **`APlayerCameraManager::UpdateCamera(float)`** 每 Tick 计算最终 `FMinimalViewInfo`
   - 调用 `UpdateViewTarget` 获取 ViewTarget 的 POV
   - 应用 `ApplyCameraModifiers`（镜头抖动、Fade 等）
   - 调用 `FillCameraCache` 存入 `CameraCachePrivate`

2. **`ULocalPlayer::GetViewPoint(FMinimalViewInfo&)`** 从 `PlayerCameraManager` 读取缓存

3. **`ULocalPlayer::CalcSceneView(...)`**（`LocalPlayer.cpp`）
   - 调用 `GetProjectionData` 构建 `FSceneViewInitOptions`
   - 填入 Location/Rotation/FOV/AspectRatio
   - `new FSceneView(ViewInitOptions)` 创建视图对象
   - 加入 `FSceneViewFamily`

#### 渲染线程侧

4. `FSceneRenderer`（如 `FDeferredShadingRenderer`）接收 `FSceneViewFamily`
5. 在 **InitViews** 阶段：
   - 将 `FSceneView` 拷贝/扩展为 `FViewInfo`
   - 调用 `SetupViewFrustum()` 根据 `ViewMatrices.GetViewProjectionMatrix()` 生成 `ViewFrustum`
   - 执行 `FrustumCull` 与 `Occlusion Cull`，填充 `FViewInfo::PrimitiveVisibilityMap`

### 视锥剔除（Frustum Culling）流程

> 文件：`Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp`

```cpp
// 核心入口
void FrustumCull(...)
{
    for (每个 Primitive)
    {
        if (IsPrimitiveVisible(Primitive, ViewFrustum, ...))
        {
            VisibilityMap[PrimitiveIndex] = true;
        }
    }
}

bool IsPrimitiveVisible(...)
{
    // 1. 可选球体快速剔除
    if (bUseSphereTestFirst && !ViewFrustum.IntersectSphere(Origin, Radius))
        return false;

    // 2. 盒体精确剔除
    if (bUseFastIntersect)
        return IntersectBox8Plane(Origin, Extent, PermutedPlanes);
    else
        return ViewFrustum.IntersectBox(Origin, Extent);
}
```

控制台变量：
- `r.Visibility.FrustumCull.Enabled`
- `r.Visibility.FrustumCull.UseSphereTestFirst`
- `r.Visibility.FrustumCull.UseFastIntersect`（SIMD 优化）

### ViewTarget 切换与插值

> 文件：`Engine/Source/Runtime/Engine/Classes/Camera/PlayerCameraManager.h`，第 115~165 行

```cpp
USTRUCT(BlueprintType)
struct FViewTargetTransitionParams
{
    float BlendTime;
    TEnumAsByte<enum EViewTargetBlendFunction> BlendFunction; // Linear/Cubic/EaseIn/EaseInOut
    float BlendExp;
    uint32 bLockOutgoing:1;
};
```

当调用 `SetViewTarget(NewTarget, TransitionParams)` 时：
1. `PendingViewTarget` 设置为新目标
2. `BlendTimeToGo` 设置为 `BlendTime`
3. 每 Tick `DoUpdateCamera` 中，根据 `BlendFunction` 和当前插值进度混合 `ViewTarget.POV` 与 `PendingViewTarget.POV`
4. 混合完成后，`ViewTarget = PendingViewTarget`

---

## 与上下层的关系

### 下层依赖

| 下层模块 | 作用 |
|---------|------|
| `Core` / `CoreUObject` | 基础类型、USTRUCT 反射、Actor/Component 体系 |
| `RenderCore` / `Renderer` / `RHI` | 接收 `FSceneView`、执行视锥剔除和实际渲染 |

### 上层调用者

| 上层模块 | 使用方式 |
|---------|---------|
| `Gameplay 项目代码` | 通过 `APlayerController::PlayerCameraManager` 获取/设置相机；通过 `UCameraComponent` 配置视角 |
| `Level Sequencer` | 控制 `ACameraActor` 实现过场动画切镜 |
| `Renderer` | 读取 `FSceneView` 进行裁剪和绘制 |

---

## 设计亮点与可迁移经验

1. **PlayerCameraManager 作为相机仲裁器**：UE 不直接让 `UCameraComponent` 决定最终视图，而是通过 `APlayerCameraManager` 做一层仲裁。这让多个相机源（Pawn、CameraActor、Modifier）可以竞争和混合，是复杂相机系统（如战斗、对话、过场）的基础设计。
2. **CameraCache 避免重复计算**：`FMinimalViewInfo` 被缓存到 `CameraCachePrivate`，同一 Tick 内多个系统（渲染、音频、AI）读取相机状态时无需重复执行完整更新。
3. **FSceneView 作为跨线程契约**：游戏线程只负责填充 `FSceneView`，绝不直接操作渲染器内部结构。这种"只读快照 + 跨线程投递"模式是引擎多线程设计的经典范式。
4. **SIMD 优化的视锥剔除**：`FConvexVolume::PermutedPlanes` 和 `IntersectBox8Plane` 利用 SSE/AVX 指令一次测试 8 个平面，大幅提升了大规模场景下的剔除效率。自研引擎在写视锥剔除时，应优先考虑 SIMD 友好的数据布局。
5. **Overscan 支持**：`UCameraComponent` 支持 `Overscan` 和 `AsymmetricOverscan`，可以在不扩展实际 FOV 的情况下渲染更多像素，供后期处理（如镜头畸变）使用。这对影视级渲染和游戏内录制非常重要。

---

## 关键源码片段

### APlayerCameraManager 核心声明

> 文件：`Engine/Source/Runtime/Engine/Classes/Camera/PlayerCameraManager.h`，第 187~210 行

```cpp
UCLASS(notplaceable, transient, BlueprintType, Blueprintable, Config=Engine, MinimalAPI)
class APlayerCameraManager : public AActor
{
    UPROPERTY(transient)
    TObjectPtr<class APlayerController> PCOwner;

    virtual void UpdateCamera(float DeltaTime);
    virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime);
    virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);

    FCameraCacheEntry CameraCachePrivate;
};
```

### FSceneView 关键字段

> 文件：`Engine/Source/Runtime/Engine/Public/SceneView.h`

```cpp
class FSceneView
{
    FViewMatrices ViewMatrices;
    FVector ViewLocation;
    FRotator ViewRotation;
    FConvexVolume ViewFrustum;
    FIntRect UnscaledViewRect;
    float FOV;
    bool bCameraCut;
};
```

### 视锥剔除入口

> 文件：`Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp`

```cpp
void FrustumCull(...)
{
    for (每个 Primitive)
    {
        if (IsPrimitiveVisible(Primitive, ViewFrustum, ...))
        {
            VisibilityMap[PrimitiveIndex] = true;
        }
    }
}
```

---

## 关联阅读

- [[UE-Engine-源码解析：渲染管线与光照]] — FSceneView 如何被渲染器使用
- [[UE-Engine-源码解析：后处理与屏幕空间效果]] — CameraModifier 与 PostProcess 的交互
- [[UE-MovieScene-源码解析：Sequencer 与过场动画]] — 过场动画中的相机控制

---

## 索引状态

- **所属 UE 阶段**：第四阶段 — 客户端运行时层 / 4.4 玩法运行时与同步
- **对应 UE 笔记**：UE-Engine-源码解析：相机与视锥剔除
- **本轮完成度**：✅ 第三轮（骨架扫描 + 血肉填充 + 关联辐射 已完成）
- **更新日期**：2026-04-17
