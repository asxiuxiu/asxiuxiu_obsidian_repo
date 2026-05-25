---
title: Bevy-bevy_core_pipeline-源码解析：后处理与特效
date: 2026-05-25
tags:
  - bevy-source
  - bevy_core_pipeline
  - bevy_post_process
  - bevy_anti_alias
  - 第三阶段-渲染管线
aliases:
  - Bevy-bevy_core_pipeline-源码解析：后处理与特效
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy 全解析主索引]]

# Bevy-bevy_core_pipeline-源码解析：后处理与特效

> **分析范围**：`crates/bevy_core_pipeline/src/tonemapping/`、`crates/bevy_core_pipeline/src/upscaling/`、`crates/bevy_core_pipeline/src/core_3d/mod.rs`、`crates/bevy_core_pipeline/src/schedule.rs`、`crates/bevy_post_process/src/bloom/`、`crates/bevy_post_process/src/lib.rs`、`crates/bevy_anti_alias/src/fxaa/`、`examples/shader_advanced/custom_post_processing.rs` | **分析轮次**：第一轮 + 第二轮 + 第三轮全量 | **源码版本**：Bevy 0.19.0-dev

---

## 零、后处理是什么？为什么需要它？

后处理（Post-Processing）是指在场景的主渲染通道（Main Pass）完成后，对已经生成的图像进行额外处理的一系列操作。常见的后处理效果包括色调映射（Tonemapping）、泛光（Bloom）、抗锯齿（FXAA）、景深（Depth of Field）、运动模糊（Motion Blur）等。

Bevy 的后处理架构有几个关键设计动机：

1. **HDR 管线支持**：现代渲染引擎在内部使用高动态范围（HDR）存储光照和颜色信息（值可以远超 1.0），但最终显示器只能显示标准动态范围（SDR）。因此需要一个从 HDR 到 SDR 的转换过程——这就是色调映射。
2. **效果叠加顺序**：不同后处理效果之间存在严格的依赖顺序。例如 Bloom 需要在 Tonemapping 之前执行（因为 Bloom 需要 HDR 数据），而 FXAA 需要在 Tonemapping 之后执行（因为 FXAA 工作在 LDR 空间）。
3. **可扩展性**：用户需要能够插入自定义后处理 Pass，而不必修改引擎源码。

> **一句话总结**：Bevy 的后处理是一组挂在 `Core3d` Schedule 上、以 `ViewTarget` 双缓冲为载体的全屏图像处理管线。

---

## 一、模块定位与构建定义

### 1.1 后处理相关 Crate 的职责划分

在 Bevy 0.19.0-dev 中，后处理相关代码已拆分到多个独立 crate 中：

| Crate | 职责 | 关键模块 |
|-------|------|---------|
| `bevy_core_pipeline` | 核心 2D/3D 渲染管线、Tonemapping、Upscaling | `core_3d/`, `tonemapping/`, `upscaling/` |
| `bevy_post_process` | Bloom、Motion Blur、DOF、Auto Exposure、Effect Stack | `bloom/`, `motion_blur/`, `dof/`, `auto_exposure/`, `effect_stack/` |
| `bevy_anti_alias` | 各类抗锯齿方案 | `fxaa/`, `smaa/`, `taa/`, `dlss/`, `contrast_adaptive_sharpening/` |

> 文件：`crates/bevy_core_pipeline/Cargo.toml`

`bevy_core_pipeline` 依赖 `bevy_render`（渲染核心）、`bevy_camera`（相机抽象）、`bevy_shader`（着色器）等。它自身不提供 Bloom 等效果，而是通过 Plugin 组合的方式将 `bevy_post_process` 和 `bevy_anti_alias` 的插件接入主 App。

### 1.2 关键文件职责表

| 文件 | 行数 | 职责 |
|------|------|------|
| `crates/bevy_core_pipeline/src/core_3d/mod.rs` | ~1022 | `Core3dPlugin`：注册 `Core3d` Schedule、Phase 提取、后处理 System 编排 |
| `crates/bevy_core_pipeline/src/schedule.rs` | ~295 | `Core3d` / `Core2d` Schedule 定义、`camera_driver` 系统 |
| `crates/bevy_core_pipeline/src/tonemapping/mod.rs` | ~464 | `TonemappingPlugin`、色调映射算法枚举、LUT 资源管理、Pipeline Specialization |
| `crates/bevy_core_pipeline/src/tonemapping/node.rs` | ~135 | `tonemapping` 系统：执行全屏色调映射 Pass |
| `crates/bevy_core_pipeline/src/upscaling/mod.rs` | ~108 | `UpscalingPlugin`、Blit Pipeline 准备 |
| `crates/bevy_core_pipeline/src/upscaling/node.rs` | ~104 | `upscaling` 系统：将最终图像 Blit 到交换链 |
| `crates/bevy_post_process/src/bloom/mod.rs` | ~501 | `BloomPlugin`、Bloom 执行逻辑（Down/Upsample 链） |
| `crates/bevy_post_process/src/bloom/settings.rs` | ~267 | `Bloom` Component 定义、参数、ExtractComponent 实现 |
| `crates/bevy_post_process/src/bloom/downsampling_pipeline.rs` | ~184 | Bloom Downsample Pipeline Specialization |
| `crates/bevy_post_process/src/bloom/upsampling_pipeline.rs` | ~163 | Bloom Upsample Pipeline Specialization、Blend Mode 配置 |
| `crates/bevy_post_process/src/bloom/bloom.wgsl` | ~183 | Bloom Down/Upsample 的 WGSL 实现 |
| `crates/bevy_anti_alias/src/fxaa/mod.rs` | ~216 | `FxaaPlugin`、FXAA Pipeline Specialization |
| `crates/bevy_anti_alias/src/fxaa/node.rs` | ~77 | `fxaa` 系统：执行 FXAA 全屏 Pass |
| `examples/shader_advanced/custom_post_processing.rs` | ~303 | 官方自定义后处理示例 |

---

## 二、第一轮：接口层（What）

### 2.1 Core3d Schedule 的编排结构

Bevy 的后处理不再使用传统的 `RenderGraph` 字符串节点（Bevy 0.13 之前的方式），而是使用 **类型安全的 Schedule 编排**。每个相机对应一个 `Core3d` Schedule 的执行实例。

> 文件：`crates/bevy_core_pipeline/src/schedule.rs`，第 34~69 行

```rust
#[derive(ScheduleLabel, Debug, Clone, PartialEq, Eq, Hash, Default)]
pub struct Core3d;

#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub enum Core3dSystems {
    Prepass,
    MainPass,
    EarlyPostProcess,
    PostProcess,
}

impl Core3d {
    pub fn base_schedule() -> Schedule {
        let mut schedule = Schedule::new(Self);
        schedule.configure_sets((Prepass, MainPass, EarlyPostProcess, PostProcess).chain());
        schedule
    }
}
```

四个阶段按 **严格顺序链式执行**：`Prepass` → `MainPass` → `EarlyPostProcess` → `PostProcess`。后处理效果通过 `.in_set(Core3dSystems::PostProcess)` 注册到 `PostProcess` 阶段，并通过 `.before(tonemapping)` / `.after(tonemapping)` 控制子顺序。

### 2.2 Core3dPlugin 中的后处理注册

> 文件：`crates/bevy_core_pipeline/src/core_3d/mod.rs`，第 100~165 行

```rust
impl Plugin for Core3dPlugin {
    fn build(&self, app: &mut App) {
        app.register_required_components_with::<Camera3d, DebandDither>(|| DebandDither::Enabled)
            .register_required_components_with::<Camera3d, CameraRenderGraph>(|| {
                CameraRenderGraph::new(Core3d)
            })
            .register_required_components::<Camera3d, Tonemapping>()
            // ...
            .add_schedule(Core3d::base_schedule())
            .add_systems(
                Core3d,
                (
                    (/* prepass systems */).chain().in_set(Core3dSystems::Prepass),
                    (main_opaque_pass_3d, main_transparent_pass_3d)
                        .chain()
                        .in_set(Core3dSystems::MainPass),
                    tonemapping.in_set(Core3dSystems::PostProcess),
                    upscaling.after(Core3dSystems::PostProcess),
                ),
            );
    }
}
```

关键观察：
- `Camera3d` 自动要求 `Tonemapping` 和 `DebandDither` 组件——这意味着所有 3D 相机默认启用色调映射和去色带抖动。
- `tonemapping` 系统注册在 `PostProcess` 阶段；`upscaling` 注册在 `PostProcess` 之后。

### 2.3 后处理效果的依赖顺序

各后处理插件通过 `before` / `after` 建立如下执行顺序（在 `Core3dSystems::PostProcess` 内）：

```
EarlyPostProcess
  ↓
MainPass (Opaque + Transparent)
  ↓
PostProcess:
  - bloom (before tonemapping)
  - tonemapping
  - fxaa (after tonemapping)
  - 用户自定义后处理 (可插在 tonemapping 后、upscaling 前)
  ↓
upscaling (after PostProcess)
```

> 文件：`crates/bevy_post_process/src/bloom/mod.rs`，第 77~84 行

```rust
.add_systems(
    Core3d,
    bloom.before(tonemapping).in_set(Core3dSystems::PostProcess),
)
.add_systems(
    Core2d,
    bloom.before(tonemapping).in_set(Core2dSystems::PostProcess),
);
```

> 文件：`crates/bevy_anti_alias/src/fxaa/mod.rs`，第 102~109 行

```rust
.add_systems(
    Core3d,
    fxaa.after(tonemapping).in_set(Core3dSystems::PostProcess),
)
.add_systems(
    Core2d,
    fxaa.after(tonemapping).in_set(Core2dSystems::PostProcess),
);
```

### 2.4 公共接口：Component 驱动的后处理开关

Bevy 的后处理采用 **Component 驱动** 的接口设计。用户只需在相机实体上插入/移除 Component 即可开关效果：

```rust
// 开启 Bloom
commands.spawn((
    Camera3d::default(),
    Bloom::default(),        // 自动 require Hdr
    Tonemapping::TonyMcMapface,
));

// 开启 FXAA
commands.spawn((
    Camera3d::default(),
    Fxaa::default(),
));
```

---

## 三、第二轮：数据层（How - Structure）

### 3.1 ViewTarget：后处理的纹理载体

后处理效果的本质是**对纹理的逐像素操作**。Bevy 使用 `ViewTarget` 作为每个 View（相机）的渲染目标管理器，其核心机制是**双缓冲翻转（Ping-Pong）**。

> 文件：`crates/bevy_render/src/view/mod.rs`，第 933~955 行

```rust
pub fn post_process_write(&self) -> PostProcessWrite<'_> {
    let old_is_a_main_texture = self.main_texture.fetch_xor(1, Ordering::SeqCst);
    if old_is_a_main_texture == 0 {
        self.main_textures.b.mark_as_cleared();
        PostProcessWrite {
            source: &self.main_textures.a.texture.default_view,
            destination: &self.main_textures.b.texture.default_view,
            // ...
        }
    } else {
        self.main_textures.a.mark_as_cleared();
        PostProcessWrite {
            source: &self.main_textures.b.texture.default_view,
            destination: &self.main_textures.a.texture.default_view,
            // ...
        }
    }
}
```

`PostProcessWrite` 提供 `source`（当前帧图像）和 `destination`（写入目标）。每次调用 `post_process_write()` 都会**原子翻转**主纹理指针。后处理系统必须遵守一条铁律：**必须将 `source` 写入 `destination`**，否则数据会丢失。

```rust
pub struct PostProcessWrite<'a> {
    pub source: &'a TextureView,
    pub destination: &'a TextureView,
    pub source_texture: &'a Texture,
    pub destination_texture: &'a Texture,
}
```

### 3.2 HDR 中间纹理格式

`Hdr` 是一个 marker component，表示相机使用 HDR 中间纹理：

> 文件：`crates/bevy_camera/src/components.rs`，第 83~89 行

```rust
/// If this component is added to a camera, the camera will use an intermediate "high dynamic range" render texture.
#[derive(Component, Default, Copy, Clone, Reflect, PartialEq, Eq, Hash, Debug)]
pub struct Hdr;
```

Bloom 的 Component 定义上有 `#[require(Hdr)]`：

> 文件：`crates/bevy_post_process/src/bloom/settings.rs`，第 30~33 行

```rust
#[derive(Component, Reflect, Clone)]
#[reflect(Component, Default, Clone)]
#[require(Hdr)]
pub struct Bloom { /* ... */ }
```

这意味着：用户给相机添加 `Bloom` 时，ECS 会自动添加 `Hdr`。`Hdr` 本身不是 bool 字段，而是一个**存在性组件（Marker Component）**——这是 Bevy ECS 风格的典型设计。

### 3.3 Tonemapping：Component 提取到渲染世界

`Tonemapping` 和 `DebandDither` 都是主世界的 Component，通过 `ExtractComponentPlugin` 每帧提取到渲染世界：

> 文件：`crates/bevy_core_pipeline/src/tonemapping/mod.rs`，第 86~90 行

```rust
app.add_plugins((
    ExtractComponentPlugin::<Tonemapping>::default(),
    ExtractComponentPlugin::<DebandDither>::default(),
));
```

`Tonemapping` 是一个枚举，包含多种色调映射算法：

```rust
pub enum Tonemapping {
    None,
    Reinhard,
    ReinhardLuminance,
    AcesFitted,
    AgX,                        // 需要 tonemapping_luts feature
    SomewhatBoringDisplayTransform,
    #[default]
    TonyMcMapface,              // Bevy 默认，需要 tonemapping_luts feature
    BlenderFilmic,              // 需要 tonemapping_luts feature
    PbrNeutral,
}
```

部分算法（AgX、TonyMcMapface、BlenderFilmic）依赖 3D LUT 纹理。这些 LUT 在 `TonemappingPlugin::build` 中被加载为 `TonemappingLuts` 资源，并通过 `ExtractResourcePlugin` 提取到渲染世界。

> 文件：`crates/bevy_core_pipeline/src/tonemapping/mod.rs`，第 35~82 行

### 3.4 Bloom 数据结构

Bloom 的核心数据结构关系：

```
Bloom (Component, 主世界)
  ├── intensity: f32
  ├── low_frequency_boost: f32
  ├── low_frequency_boost_curvature: f32
  ├── high_pass_frequency: f32
  ├── prefilter: BloomPrefilter
  ├── composite_mode: BloomCompositeMode
  ├── max_mip_dimension: u32
  └── scale: Vec2

ExtractComponent for Bloom
  └── 输出: (Bloom, BloomUniforms)
       └── BloomUniforms (Component, 渲染世界)
            ├── threshold_precomputations: Vec4
            ├── viewport: Vec4
            ├── scale: Vec2
            └── aspect: f32

BloomTexture (Component, 渲染世界)
  ├── texture: CachedTexture (或 Vec<CachedTexture> for WebGL)
  └── mip_count: u32

BloomBindGroups (Component, 渲染世界)
  ├── cache_key: (TextureId, BufferId)
  ├── downsampling_bind_groups: Box<[BindGroup]>
  ├── upsampling_bind_groups: Box<[BindGroup]>
  └── sampler: Sampler

BloomDownsamplingPipelineIds (Component, 渲染世界)
  ├── main: CachedRenderPipelineId
  └── first: CachedRenderPipelineId

UpsamplingPipelineIds (Component, 渲染世界)
  ├── id_main: CachedRenderPipelineId  (写入 Bloom 纹理)
  └── id_final: CachedRenderPipelineId (写入 ViewTarget)
```

---

## 四、第三轮：逻辑层（How - Behavior）

### 4.1 Bloom 执行流程：Downsample → Upsample → Composite

> 文件：`crates/bevy_post_process/src/bloom/mod.rs`，第 88~275 行

Bloom 的核心执行函数 `bloom` 是一个渲染世界中的系统（system），其流程如下：

```rust
pub fn bloom(
    view: ViewQuery<( /* ExtractedCamera, ViewTarget, BloomTexture, ... */ )>,
    downsampling_pipeline_res: Res<BloomDownsamplingPipeline>,
    pipeline_cache: Res<PipelineCache>,
    uniforms: Res<ComponentUniforms<BloomUniforms>>,
    mut ctx: RenderContext,
) {
    // 1. 如果 intensity == 0 或相机不是 HDR，直接跳过
    if bloom_settings.intensity == 0.0 || !camera.hdr {
        return;
    }

    // 2. 获取已编译好的 Pipeline 和 Uniform 绑定
    let uniforms_binding = uniforms.binding();
    let downsampling_first_pipeline = pipeline_cache.get_render_pipeline(downsampling_pipeline_ids.first);
    // ...

    // 3. 第一次 Downsample：从主纹理读取，写入 Bloom mip 0
    {
        let mut pass = command_encoder.begin_render_pass(/* ... */);
        pass.set_pipeline(downsampling_first_pipeline);
        pass.set_bind_group(0, &downsampling_first_bind_group, &[uniform_index.index()]);
        pass.draw(0..3, 0..1); // 全屏三角形
    }

    // 4. 剩余 Downsample 链：mip 1 .. mip_count
    for mip in 1..bloom_texture.mip_count {
        let mut pass = command_encoder.begin_render_pass(/* 写入 bloom_texture.view(mip) */);
        pass.set_pipeline(downsampling_pipeline);
        pass.set_bind_group(0, &bind_groups.downsampling_bind_groups[mip - 1], /* ... */);
        pass.draw(0..3, 0..1);
    }

    // 5. Upsample 链（非最终）：从最小 mip 向上混合
    for mip in (1..bloom_texture.mip_count).rev() {
        let blend = compute_blend_factor(bloom_settings, mip as f32, max_mip);
        pass.set_blend_constant(LinearRgba::gray(blend).into());
        pass.draw(0..3, 0..1);
    }

    // 6. 最终 Upsample：将 Bloom 结果混合回 ViewTarget 主纹理
    {
        let mut pass = command_encoder.begin_render_pass(/* 写入 view_target unsampled */);
        pass.set_pipeline(upsampling_final_pipeline);
        pass.set_blend_constant(/* ... */);
        pass.draw(0..3, 0..1);
    }
}
```

**关键决策点**：
- `camera.hdr` 检查：如果相机没有 `Hdr` component（或 `camera.hdr == false`），Bloom 直接返回。这是因为 Bloom 需要 HDR 数据才能正确识别"过亮"区域。
- `intensity == 0.0` 检查：提供零开销关闭路径。
- WebGL 回退：在 WebGL2 环境下（不支持绑定特定 mip level），Bloom 使用 `Vec<CachedTexture>` 而非单纹理 mipchain。

### 4.2 Bloom 的 Downsample Shader

> 文件：`crates/bevy_post_process/src/bloom/bloom.wgsl`，第 1~127 行

Bloom 的 downsample 使用 **13-tap 采样核**，参考了 Call of Duty: Advanced Warfare 的实现：

```wgsl
fn sample_input_13_tap(uv: vec2<f32>) -> vec3<f32> {
    // 13 个采样点，分为 5 组
    var group0 = (a + b + d + e) * (0.125f / 4.0f);
    var group1 = (b + c + e + f) * (0.125f / 4.0f);
    var group2 = (d + e + g + h) * (0.125f / 4.0f);
    var group3 = (e + f + h + i) * (0.125f / 4.0f);
    var group4 = (j + k + l + m) * (0.5f / 4.0f);

#ifdef FIRST_DOWNSAMPLE
    // Firefly reduction: Karis average
    group0 *= karis_average(group0);
    group1 *= karis_average(group1);
    // ...
    return group0 + group1 + group2 + group3 + group4;
#else
    // 普通 downsample
    var sample = (a + c + g + i) * 0.03125;
    sample += (b + d + f + h) * 0.0625;
    sample += (e + j + k + l + m) * 0.125;
    return sample;
#endif
}
```

**首次 downsample** 使用 `karis_average` 进行 firefly reduction（抑制过亮单像素），这是从 Brian Karis 的文章中借鉴的技术。亮度计算基于 Rec. 709 系数。

### 4.3 Bloom 的 Upsample Shader

> 文件：`crates/bevy_post_process/src/bloom/bloom.wgsl`，第 129~154 行

Upsample 使用 **3x3 tent filter**：

```wgsl
fn sample_input_3x3_tent(uv: vec2<f32>) -> vec3<f32> {
    let frag_size = uniforms.scale / vec2<f32>(textureDimensions(input_texture));
    // 9 个采样点
    var sample = e * 0.25;
    sample += (b + d + f + h) * 0.125;
    sample += (a + c + g + i) * 0.0625;
    return sample;
}
```

### 4.4 Bloom 的 Blend 策略

Bloom 的混合不是简单的叠加，而是通过 `compute_blend_factor` 计算逐 mip 的混合系数：

> 文件：`crates/bevy_post_process/src/bloom/mod.rs`，第 486~501 行

```rust
fn compute_blend_factor(bloom: &Bloom, mip: f32, max_mip: f32) -> f32 {
    let mut lf_boost =
        (1.0 - ops::powf(1.0 - (mip / max_mip), 1.0 / (1.0 - bloom.low_frequency_boost_curvature)))
        * bloom.low_frequency_boost;
    let high_pass_lq = 1.0
        - (((mip / max_mip) - bloom.high_pass_frequency) / bloom.high_pass_frequency)
            .clamp(0.0, 1.0);
    lf_boost *= match bloom.composite_mode {
        BloomCompositeMode::EnergyConserving => 1.0 - bloom.intensity,
        BloomCompositeMode::Additive => 1.0,
    };
    (bloom.intensity + lf_boost) * high_pass_lq
}
```

这个函数在 CPU 上计算，然后通过 `set_blend_constant` 传递给 GPU。混合模式有两种：

| 模式 | Blend 公式 | 物理正确性 | 视觉效果 |
|------|-----------|-----------|---------|
| `EnergyConserving` | `Src * Constant + Dst * (1 - Constant)` | ✅ 能量守恒 | 自然、柔和 |
| `Additive` | `Src * Constant + Dst * 1` | ❌ 能量不守恒 | 明亮、发光感强 |

> 文件：`crates/bevy_post_process/src/bloom/upsampling_pipeline.rs`，第 74~104 行

---

## 五、自定义后处理流程

### 5.1 官方示例分析

Bevy 官方提供了 `custom_post_processing.rs` 示例，展示了插入自定义后处理的完整流程：

> 文件：`examples/shader_advanced/custom_post_processing.rs`

**五步标准流程**：

```
1. 定义 Settings Component（主世界）
   └── 实现 ExtractComponent trait（自动由 ExtractComponentPlugin 处理）

2. 定义 Pipeline Resource（渲染世界）
   └── 在 Plugin::build 的 RenderApp 中初始化

3. 实现渲染系统（ViewQuery + RenderContext）
   └── 使用 ViewTarget::post_process_write() 获取 source/destination
   └── 创建 BindGroup、开始 RenderPass、绘制全屏三角形

4. 注册到 Core3d Schedule
   └── render_app.add_systems(Core3d, post_process_system.in_set(Core3dSystems::PostProcess));

5. 编写 WGSL Shader
   └── 全屏三角形顶点由 FullscreenShader 提供
   └── 片元着色器接收 source texture 和 uniforms
```

### 5.2 关键代码：自定义后处理系统

```rust
fn post_process_system(
    view: ViewQuery<(&ViewTarget, &PostProcessSettings, &DynamicUniformIndex<PostProcessSettings>)>,
    post_process_pipeline: Option<Res<PostProcessPipeline>>,
    pipeline_cache: Res<PipelineCache>,
    settings_uniforms: Res<ComponentUniforms<PostProcessSettings>>,
    mut cache: Local<PostProcessBindGroupCache>,
    mut ctx: RenderContext,
) {
    let (view_target, _settings, settings_index) = view.into_inner();
    let post_process = view_target.post_process_write(); // 翻转双缓冲

    let bind_group = ctx.render_device().create_bind_group(
        "post_process_bind_group",
        &pipeline_cache.get_bind_group_layout(&post_process_pipeline.layout),
        &BindGroupEntries::sequential((
            post_process.source,           // 读取源纹理
            &post_process_pipeline.sampler,
            settings_binding.clone(),      //  uniform 数据
        )),
    );

    let mut render_pass = ctx.command_encoder().begin_render_pass(&RenderPassDescriptor {
        color_attachments: &[Some(RenderPassColorAttachment {
            view: post_process.destination, // 必须写入目标纹理
            ops: Operations::default(),
        })],
        // ...
    });
    render_pass.set_pipeline(pipeline);
    render_pass.set_bind_group(0, &bind_group, &[settings_index.index()]);
    render_pass.draw(0..3, 0..1); // 全屏三角形
}
```

**重要约束**：
- `post_process_write()` 必须在每帧调用，它会内部翻转 `ViewTarget` 的双缓冲。如果不调用或忘记写入 `destination`，上一帧的数据会丢失。
- BindGroup 必须在 `post_process_write()` 之后创建，因为 `source` 和 `destination` 每帧会交换。

---

## 六、设计决策分析

### 6.1 设计决策：使用 Schedule 编排替代 RenderGraph 字符串节点

**问题背景**：Bevy 0.12 及之前使用字符串标识的 RenderGraph 节点（如 `"tonemapping"`、`"bloom"`），容易因拼写错误或顺序约束不足导致后处理节点顺序混乱。

**naive 方案**：继续使用字符串节点，增加更多人工文档说明。
- 缺点：无编译期检查；重构时容易遗漏；自定义插件插入顺序容易出错。

**实际方案**（Bevy 0.13+）：使用类型安全的 `ScheduleLabel` + `SystemSet` + `before`/`after` 编排。
- 优点：Rust 编译器保证顺序约束的合法性；`Core3dSystems::PostProcess` 提供统一的阶段上下文；自定义后处理只需 `.in_set(Core3dSystems::PostProcess)` 即可融入。
- 代价：相比传统 RenderGraph 的节点可视化，Schedule 的依赖关系需要阅读代码才能理解。

### 6.2 设计决策：Component 驱动的后处理开关

**问题背景**：如何让用户方便地开关后处理效果，并确保效果只在需要的相机上运行？

**naive 方案**：全局 Resource 控制所有相机的后处理。
- 缺点：多相机场景下无法独立控制；不同相机需要不同后处理配置时难以扩展。

**实际方案**：每个后处理效果是独立的 Component，附加在相机实体上。渲染世界的系统通过 `ViewQuery` 只处理带有对应 Component 的 View。
- 优点：每个相机独立配置；天然支持 ECS 的添加/移除语义；`ExtractComponent` 自动处理主世界到渲染世界的同步。
- 代价：每个效果需要独立的 Extract/Prepare/Queue 系统，有一定的样板代码。

### 6.3 设计决策：Bloom 使用 Downsample/Upsample Mipchain 而非 Kawase Blur

**问题背景**：Bloom 需要一种高效的模糊算法。Kawase Blur（多次小半径模糊）和 Mipchain Downsample/Upsample 都是常用方案。

**实际方案**：Bevy 采用了基于 mipchain 的 downsample + upsample 方案，参考了 Call of Duty: Advanced Warfare 的 HDR Bloom 实现。
- 优点：每次 downsample 分辨率减半，计算量随 mip 指数下降；`textureSample` 配合线性过滤天然实现 2x2 box filter；upsample 的 tent filter 提供高质量的重建；整体性能开销可控。
- 代价：需要分配带 mipchain 的 HDR 中间纹理（`Rg11b10Ufloat` 格式），占用一定显存；非各向同性的 bloom（`scale != Vec2::ONE`）需要动态计算 UV 偏移，走慢路径。

---

## 七、关联辐射（Context）

### 7.1 与上层模块的关系

- **bevy_render**：`ViewTarget`（双缓冲纹理管理）、`ExtractComponent` / `ExtractResource`（数据提取机制）、`RenderContext`（CommandEncoder 封装）、`FullscreenShader`（全屏三角形）都是后处理的基础设施。
- **bevy_camera**：`Camera`、`Hdr`、`CameraRenderGraph` 定义了后处理的作用对象和渲染图选择。
- **bevy_pbr**：PBR 材质的高光/自发光（`StandardMaterial::emissive`）是 Bloom 的主要来源。

### 7.2 与下层模块的关系

- **wgpu**：所有后处理最终都翻译为 `wgpu::RenderPass` 和 `wgpu::BindGroup`。
- **naga**：WGSL 着色器通过 naga 编译为 GPU 字节码。

### 7.3 跨引擎对照

| 特性 | Bevy | Unreal Engine |
|------|------|---------------|
| 后处理开关方式 | Component（`Bloom`、`Fxaa`） | Post Process Volume + Camera Settings |
| 编排机制 | ECS Schedule (`Core3dSystems::PostProcess`) | Render Graph Node |
| Bloom 算法 | Downsample/Upsample mipchain + tent filter | Dual Kawase + Convolution |
| ToneMapping | 全屏 Pass，支持 9 种算法 | 全屏 Pass，ACES、Filmic 等 |
| 自定义后处理 | Plugin + System + WGSL | Post Process Material / Custom HLSL |

### 7.4 设计亮点总结

1. **Ping-Pong 双缓冲**：`ViewTarget::post_process_write()` 提供简洁、零开销的双缓冲机制，避免后处理链中每步都分配新纹理。
2. **Pipeline Specialization**：`SpecializedRenderPipeline` 允许根据 Component 参数（如 `BloomCompositeMode`、`Fxaa::edge_threshold`）动态生成专用管线，兼顾灵活性和性能。
3. **WebGL 回退**：Bloom 在 WebGL2 下自动降级为 `Vec<CachedTexture>`，保证跨平台兼容性。

---

## 八、关键源码片段

### 8.1 Core3d Schedule 配置

> 文件：`crates/bevy_core_pipeline/src/core_3d/mod.rs`，第 145~163 行

```rust
.add_systems(
    Core3d,
    (
        (early_prepass, early_deferred_prepass, late_prepass, late_deferred_prepass, copy_deferred_lighting_id)
            .chain()
            .in_set(Core3dSystems::Prepass),
        (main_opaque_pass_3d, main_transparent_pass_3d)
            .chain()
            .in_set(Core3dSystems::MainPass),
        tonemapping.in_set(Core3dSystems::PostProcess),
        upscaling.after(Core3dSystems::PostProcess),
    ),
);
```

### 8.2 Bloom 的 Downsample + Upsample 循环

> 文件：`crates/bevy_post_process/src/bloom/mod.rs`，第 157~271 行

```rust
// First downsample pass (reads from main texture)
{
    let mut pass = command_encoder.begin_render_pass(/* bloom_texture.view(0) */);
    pass.set_pipeline(downsampling_first_pipeline);
    pass.draw(0..3, 0..1);
}

// Other downsample passes
for mip in 1..bloom_texture.mip_count {
    let mut pass = command_encoder.begin_render_pass(/* bloom_texture.view(mip) */);
    pass.set_pipeline(downsampling_pipeline);
    pass.draw(0..3, 0..1);
}

// Upsample passes except final
for mip in (1..bloom_texture.mip_count).rev() {
    let mut pass = command_encoder.begin_render_pass(/* bloom_texture.view(mip-1), LoadOp::Load */);
    pass.set_blend_constant(LinearRgba::gray(blend).into());
    pass.draw(0..3, 0..1);
}

// Final upsample pass (writes to ViewTarget)
{
    let mut pass = command_encoder.begin_render_pass(/* view_texture_unsampled */);
    pass.set_pipeline(upsampling_final_pipeline);
    pass.draw(0..3, 0..1);
}
```

### 8.3 Tonemapping 的条件跳过

> 文件：`crates/bevy_core_pipeline/src/tonemapping/node.rs`，第 46~52 行

```rust
if *tonemapping == Tonemapping::None {
    return;
}
if !camera.hdr {
    return;
}
```

---

## 九、关联阅读

- [[Bevy-bevy_render-源码解析：RenderApp 与提取阶段]] — 理解主世界到渲染世界的数据提取机制
- [[Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动]] — 理解 `camera_driver` 和 Schedule 执行
- [[Bevy-bevy_render-源码解析：View 与 Camera]] — 理解 `ViewTarget`、`ExtractedCamera` 的生命周期
- [[Bevy-bevy_render-源码解析：Texture 与 BindGroup]] — 理解 `CachedTexture`、`BindGroup` 的创建和管理
- [[Bevy-bevy_pbr-源码解析：PBR 材质与光照模型]] — 理解 Bloom 的光源（自发光材质）

---

## 十、索引状态

- **所属阶段**：第三阶段-渲染管线
- **对应索引条目**：`[[Bevy-bevy_core_pipeline-源码解析：后处理与特效]]`
- **分析轮次**：第一轮 + 第二轮 + 第三轮全量
- **覆盖范围**：
  - ✅ `bevy_core_pipeline` 后处理模块结构（tonemapping、upscaling、core_3d）
  - ✅ `bevy_post_process` 后处理模块结构（bloom、dof、motion_blur、auto_exposure、effect_stack）
  - ✅ `bevy_anti_alias` 模块结构（fxaa、smaa、taa）
  - ✅ Core3d Schedule 后处理 Node 编排（Prepass → MainPass → PostProcess → Upscaling）
  - ✅ 自定义后处理流程（Plugin + System + WGSL）
  - ✅ HDR 与 ToneMapping 架构（Hdr component、ViewTarget 双缓冲、ExtractComponent）
  - ✅ Bloom 算法细节（Downsample/Upsample 链、Karis average、parametric curve、Blend Mode）
