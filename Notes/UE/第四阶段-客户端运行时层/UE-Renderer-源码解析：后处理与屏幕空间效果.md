---
title: UE-Renderer-源码解析：后处理与屏幕空间效果
date: 2026-05-25
tags:
  - ue-source
  - renderer
  - post-processing
  - rdg
  - temporal-aa
  - global-shader
aliases:
  - UE 后处理架构源码解析
  - UE PostProcessing 源码分析
---

> [[Notes/UE/00-UE全解析主索引.md|← 返回 UE 全解析主索引]]

---

## 引言

后处理（Post-Processing）是现代渲染管线的"最后一公里"——场景颜色（SceneColor）生成之后、像素真正写入屏幕之前，所有发生在屏幕空间的全屏操作都归它管。从 Bloom、Tone Mapping、TAA 到 FXAA、Color Grading，这些决定画面最终观感的效果，全部在 Renderer 模块的 PostProcess 子系统中完成。

**为什么要专门研究这部分源码？** 因为后处理是 UE 渲染管线中最适合用 RDG（Rendering Dependency Graph）来体现其设计优势的领域：大量短暂生命周期的中间纹理、严格有序的 Pass 依赖、以及需要跨帧复用的历史缓冲——这些恰好是 RDG 最擅长解决的问题。同时，后处理也是引擎与第三方插件（DLSS、FSR）集成最深的环节，理解它的架构有助于看清 UE 的可扩展性设计。

本文按三层剥离法，从暴露给场景编辑器和 gameplay 的公共接口，到驱动这些效果的数据结构，再到 Renderer 模块中真正编排 GPU 工作的执行逻辑，逐层解析 UE 的后处理系统。

---

## 模块定位

后处理系统的源码分布在 UE 的多个模块中，核心集中在 Renderer：

| 模块 | 路径 | 职责 |
|------|------|------|
| **Renderer** | `Engine/Source/Runtime/Renderer/Private/PostProcess/` | 后处理 Pass 的具体实现：Bloom、Tonemap、TAA、FXAA、DOF 等 |
| **Renderer** | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp` | 后处理总调度函数 `AddPostProcessingPasses` |
| **RenderCore** | `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | RDG 框架，编排后处理 Pass 与资源生命周期 |
| **Engine** | `Engine/Source/Runtime/Engine/Classes/Engine/PostProcessVolume.h` | `APostProcessVolume` 和 `FPostProcessSettings` 定义 |
| **Engine** | `Engine/Source/Runtime/Engine/Classes/Components/PostProcessComponent.h` | `UPostProcessComponent` 定义 |
| **Shader** | `Engine/Shaders/Private/PostProcess/` | 后处理着色器源码（`.usf`） |

---

## 第一层：接口层（What）

### 1.1 PostProcessVolume：场景空间的后处理配置器

**APostProcessVolume** 继承自 `AVolume`，是 UE 中最直观的后处理配置入口。它的核心设计思想是"局部覆盖"——通过空间体积（Volume）让不同区域拥有不同的画面风格。

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/PostProcessVolume.h`

```cpp
class APostProcessVolume : public AVolume
{
    // 后处理设置结构体，包含所有可覆盖的参数
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    FPostProcessSettings Settings;

    // 优先级：重叠时高 Priority 的 Volume 先参与混合
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    float Priority;

    // 混合权重：0 = 无效果，1 = 完全效果
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    float BlendWeight;

    // 混合半径：Volume 边界外多少距离开始过渡
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    float BlendRadius;

    // 是否无边界：true 则作用于整个世界
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    uint32 bUnbound : 1;

    // 是否启用
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessVolume")
    uint32 bEnabled : 1;
};
```

**Volume 的混合权重机制**：当摄像机处于多个 Volume 的重叠区域时，UE 不会简单地用 Priority 做"赢者通吃"。实际流程是：
1. 收集所有影响当前摄像机的 Volume（按 Priority 排序）。
2. 对每个后处理参数，按 `BlendWeight` 做线性插值（lerp）混合。
3. `BlendRadius` 控制摄像机在 Volume 边界附近时的过渡曲线——距离边界越远，`BlendWeight` 越接近 0。
4. `bUnbound = true` 的 Volume 忽略空间边界，常用于全局基调（如全局 Bloom、全局 Tone Mapping）。

> **注意**：如果两个 Volume 的 Priority 相同，混合顺序是未定义的。实际项目中应确保重叠 Volume 的 Priority 有明确层级。

### 1.2 PostProcessComponent：Actor 级别的后处理挂载点

除了关卡中放置的 Volume，UE 还允许将后处理绑定到任意 Actor 上——这就是 **UPostProcessComponent**。

> 文件：`Engine/Source/Runtime/Engine/Classes/Components/PostProcessComponent.h`

```cpp
class UPostProcessComponent : public USceneComponent
{
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessComponent")
    FPostProcessSettings Settings;

    // 同 Volume 的 Priority，用于与其他 Component/Volume 排序
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessComponent")
    float Priority;

    // 同 Volume 的 BlendWeight
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessComponent")
    float BlendWeight;

    // 是否忽略边界（Component 没有物理体积，通常设为 true）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessComponent")
    uint32 bUnbound : 1;
};
```

Volume 与 Component 的底层混合逻辑是统一的：渲染线程在准备 `FViewInfo` 时，会将所有影响该视图的 Volume 和 Component 的 `FPostProcessSettings` 按 Priority 排序后混合，最终结果写入 `View.FinalPostProcessSettings`。

### 1.3 FPostProcessSettings：全局 vs 局部配置的数据结构

**FPostProcessSettings** 是后处理系统的"配置清单"——它用一个巨大的结构体囊括了所有后处理参数，从 Bloom 强度到 Tone Mapping 曲线，从 Color Grading LUT 到 Film Grain。

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/Scene.h`

```cpp
struct FPostProcessSettings
{
    // 每个参数都有对应的 bOverride_XXX 标志位
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bloom")
    uint32 bOverride_BloomIntensity : 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bloom")
    float BloomIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bloom")
    uint32 bOverride_BloomThreshold : 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bloom")
    float BloomThreshold;

    // ... 数十个类似参数

    // Blendable 后处理材质数组（UE5 中为 WeightedBlendables）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PostProcessMaterials")
    TArray<FWeightedBlendable> WeightedBlendables;
};
```

**关键设计**：每个参数都有 `bOverride_` 前缀的布尔标志。这决定了混合时的行为：
- 如果 Volume A 勾选了 `bOverride_BloomIntensity`，而 Volume B 没有勾选，则混合时只取 A 的 `BloomIntensity`，B 的该字段被忽略。
- 这种"按字段覆盖"的机制让不同 Volume 可以分别控制不同参数，而不会互相污染。

**全局配置的兜底**：如果没有任何 Volume/Component 覆盖某个参数，UE 会使用 `UScene` 中定义的默认值，或者项目配置（`DefaultEngine.ini`）中的全局设置。

### 1.4 Blendable：后处理材质的插入机制

除了内置参数，UE 还支持通过后处理材质（Post Process Material）来自定义全屏效果。这类材质在编辑器中设置 **Material Domain = Post Process**，然后被添加到 Volume 的 **Blendables**（UE5 中显示为 Post Process Materials）数组中。

Blendable 的插入位置由材质的 **Blendable Location** 决定：

| 插入位置 | 含义 | 典型用途 |
|----------|------|----------|
| `BL_BeforeTranslucency` | 半透明合成之前 | 极早期处理，如风格化描边 |
| `BL_BeforeTonemapping` | Tone Mapping 之前，HDR 阶段 | 需要 HDR 信息的自定义效果 |
| `BL_ReplacingTonemapper` | 替换 UE 默认 Tone Mapping | 自定义色调映射器 |
| `BL_AfterTonemapping` | Tone Mapping 之后，LDR 阶段 | 性能友好，如扫描线、暗角 |
| `BL_SSRInput` | SSR 输入阶段 | 自定义屏幕空间反射输入 |

每个 Blendable 还有 **Blendable Priority**，用于在同一 Location 内排序。UE 在渲染时会调用 `GetPostProcessMaterialChain(View, Location)` 获取该位置的所有材质，按优先级排序后依次执行。

> **重要**：后处理材质在内部通过 `IBlendableInterface` 接口统一处理。UE5 中虽然 UI 上叫"Post Process Materials"，但底层数据结构仍然是 `WeightedBlendables`，保留了未来扩展其他 Blendable 类型的可能性。

---

## 第二层：数据层（How - Structure）

### 2.1 FPostProcessingInputs：后处理阶段的输入数据包

后处理不是凭空运行的，它需要前端渲染阶段产出的各种缓冲。这些数据通过 `FPostProcessingInputs` 打包传入 `AddPostProcessingPasses`：

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.h`

```cpp
struct FPostProcessingInputs
{
    // 场景纹理集合（SceneColor、SceneDepth、GBuffer、Velocity 等）
    FRDGTextureMSAA SceneTextures;

    // 独立半透明纹理
    FSeparateTranslucencyTextures* SeparateTranslucencyTextures;

    // 视图族输出目标（Swap Chain 或 Capture Target）
    FRDGTextureRef ViewFamilyTexture;

    // ... 其他辅助数据
};
```

**SceneColor 的流动**：后处理管线的核心就是围绕 **SceneColor** 纹理的"接力"——每个 Pass 读取上一 Pass 产出的 SceneColor，处理后输出到新的纹理，最终写入 `ViewFamilyTexture`。

### 2.2 TOverridePassSequence：后处理 Pass 的编排状态机

UE5 的后处理不再像 UE4 那样用传统的 `FRenderingCompositePass` 链式结构，而是基于 RDG 的 `AddPass` 直接构建。但为了管理大量可选 Pass 的启用/禁用和覆盖逻辑，UE 引入了 `TOverridePassSequence`：

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp`

```cpp
enum class EPass : uint32
{
    MotionBlur,           // 运动模糊
    Tonemap,              // 色调映射
    FXAA,                 // FXAA 抗锯齿
    PostProcessMaterialAfterTonemapping, // 色调映射后的后处理材质
    VisualizeDepthOfField,// DOF 可视化
    // ... 编辑器可视化、调试 Pass
    PrimaryUpscale,       // 主放大（Spatial Upscale / Panini）
    SecondaryUpscale,     // 次放大
    MAX
};

TOverridePassSequence<EPass> PassSequence(ViewFamilyOutput);
```

这个枚举+模板类的设计让 UE 可以：
1. **按条件启用/禁用 Pass**：根据 `EngineShowFlags`、CVar、后处理设置等决定是否执行某个 Pass。
2. **支持覆盖输出**：如果某个 Pass 是序列的最后一个，它可以直接写入 `ViewFamilyOutput`，避免一次额外的 Blit。
3. **插件扩展点**：`ISceneViewExtension` 的 `SubscribeToPostProcessingPass` 允许插件在指定 Pass 前后注入自定义逻辑。

### 2.3 FTemporalAAHistory：TAA 的历史帧容器

时域抗锯齿（TAA）的核心是复用上一帧的渲染结果。UE 用 `FTemporalAAHistory` 来封装这种跨帧状态：

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.h`

```cpp
struct FTemporalAAHistory
{
    // 历史颜色缓冲（可有多帧，但通常主要用 RT[0]）
    TRefCountPtr<IPooledRenderTarget> RT[2];

    // 历史帧的视口矩形
    FIntRect ViewportRect;

    // 参考缓冲尺寸（用于缩放匹配）
    FIntPoint ReferenceBufferSize;

    // 历史是否有效（首帧或分辨率变化时可能无效）
    bool bHistoryValid;
};
```

历史缓冲的存储位置：`FSceneViewState`（每个视图的状态对象）中保存了 `PrevFrameViewInfo`，其中包含：
- `TemporalAAHistory`：TAA/TSR 的主历史颜色缓冲
- `HalfResTemporalAAHistory`：半分辨率历史（供 SSR 等复用）
- `CustomSSRInput`：SSR 输入的历史数据

### 2.4 后处理中间纹理的分配策略

后处理阶段会产生大量临时纹理（如下采样链、Bloom 各级、Eye Adaptation 缓冲）。UE5 通过两种机制管理它们：

**1. Transient Texture（瞬时纹理）**：
- 由 `FRDGBuilder::CreateTexture()` 创建，生命周期严格限定在单帧的 RDG 图内。
- 底层通过 `IRHITransientResourceAllocator` 分配，执行完毕后立即回收内存。
- 支持**内存别名（Aliasing）**：生命周期不重叠的纹理可以复用同一块 GPU 内存。

**2. Pooled Texture（池化纹理）**：
- 通过 `FRenderTargetPool` 管理的纹理，跨帧复用。
- 历史缓冲（如 `FTemporalAAHistory::RT`）必须使用池化纹理，因为它们需要在帧之间保持数据。
- `QueueTextureExtraction()` 将 RDG 纹理提取为 `IPooledRenderTarget`，使其生命周期延续到下一帧。

---

## 第三层：逻辑层（How - Execution）

### 3.1 AddPostProcessingPasses：后处理总调度

后处理的入口只有一个：`AddPostProcessingPasses`。它在 `FDeferredShadingSceneRenderer::Render()` 的末尾被调用，负责把整个后处理管线添加到 RDG 中。

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp`

```cpp
void AddPostProcessingPasses(
    FRDGBuilder& GraphBuilder,
    const FViewInfo& View,
    int32 ViewIndex,
    // ... 场景Uniform、光照方法等参数
    const FPostProcessingInputs& Inputs)
{
    // 获取场景纹理参数
    const FSceneTextureParameters SceneTextureParameters = 
        GetSceneTextureParameters(GraphBuilder, Inputs.SceneTextures);

    // 初始化 SceneColor 和各种标记
    FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);
    // ... 初始化 EyeAdaptation、Histogram、Bloom 等变量

    // 根据视图设置决定各 Pass 是否启用
    const bool bBloomEnabled = View.FinalPostProcessSettings.BloomIntensity > 0.0f;
    const bool bMotionBlurEnabled = IsMotionBlurEnabled(View);
    const EAntiAliasingMethod AntiAliasingMethod = View.AntiAliasingMethod;
    // ...
}
```

这个函数的代码量很大（数百行），但其核心逻辑可以概括为**"按固定顺序依次调用各个 AddXxxPass 函数，把 SceneColor 接力传递"**。

### 3.2 后处理 Pass 链的具体顺序

基于 UE5 源码（`PostProcessing.cpp`），完整的后处理 Pass 链顺序如下。注意：许多 Pass 是条件执行的，取决于项目设置和 ShowFlags。

```
[输入：SceneColor (HDR), SceneDepth, Velocity, SeparateTranslucency]
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Before Translucency 后处理材质 (BL_BeforeTranslucency)    │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 景深 (Diaphragm DOF)                                      │
│    - 如果启用 DOF，SceneColor 经过 DOF 模糊                   │
│    - 然后合成 SeparateTranslucency（独立半透明）              │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Before Tonemapping 后处理材质 (BL_BeforeTonemapping)      │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 时域抗锯齿 / 上采样 (TAA / TSR / DLSS / FSR)              │
│    - UE5 默认走 TSR (Temporal Super Resolution)              │
│    - 输入：SceneColor + SceneDepth + Velocity                │
│    - 输出： upscaled/denoised SceneColor                     │
│    - 同时更新 Temporal History Buffer（供下一帧使用）         │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. SSR Input 后处理材质 (BL_SSRInput)                        │
│    - 输出保存到 PrevFrameViewInfo.CustomSSRInput             │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. 运动模糊 (Motion Blur)                                    │
│    - 基于 Velocity Buffer 的方向性模糊                       │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 半分辨率下采样 (HalfResolutionSceneColor)                 │
│    - 用于 Bloom、Histogram、Eye Adaptation 等                │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 8. 直方图 (Histogram)                                        │
│    - 仅在启用自动曝光 + Histogram 模式时执行                 │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 9. 自动曝光 / 人眼适应 (Eye Adaptation)                      │
│    - Basic 模式：基于下采样链的 Alpha 通道                   │
│    - Histogram 模式：基于上面生成的 Histogram 纹理           │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 10. 泛光 (Bloom)                                             │
│    - Bloom Setup（可选阈值过滤）                              │
│    - 下采样链生成（复用 SceneDownsampleChain 或单独生成）     │
│    - Bloom Pass（实际模糊与合成）                             │
│    - Lens Flares（镜头光晕，可选）                            │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 11. 色调映射 (Tone Mapping)                                  │
│    - 如果存在 ReplacingTonemapper 材质，执行材质 Pass          │
│    - 否则执行 UE 默认 Tonemap Pass（ACES Filmic）            │
│    - 同时合成 Bloom、应用 Color Grading LUT、Chromatic       │
│      Aberration、Vignette、Film Grain 等                     │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 12. FXAA 抗锯齿 (若 AntiAliasingMethod == AAM_FXAA)          │
│    - 纯粹的图像空间边缘平滑，速度极快                         │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 13. After Tonemapping 后处理材质 (BL_AfterTonemapping)       │
│    - LDR 阶段，性能开销最低                                   │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 14. 主放大 (Primary Upscale)                                 │
│    - 处理 Primary Screen Percentage 的 Spatial Upscale       │
│    - 或 Panini Projection（VR 投影校正）                      │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 15. 次放大 (Secondary Upscale)                               │
│    - Secondary Screen Percentage 的放大到最终输出分辨率      │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
[输出到 ViewFamilyTexture]
```

**为什么是这个顺序？** 这是由物理正确性和数据依赖决定的：
- TAA/TSR 必须在 Tone Mapping 之前，因为它需要在 HDR 空间做历史帧混合。
- Bloom 必须在 Tone Mapping 之前，因为 Bloom 的输入是 HDR 亮度。
- FXAA 必须在 Tone Mapping 之后，因为它只需要处理 LDR 图像的边缘。
- 后处理材质的 Before/After Tonemapping 位置由用户显式指定，UE 只是按约定预留了插槽。

### 3.3 RDG 如何编排后处理 Pass

UE5 的后处理完全建立在 RDG 之上。每个子效果（如 `AddBloomPass`、`AddTonemapPass`）内部都会调用 `GraphBuilder.AddPass()` 来声明自己的 GPU 工作。

以 Bloom 的下采样链为例：

```cpp
// Bloom 下采样链的初始化
FSceneDownsampleChain BloomDownsampleChain;
BloomDownsampleChain.Init(GraphBuilder, View, EyeAdaptationParameters, 
                          DownsampleInput, DownsampleQuality, bLogLumaInAlpha);

// AddBloomPass 内部会读取下采样链的最后一级，生成 Bloom 纹理
FBloomOutputs PassOutputs = AddBloomPass(GraphBuilder, View, PassInputs);
```

`FSceneDownsampleChain::Init` 内部会连续添加多个 `AddPass`，每级将上一级纹理作为输入，输出到下一级。RDG 会自动推导这些 Pass 之间的资源依赖，并确保执行顺序。

**关键 API**：

| API | 用途 |
|-----|------|
| `GraphBuilder.CreateTexture(Desc, Name)` | 创建瞬时纹理（不立即分配 GPU 内存） |
| `GraphBuilder.RegisterExternalTexture(PooledRT)` | 将池化纹理注册进 RDG |
| `GraphBuilder.QueueTextureExtraction(RDGTexture, &PooledRT)` | 提取纹理，延长生命周期到图外 |
| `GraphBuilder.AddPass(Name, Parameters, Flags, Lambda)` | 声明一个 Pass |

### 3.4 Transient Resource Allocator：中间纹理的复用艺术

后处理是 GPU 内存压力的"重灾区"：Bloom 需要 4~6 级下采样纹理，TAA 需要多帧历史缓冲，Eye Adaptation 需要中间计算纹理。RDG 的 `IRHITransientResourceAllocator` 通过**内存别名**大幅缓解了这个问题。

**原理**：在 `FRDGBuilder::Execute()` 的编译阶段，RDG 会分析所有纹理的生命周期（从第一次被写入到最后一次被读取）。如果两个纹理的生命周期不重叠，且格式/尺寸兼容，它们可以复用同一块 GPU 内存。

> 文件：`Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h`

```cpp
class FRDGBuilder
{
    // 后端提供的瞬时资源分配器
    IRHITransientResourceAllocator* TransientResourceAllocator = nullptr;
};
```

**Bloom 中的典型复用**：Bloom 的 6 级下采样纹理，在 Bloom Pass 执行完毕后就不再被读取。RDG 可以让这些纹理与后续 Pass（如 Tone Mapping 的临时 LUT）复用同一块内存。开发者无需手动管理这种复用——RDG 的编译器自动完成。

**历史缓冲的例外**：TAA 的 `SceneColorHistory` 不能是 Transient 的，因为它需要跨帧保持数据。这类纹理使用 `QueueTextureExtraction` 提取为 `IPooledRenderTarget`，由 `FRenderTargetPool` 管理跨帧复用。

### 3.5 TAA/TSR 的历史帧管理细节

UE5 的默认时域上采样是 **TSR（Temporal Super Resolution）**，它相比 UE4 的 TAA 增加了更多 Pass。核心流程在 `AddTemporalSuperResolutionPasses` 中：

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.cpp`

```cpp
void FDefaultTemporalUpscaler::AddPasses(
    FRDGBuilder& GraphBuilder,
    const FViewInfo& View,
    const FPassInputs& PassInputs,
    FRDGTextureRef* OutSceneColorTexture,
    FIntRect* OutSceneColorViewRect) const
{
    // 若支持 Gen5 TAA（UE5 默认），进入 TSR 路径
    if (CVarTAAAlgorithm.GetValueOnRenderThread() && 
        DoesPlatformSupportGen5TAA(View.GetShaderPlatform()))
    {
        return AddTemporalSuperResolutionPasses(
            GraphBuilder, View, PassInputs,
            OutSceneColorTexture, OutSceneColorViewRect);
    }
    // ... 否则走传统 TAA
}
```

TSR 的主要 Pass（基于 RenderDoc 捕获和源码分析）：

| Pass | 职责 |
|------|------|
| Clear Prev Textures | 清理上一帧的临时纹理 |
| Dilate Velocity | 放大 Velocity Buffer，避免运动边缘的重投影错误 |
| Rejection | 过滤无效速度（如静止物体的亚像素抖动） |
| Frequency Filter | 分离高频/低频信息，分别处理 |
| History Comparison | 将当前帧与历史缓冲对比，检测遮挡变化 |
| Reprojection | 用 Velocity 将历史颜色重投影到当前帧 |
| History Update | 混合当前帧与重投影历史，输出新历史缓冲 |

**History Buffer 的分配与复用策略**：

1. **每帧提取**：TSR 在每帧结束时，通过 `GraphBuilder.QueueTextureExtraction(HistoryTexture, &ViewState->PrevFrameViewInfo.TemporalAAHistory.RT[0])` 将当前输出提取到池化纹理中。

2. **下帧注册**：下一帧开始时，这个池化纹理通过 `GraphBuilder.RegisterExternalTexture(History.RT[0])` 重新注册进 RDG，作为 TSR History Update Pass 的输入。

3. **分辨率变化处理**：如果视口大小改变，`ReferenceBufferSize` 会与新尺寸不匹配，TSR 会检测到这种情况并标记 `bHistoryValid = false`，丢弃旧历史重新累积。

4. **Motion Vector 缓冲的来源**：Velocity Buffer 不是后处理阶段生成的——它在前端的 Base Pass（或 Velocity Pass）中写入 GBuffer。TAA/TSR 只是读取它。对于静态物体，UE 还可以通过前一帧和当前帧的 View-Projection 矩阵在 Shader 中实时推导速度，以节省带宽。

### 3.6 后处理 Shader 的组织方式

UE 的后处理 Shader 分为两种组织形式：

**1. Global Shader（引擎内置效果）**：

所有内置后处理效果（Bloom、Tonemap、FXAA、TAA 等）都使用 `FGlobalShader` 子类实现。以 Bloom Setup 为例：

> 文件：`Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessBloomSetup.cpp`

```cpp
class FBloomSetupPS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FBloomSetupPS);
    SHADER_USE_PARAMETER_STRUCT(FBloomSetupPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
        SHADER_PARAMETER(float, BloomThreshold)
        // ...
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FBloomSetupPS, 
    "/Engine/Private/PostProcessBloomSetup.usf", 
    "MainPS", SF_Pixel);
```

特点：
- C++ 类与 `.usf` 文件通过 `IMPLEMENT_GLOBAL_SHADER` 宏静态绑定。
- 参数通过 `BEGIN_SHADER_PARAMETER_STRUCT` 声明，RDG 自动处理资源过渡。
- 每个 Pass 的 C++ 代码（如 `AddBloomSetupPass`）负责创建 `FParameters`、分配纹理、调用 `AddPass`。

**2. Material Shader（用户自定义 Blendable）**：

后处理材质使用标准的 UE Material Shader 管线编译。当材质 Domain 设为 Post Process 时，Material Compiler 会生成对应的后处理 Pixel Shader。

在 `AddPostProcessMaterialPass` 中，UE 通过 `FMaterialShaderParameters` 将材质 Shader 与 RDG 集成：

```cpp
FScreenPassTexture AddPostProcessMaterialPass(
    FRDGBuilder& GraphBuilder,
    const FViewInfo& View,
    const FPostProcessMaterialInputs& Inputs,
    const UMaterialInterface* Material)
{
    // 获取材质编译后的 Shader
    const FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
    // ... 设置材质参数、绑定 SceneTexture、执行 DrawScreenPass
}
```

**两种 Shader 的关键差异**：

| 维度 | Global Shader | Material Shader |
|------|---------------|-----------------|
| 编译时机 | 引擎构建时（随引擎发布） | 编辑器中材质保存时 |
| 参数来源 | C++ 硬编码 + RDG 资源 | 材质蓝图节点 + 材质参数 |
| 插入位置 | 固定在后处理链的特定阶段 | 用户通过 BlendableLocation 选择 |
| 性能 | 高度优化，支持 Compute/Async | 受材质复杂度影响，通常为 Pixel Shader |

---

## 关键设计模式总结

### 1. 配置与执行的解耦

`FPostProcessSettings`（数据）与 `AddPostProcessingPasses`（执行）完全解耦。场景编辑器只修改配置，渲染线程在每帧开始时将多个 Volume/Component 的配置混合成 `View.FinalPostProcessSettings`，后处理执行逻辑只读这个最终配置。这让 gameplay 代码可以安全地动态调整后处理，而无需关心 GPU 管线状态。

### 2. Pass 序列的显式编排

UE5 放弃了 UE4 的隐式 Composite Graph 链接方式，改用显式的 `TOverridePassSequence` + 顺序调用 `AddXxxPass`。这让后处理管线变得**可追踪、可调试**——通过 `r.CompositionGraphDebug` 可以在控制台输出完整的 Pass 序列。

### 3. 历史缓冲的"提取-注册"循环

跨帧资源通过 `QueueTextureExtraction` → 池化存储 → `RegisterExternalTexture` 的循环实现 RDG 内外的无缝衔接。这个模式不仅用于 TAA，也用于 SSR、SSGI 等所有需要时域累积的效果。

### 4. Compute vs Pixel Shader 的灵活切换

许多后处理 Pass（如 Tonemap）同时实现了 CS 版本和 PS 版本，根据 `View.bUseComputePasses` 和平台能力动态选择。这让 UE 可以在支持 Async Compute 的平台上把后处理 offload 到计算管线，减少 Graphics Queue 的压力。

---

## 延伸思考

1. **为什么 UE5 的 TSR 不能关闭？** 从源码可见，只要没有显式禁用 TemporalAA，无论抗锯齿方法选什么，`AddPostProcessingPasses` 都会进入 `ITemporalUpscaler::AddPasses`。这是因为在 Nanite + Lumen 的默认管线中，时域累积不仅是抗锯齿手段，也是降噪和细节重建的关键环节。

2. **自定义后处理的最佳插入点在哪里？** 如果你要写的是不需要 HDR 信息的全屏滤镜（如扫描线、复古效果），选 `BL_AfterTonemapping` 性能最好；如果你需要深度或法线信息，选 `BL_BeforeTonemapping`；如果你要替换 UE 的色调映射，使用 `BL_ReplacingTonemapper`。

3. **内存优化的切入点**：Bloom 的下采样链和 TAA 的历史缓冲是最大的两个 GPU 内存消费者。通过调整 `r.Bloom.Quality`、`r.TemporalAA.History.ScreenPercentage` 等 CVar，可以在画面质量和内存占用之间做权衡。
