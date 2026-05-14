---
title: Bevy-bevy_render-源码解析：View 与 Camera
date: 2026-05-14
tags:
  - Bevy
  - 渲染管线
  - View
  - Camera
  - 源码解析
aliases:
  - ExtractedView
  - ExtractedCamera
  - ViewTarget
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy 全解析主索引]]

## 零、What and Why

在 Bevy 的渲染架构中，**View** 是一个极其核心却又容易被忽视的概念。它不只代表"摄像机"——任何需要从某个视点渲染场景的实体都可以是一个 view，包括 directional light 的 shadow cascade、point light 的 cubemap face、甚至反射探针。`bevy_render` 的 `view` 模块和 `camera` 模块共同负责：把 main world 的 `Camera` 组件提取到 render world，为每个 view 准备好 uniform、target texture、viewport、可见性列表等全部渲染上下文。

理解 View 系统，就是理解 Bevy 如何处理**多视口、多相机、多渲染目标**这些现代引擎的基本需求。本文从提取（Extract）→准备（Prepare）→渲染（Render）的时序出发，逐层拆解 `ExtractedView`、`ExtractedCamera`、`ViewTarget`、`ViewUniform` 以及可见性系统的数据流。

---

## 一、Module 定位与构建定义

| 文件 | 职责 | 关键导出 |
|------|------|----------|
| `src/view/mod.rs` | View 核心定义：`ViewPlugin`、`ExtractedView`、`ViewTarget`、`ViewUniform`、`Msaa`、颜色分级、view target / attachment / uniform 准备系统 | `ViewPlugin`, `ExtractedView`, `ViewTarget`, `ViewUniforms`, `Msaa`, `RetainedViewEntity` |
| `src/view/visibility/mod.rs` | Render world 可见性系统：`RenderVisibleEntities`、`RenderExtractedVisibleEntities`、CPU culled 实体收集 | `RenderVisibleEntities`, `RenderExtractedVisibleEntities`, `collect_visible_cpu_culled_entities` |
| `src/view/window/mod.rs` | Window 渲染插件：窗口提取、surface 创建、swapchain 获取、present | `WindowRenderPlugin`, `ExtractedWindow`, `WindowSurfaces`, `prepare_windows`, `create_surfaces` |
| `src/camera.rs` | Camera 提取与排序：`CameraPlugin`、`ExtractedCamera`、`extract_cameras`、`sort_cameras`、TemporalJitter、MipBias、DirtySpecializations | `CameraPlugin`, `ExtractedCamera`, `extract_cameras`, `sort_cameras`, `TemporalJitter`, `MipBias`, `DirtySpecializations` |

---

## 二、第一轮：接口层

### 2.1 ExtractedView —— Render World 中的视图描述

```rust
// src/view/mod.rs:325-384
#[derive(Component)]
pub struct ExtractedView {
    pub retained_view_entity: RetainedViewEntity,
    pub clip_from_view: Mat4,
    pub world_from_view: GlobalTransform,
    pub clip_from_world: Option<Mat4>,
    pub target_format: TextureFormat,
    pub viewport: UVec4,
    pub color_grading: ColorGrading,
    pub invert_culling: bool,
}
```

- **`retained_view_entity`**：跨帧稳定的 view 标识。因为 render world 的 `Entity` 每帧可能变化，Bevy 用 `RetainedViewEntity { main_entity, auxiliary_entity, subview_index }` 来唯一标识一个 view。例如，一个 point light 的 shadow map 有 6 个 face，每个 face 就是一个 subview。
- **`clip_from_view`**：投影矩阵。Bevy 使用 reverse-Z infinite perspective（`near` 在 `clip_from_view[3][2]`，`clip_from_view[2][2] == 0`）。
- **`world_from_view`**：view 的世界变换，即 camera 的 `GlobalTransform`。
- **`clip_from_world`**：可选的预计算 view-projection 矩阵。如果为 `None`，渲染时会用 `clip_from_view * view_from_world` 计算。
- **`target_format`**：该 view 渲染目标的颜色格式。HDR 会强制设为 `Rgba16Float`。
- **`viewport`**：`uvec4(origin.x, origin.y, width, height)`，定义该 view 在 target 上的绘制区域。
- **`invert_culling`**：用于镜像相机（如水面反射），翻转背面剔除方向。

### 2.2 ExtractedCamera —— 从 Camera 提取的渲染专用数据

```rust
// src/camera.rs:453-471
#[derive(Component, Debug)]
#[require(RenderVisibleEntities)]
pub struct ExtractedCamera {
    pub target: Option<NormalizedRenderTarget>,
    pub physical_viewport_size: Option<UVec2>,
    pub physical_target_size: Option<UVec2>,
    pub viewport: Option<Viewport>,
    pub schedule: InternedScheduleLabel,
    pub order: isize,
    pub output_mode: CameraOutputMode,
    pub msaa_writeback: MsaaWriteback,
    pub clear_color: ClearColorConfig,
    pub sorted_camera_index_for_target: usize,
    pub exposure: f32,
    pub hdr: bool,
    pub compositing_space: Option<CompositingSpace>,
}
```

与 `ExtractedView` 不同，`ExtractedCamera` 只存在于真正的 camera 实体上（light 的 shadow view 只有 `ExtractedView` 而没有 `ExtractedCamera`）。它携带了与输出、清除、后处理相关的配置。

- **`target`**：归一化后的渲染目标，可以是 window、image asset 或自定义 texture view。
- **`schedule`**：该 camera 使用的 render graph schedule label。`Camera2d` 和 `Camera3d` 会绑定到不同的 schedule。
- **`order`**：相机渲染优先级。负数先渲染，正数后渲染。同 `order` 同 `target` 的相机会触发 ambiguity warning。
- **`sorted_camera_index_for_target`**：同一 target 上多个 camera 的序号，用于 MSAA resolve 和 post-process 链中的纹理索引。

### 2.3 RetainedViewEntity —— 跨帧稳定的 View ID

```rust
// src/view/mod.rs:274-296
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct RetainedViewEntity {
    pub main_entity: MainEntity,
    pub auxiliary_entity: MainEntity,
    pub subview_index: u32,
}
```

设计动机：render world 的 `Entity` 不稳定（每帧重新提取），`MainEntity` 虽然稳定但不够用（一个 main world 实体可能对应多个 render world view）。因此引入 `subview_index`：
- 普通 camera：`subview_index = 0`
- Directional light shadow cascade：`subview_index = cascade_index`
- Point light shadow cubemap：`subview_index = face_index (0..5)`

### 2.4 ViewTarget —— 双缓冲渲染目标

```rust
// src/view/mod.rs:683-694
#[derive(Component, Clone)]
pub struct ViewTarget {
    main_textures: MainTargetTextures,
    main_texture_format: TextureFormat,
    main_texture: Arc<AtomicUsize>,
    out_texture: Option<OutputColorAttachment>,
    pub compositing_space: Option<CompositingSpace>,
}
```

- **`main_textures`**：包含 A、B 两个 `ColorAttachment` 的双缓冲结构。后处理 pass 通过 `post_process_write()` 翻转当前读写目标。
- **`main_texture`**：`AtomicUsize` 表示当前活跃的是 A（0）还是 B（1）。多个共享同一 `NormalizedRenderTarget` 的 view 会共享这个 atomic，确保 post-process 链的连续性。
- **`out_texture`**：最终输出 attachment。如果是 window，这就是 swapchain texture view；如果是 `Image` asset，就是对应的 `GpuImage` texture view。

### 2.5 ViewUniform —— 传给 Shader 的视图常量

```rust
// src/view/mod.rs:605-657
#[derive(Clone, ShaderType)]
pub struct ViewUniform {
    pub clip_from_world: Mat4,
    pub unjittered_clip_from_world: Mat4,
    pub world_from_clip: Mat4,
    pub world_from_view: Mat4,
    pub view_from_world: Mat4,
    pub clip_from_view: Mat4,
    pub view_from_clip: Mat4,
    pub world_position: Vec3,
    pub exposure: f32,
    pub viewport: Vec4,
    pub main_pass_viewport: Vec4,
    pub frustum: [Vec4; 6],
    pub color_grading: ColorGradingUniform,
    pub mip_bias: f32,
    pub frame_count: u32,
}
```

这是一个标准的 view constant buffer，包含：
- 各种空间变换矩阵（world ↔ view ↔ clip）
- `unjittered_clip_from_world`：TAA 等时域技术需要无 jitter 的 VP 矩阵做历史帧重投影
- `frustum`：6 个 world-space half-space（`normal.dot(p) + distance > 0`），用于 GPU culling
- `color_grading`：`ColorGrading` 组件打包后的 GPU 友好格式
- `mip_bias`：纹理采样 mip 偏移，常用于 TAA/FSR 等后处理
- `frame_count`：递增帧计数器，用于时域噪声或动画

---

## 三、第二轮：数据层

### 3.1 View 与 Camera 的数据流全景

```text
Main World                              Render World (ExtractSchedule)
─────────────────────────────────────────────────────────────────────────
Camera + GlobalTransform + VisibleEntities
    │                                          │
    └──── extract_cameras ─────────────────────┤
                                               ▼
                                    ExtractedCamera (target, order, ...)
                                    ExtractedView (projection, transform, ...)
                                    RenderExtractedVisibleEntities
                                    Frustum / TemporalJitter / MipBias ...
                                               │
                        Render (PrepareViews / PrepareResources)
                                               │
                                               ▼
                                    ViewTarget (main_texture A/B)
                                    ViewDepthTexture
                                    ViewUniformOffset
                                    RenderVisibleEntities (CPU culled)
                                               │
                        Render (Render phase)
                                               │
                                               ▼
                                    TrackedRenderPass ← ViewTarget::get_color_attachment()
```

### 3.2 Msaa —— 多重采样反走样配置

```rust
// src/view/mod.rs:225-263
#[derive(Component, Default, Clone, Copy, ExtractComponent, Reflect, PartialEq, PartialOrd, Eq, Hash, Debug)]
pub enum Msaa {
    Off = 1,
    Sample2 = 2,
    #[default]
    Sample4 = 4,
    Sample8 = 8,
}
```

`Msaa` 是 `Camera` 的 required component（`CameraPlugin::build` 中 `register_required_components::<Camera, Msae>()`）。`ViewPlugin` 在 `finish` 时把 `Msaa` 提取到 render world。`prepare_view_targets` 根据 `Msaa` 的 sample count 决定是否创建额外的 multisampled texture（`main_texture_sampled`）。

### 3.3 RenderVisibleEntities —— 按 VisibilityClass 分类的可见实体

```rust
// src/view/visibility/mod.rs:44-49
#[derive(Clone, Component, Default, Debug)]
pub struct RenderVisibleEntities {
    pub classes: TypeIdMap<RenderVisibleEntitiesClass>,
}

// src/view/visibility/mod.rs:87-108
#[derive(Clone, Debug, Default, Reflect)]
pub struct RenderVisibleEntitiesClass {
    pub entities_cpu_culling: Vec<(Entity, MainEntity)>,
    pub entities_gpu_culling: MainEntityHashMap<Entity>,
    added_entities: Vec<(Entity, MainEntity)>,
    pub removed_entities: Vec<(Entity, MainEntity)>,
}
```

- **`classes`**：按 `VisibilityClass`（TypeId）分类。不同类别的可见实体（如 3D mesh、2D sprite、particle）可能由不同的 system 处理。
- **`entities_cpu_culling`**：经过 CPU frustum culling 后仍然可见的实体，已排序，支持二分查找。
- **`entities_gpu_culling`**：带有 `NoCpuCulling` 的实体，跳过 CPU culling，由 GPU 端 culling shader 处理。
- **`added_entities` / `removed_entities`**：增量更新列表，供 `DirtySpecializations` 使用，避免每帧全量重排。

### 3.4 ExtractedWindows + WindowSurfaces —— 窗口与 Swapchain

```rust
// src/view/window/mod.rs:50-75
pub struct ExtractedWindow {
    pub entity: Entity,
    pub handle: RawHandleWrapper,
    pub physical_width: u32,
    pub physical_height: u32,
    pub present_mode: PresentMode,
    pub desired_maximum_frame_latency: Option<NonZero<u32>>,
    pub swap_chain_texture_view: Option<TextureView>,
    pub swap_chain_texture: Option<SurfaceTexture>,
    pub swap_chain_texture_format: Option<TextureFormat>,
    pub size_changed: bool,
    pub present_mode_changed: bool,
    pub alpha_mode: CompositeAlphaMode,
    pub needs_initial_present: bool,
}
```

- **`swap_chain_texture_view`**：当前帧的 swapchain texture view。`prepare_windows` 每帧调用 `surface.get_current_texture()` 获取。
- **`size_changed` / `present_mode_changed`**：标记窗口属性变化，`create_surfaces` 会根据这些标记重新配置 surface。
- **`needs_initial_present`**：Wayland 特殊处理——窗口必须至少 present 一次才会显示。

```rust
// src/view/window/mod.rs:209-214
#[derive(Resource, Default)]
pub struct WindowSurfaces {
    surfaces: EntityHashMap<SurfaceData>,
    configured_windows: EntityHashSet,
}
```

`WindowSurfaces` 持有 wgpu `Surface` 对象和对应的 `SurfaceConfiguration`。Surface 的生命周期与窗口绑定，窗口关闭时通过 `extract_windows` 中的 `RemovedComponents<RawHandleWrapper>` 检测并清理。

### 3.5 DirtySpecializations —— 管线特化的脏标记

```rust
// src/camera.rs:825-843
#[derive(Clone, Resource, Default)]
pub struct DirtySpecializations {
    pub changed_renderables: MainEntityHashSet,
    pub removed_renderables: MainEntityHashSet,
    pub views: HashSet<RetainedViewEntity>,
}
```

当实体的材质、mesh、或 view 的某些属性发生变化时，对应的 specialized pipeline 可能失效。`DirtySpecializations` 是一个保守的脏标记集合：
- **`changed_renderables`**：实体自身变化，需要重新特化 pipeline。
- **`removed_renderables`**：实体被销毁，需要移除对应的 phase item。
- **`views`**：view 本身变化（如相机移动导致 frustum 变化），该 view 下所有可见实体都需要重新特化。

`extract_cameras` 系统会在提取相机时，把 `RenderVisibleEntities` 中的增量变化（`added_entities` / `removed_entities`）同步到 `DirtySpecializations`，供后续的 specialization/queuing 系统使用。

---

## 四、第三轮：逻辑层

### 4.1 提取阶段：extract_cameras

```rust
// src/camera.rs:473-697
pub fn extract_cameras(
    mut commands: Commands,
    mut main_pass_formats: ResMut<CameraMainPassTextureFormats>,
    query: Extract<Query<(Entity, RenderEntity, &Camera, &RenderTarget, &CameraRenderGraph,
                          &GlobalTransform, &VisibleEntities, &Frustum, ...)>>,
    ...
) {
    main_pass_formats.clear();
    for (main_entity, render_entity, camera, render_target, ...) in query.iter() {
        if !camera.is_active {
            commands.entity(render_entity).remove::<ExtractedCameraComponents>();
            continue;
        }
        // 计算 viewport、target size
        if let (Some(viewport_rect), Some(viewport_size), Some(target_size)) = (...) {
            // 构建 RenderExtractedVisibleEntities
            let mut render_visible_entities_cpu_culling = ...;
            for (visibility_class, visible_mesh_entities) in visible_entities.entities.iter() {
                let render_view_visible_entities = render_visible_entities_cpu_culling
                    .classes.entry(*visibility_class).or_default();
                render_view_visible_entities.entities.clear();
                for main_entity in visible_mesh_entities {
                    let render_entity = visibility_extraction_system_param.mapper.get(*main_entity)
                        .map(|e| e.entity()).unwrap_or(Entity::PLACEHOLDER);
                    render_view_visible_entities.entities.push((render_entity, MainEntity::from(*main_entity)));
                }
            }
            // 插入 ExtractedCamera + ExtractedView + ...
            commands.entity(render_entity).insert((
                ExtractedCamera { ... },
                ExtractedView {
                    retained_view_entity: RetainedViewEntity::new(main_entity.into(), None, 0),
                    clip_from_view: camera.clip_from_view(),
                    world_from_view: *transform,
                    clip_from_world: None,
                    target_format,
                    viewport: UVec4::new(viewport_origin.x, viewport_origin.y, viewport_size.x, viewport_size.y),
                    color_grading,
                    invert_culling: camera.invert_culling,
                },
                render_visible_entities_cpu_culling,
                *frustum,
            ));
            // 可选组件：TemporalJitter、MipBias、RenderLayers、Projection、NoIndirectDrawing
            ...
        }
    }
}
```

**关键逻辑：**
1. **跳过非活跃相机**：`camera.is_active == false` 时移除所有已提取组件。
2. **可见性列表转换**：`VisibleEntities` 是 main world 的组件，里面存的是 main world entity。`extract_cameras` 通过 `VisibilityExtractionSystemParam.mapper`（`SQuery<Read<RenderEntity>>`）把它们映射到 render world entity。如果某个实体没有对应的 render entity（比如未被提取的 mesh），则填 `Entity::PLACEHOLDER`。
3. **HDR / CompositingSpace 决定 target format**：HDR 强制 `Rgba16Float`；`CompositingSpace::Srgb` 用 `Rgba8Unorm`（shader 输出 sRGB，gamma 编码混合）；其他情况用 target 原格式。
4. **`NoIndirectDrawing`**：如果平台不支持 `GpuPreprocessingMode::Culling` 或用户显式添加了 `NoIndirectDrawing` 组件，则插入该组件，禁用 GPU culling/indirect draw。

### 4.2 准备阶段：prepare_view_targets

```rust
// src/view/mod.rs:1148-1284
pub fn prepare_view_targets(
    mut commands: Commands,
    clear_color_global: Res<ClearColor>,
    render_device: Res<RenderDevice>,
    mut texture_cache: ResMut<TextureCache>,
    cameras: Query<(Entity, &ExtractedCamera, &ExtractedView, &CameraMainTextureUsages, &Msaa)>,
    view_target_attachments: Res<ViewTargetAttachments>,
    mut main_texture_atomics: Local<HashMap<MainTextureKey, Weak<AtomicUsize>>>,
) {
    main_texture_atomics.retain(|_, weak| weak.strong_count() > 0);
    for (entity, camera, view, texture_usage, msaa) in cameras.iter() {
        let Some(target_size) = camera.physical_target_size else {
            commands.entity(entity).try_remove::<ViewTarget>();
            continue;
        };
        let out_attachment = camera.target.as_ref().and_then(|t| view_target_attachments.get(t));
        // 无输出且需要 clear 时跳过
        if out_attachment.is_none() && !matches!(camera.clear_color, ClearColorConfig::None) {
            commands.entity(entity).try_remove::<ViewTarget>();
            continue;
        }
        let main_texture_format = view.target_format;
        let clear_color = match camera.clear_color { ... };
        let converted_clear_color = clear_color.map(|color| match camera.compositing_space { ... });
        let key: MainTextureKey = (camera.target.clone(), texture_usage.0, main_texture_format, *msaa);
        let (a, b, sampled, main_texture) = textures.entry(key.clone()).or_insert_with(|| {
            // 从 TextureCache 获取或创建 A/B 两个 texture
            // MSAA 时额外创建 sampled texture
            // 复用跨帧的 AtomicUsize
        });
        commands.entity(entity).insert(ViewTarget {
            main_texture: main_textures.main_texture.clone(),
            main_textures,
            main_texture_format,
            out_texture: out_attachment.cloned(),
            compositing_space: camera.compositing_space,
        });
    }
}
```

**关键逻辑：**
1. **双缓冲复用**：同一 `NormalizedRenderTarget` + format + MSAA 配置的相机共享同一对 A/B texture。`main_texture_atomics` 用 `Weak<AtomicUsize>` 追踪生命周期，避免窗口关闭后泄漏。
2. **MSAA 处理**：`msaa.samples() > 1` 时创建 multisampled texture 作为实际 render attachment，主 texture 作为 resolve target。
3. **Clear color 转换**：根据 `CompositingSpace` 把 `ClearColor` 转换为对应颜色空间的 `WgpuColor`。如果 main texture 存 Oklab，clear 时也要转成 Oklab，否则会出现颜色偏差。
4. **Viewport clamp**：如果 `physical_target_size` 无效（0x0），直接移除 `ViewTarget`，该相机本帧不渲染。

### 4.3 准备阶段：prepare_view_uniforms

```rust
// src/view/mod.rs:986-1076
pub fn prepare_view_uniforms(
    mut commands: Commands,
    render_device: Res<RenderDevice>,
    render_queue: Res<RenderQueue>,
    mut view_uniforms: ResMut<ViewUniforms>,
    views: Query<(Entity, Option<&ExtractedCamera>, &ExtractedView, Option<&Frustum>,
                  Option<&TemporalJitter>, Option<&MipBias>, Option<&MainPassResolutionOverride>)>,
    frame_count: Res<FrameCount>,
) {
    let view_iter = views.iter();
    let view_count = view_iter.len();
    let Some(mut writer) = view_uniforms.uniforms.get_writer(view_count, &render_device, &render_queue)
    else { return; };
    for (entity, extracted_camera, extracted_view, frustum, temporal_jitter, mip_bias, resolution_override)
        in &views
    {
        let viewport = extracted_view.viewport.as_vec4();
        let mut main_pass_viewport = viewport;
        if let Some(resolution_override) = resolution_override {
            main_pass_viewport.z = resolution_override.0.x as f32;
            main_pass_viewport.w = resolution_override.0.y as f32;
        }
        let unjittered_projection = extracted_view.clip_from_view;
        let mut clip_from_view = unjittered_projection;
        if let Some(temporal_jitter) = temporal_jitter {
            temporal_jitter.jitter_projection(&mut clip_from_view, main_pass_viewport.zw());
        }
        let view_from_clip = clip_from_view.inverse();
        let world_from_view = extracted_view.world_from_view.to_matrix();
        let view_from_world = world_from_view.inverse();
        let clip_from_world = if temporal_jitter.is_some() {
            clip_from_view * view_from_world
        } else {
            extracted_view.clip_from_world.unwrap_or_else(|| clip_from_view * view_from_world)
        };
        let frustum = frustum.map(|f| f.half_spaces.map(|h| h.normal_d())).unwrap_or([Vec4::ZERO; 6]);
        let view_uniforms = ViewUniformOffset {
            offset: writer.write(&ViewUniform {
                clip_from_world,
                unjittered_clip_from_world: unjittered_projection * view_from_world,
                world_from_clip: world_from_view * view_from_clip,
                world_from_view,
                view_from_world,
                clip_from_view,
                view_from_clip,
                world_position: extracted_view.world_from_view.translation(),
                exposure: extracted_camera.map(|c| c.exposure).unwrap_or_default(),
                viewport,
                main_pass_viewport,
                frustum,
                color_grading: extracted_view.color_grading.clone().into(),
                mip_bias: mip_bias.unwrap_or(&MipBias(0.0)).0,
                frame_count: frame_count.0,
            }),
        };
        commands.entity(entity).insert(view_uniforms);
    }
}
```

**关键逻辑：**
1. **DynamicUniformBuffer**：`ViewUniforms` 使用 `DynamicUniformBuffer<ViewUniform>`，支持每个 view 一个 uniform，通过 dynamic offset 绑定。如果平台支持 storage buffer，还会添加 `BufferUsages::STORAGE`。
2. **Temporal Jitter**：如果存在 `TemporalJitter` 组件，调用 `jitter_projection` 修改 `clip_from_view`。TAA 需要每帧对投影矩阵做子像素偏移。
3. **Resolution Override**：`MainPassResolutionOverride` 允许以更低分辨率渲染 main pass（动态分辨率缩放），此时 `main_pass_viewport` 与 `viewport` 不同。
4. **Frustum 打包**：6 个 half-space 平面被编码为 `array<vec4<f32>, 6>`，每个 `vec4` 是 `(normal.xyz, distance)`。

### 4.4 窗口准备：prepare_windows + create_surfaces

```rust
// src/view/window/mod.rs:244-338
pub fn prepare_windows(
    mut windows: ResMut<ExtractedWindows>,
    mut window_surfaces: ResMut<WindowSurfaces>,
    render_device: Res<RenderDevice>,
    sorted_cameras: Res<SortedCameras>,
) {
    for window in windows.windows.values_mut() {
        // 跳过没有被任何相机瞄准的窗口（除非需要 initial present）
        let is_camera_target = sorted_cameras.0.iter().any(|c| {
            matches!(&c.target, Some(NormalizedRenderTarget::Window(w)) if w.entity() == window.entity)
                && matches!(c.output_mode, CameraOutputMode::Write { .. })
        });
        if !is_camera_target && !window.needs_initial_present {
            continue;
        }
        // 如果已有 swapchain texture 且窗口未变化，复用
        if window.has_swapchain_texture() && !window.size_changed && !window.present_mode_changed {
            continue;
        }
        // 获取新的 swapchain texture
        let surface = &surface_data.surface;
        match surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(surface_texture)
            | wgpu::CurrentSurfaceTexture::Suboptimal(surface_texture) => {
                window.set_swapchain_texture(surface_texture);
            }
            wgpu::CurrentSurfaceTexture::Outdated => {
                render_device.configure_surface(surface, &surface_data.configuration);
                // 重试获取...
            }
            wgpu::CurrentSurfaceTexture::Occluded => {}
            other => { bevy_log::error!("Couldn't get swap chain texture: {other:?}"); }
        }
    }
}
```

```rust
// src/view/window/mod.rs:361-466
pub fn create_surfaces(
    mut windows: ResMut<ExtractedWindows>,
    mut window_surfaces: ResMut<WindowSurfaces>,
    render_instance: Res<RenderInstance>,
    render_adapter: Res<RenderAdapter>,
    render_device: Res<RenderDevice>,
) {
    for window in windows.windows.values_mut() {
        let data = window_surfaces.surfaces.entry(window.entity).or_insert_with(|| {
            let surface_target = SurfaceTargetUnsafe::RawHandle {
                raw_display_handle: Some(window.handle.get_display_handle()),
                raw_window_handle: window.handle.get_window_handle(),
            };
            // SAFETY: window handles are valid objects to create surfaces on
            let surface = unsafe {
                render_instance.create_surface_unsafe(surface_target)
                    .expect("Failed to create wgpu surface")
            };
            let caps = surface.get_capabilities(&render_adapter);
            let present_mode = present_mode(window, &caps);
            let format = select_srgb_format(&caps.formats);
            let configuration = SurfaceConfiguration {
                format, width: window.physical_width, height: window.physical_height,
                usage: TextureUsages::RENDER_ATTACHMENT, present_mode,
                desired_maximum_frame_latency: ..., alpha_mode: ..., view_formats: ...,
            };
            render_device.configure_surface(&surface, &configuration);
            SurfaceData { surface: WgpuWrapper::new(surface), configuration, texture_view_format }
        });
        if window.size_changed || window.present_mode_changed {
            data.configuration.width = window.physical_width;
            data.configuration.height = window.physical_height;
            let caps = data.surface.get_capabilities(&render_adapter);
            data.configuration.present_mode = present_mode(window, &caps);
            render_device.configure_surface(&data.surface, &data.configuration);
        }
    }
}
```

**关键逻辑：**
1. **Lazy surface creation**：窗口第一次被提取时才会创建 wgpu `Surface`，不是窗口创建时就建。
2. **Present mode fallback**：`present_mode` 函数（`window/mod.rs:468-511`）实现了完整的 fallback 链。例如 `AutoVsync` 先尝试 `FifoRelaxed`，不行再回 `Fifo`。
3. **Linux timeout workaround**：`prepare_windows` 中对 Linux mesa driver 的 `Timeout` 错误做了特殊处理（`window/mod.rs:284-299`），避免某些驱动 quirks 导致崩溃。
4. **Swapchain reuse**：如果上一帧已经获取了 swapchain texture 且窗口未改变，直接复用，避免多余的 `get_current_texture` 调用。

### 4.5 可见性收集：collect_visible_cpu_culled_entities

```rust
// src/view/visibility/mod.rs:331-379
pub fn collect_visible_cpu_culled_entities(
    mut cameras: Query<(&mut RenderVisibleEntities, Option<&mut RenderExtractedVisibleEntities>)>,
    mut lights: Query<(&mut RenderShadowMapVisibleEntities, Option<&mut RenderExtractedShadowMapVisibleEntities>)>,
    mut visibility_classes: Local<HashSet<TypeId>>,
) {
    // Collect cameras
    for (mut render_visible_entities, mut maybe_cpu_culled) in cameras.iter_mut() {
        let mut maybe_subview = maybe_cpu_culled.as_deref_mut();
        collect_visible_cpu_culled_entities_for_subview(
            &mut render_visible_entities, &mut maybe_subview, &mut visibility_classes);
    }
    // Collect shadow maps
    for (mut shadow_map_visible, mut maybe_shadow_cpu_culled) in lights.iter_mut() {
        for (subview, render_visible_entities) in shadow_map_visible.subviews.iter_mut() {
            let mut maybe_subview = maybe_shadow_cpu_culled.as_mut()
                .and_then(|v| v.subviews.get_mut(subview));
            collect_visible_cpu_culled_entities_for_subview(
                render_visible_entities, &mut maybe_subview, &mut visibility_classes);
        }
    }
}
```

这个 system 把 `RenderExtractedVisibleEntities`（提取阶段生成）同步到 `RenderVisibleEntities`（渲染阶段使用）。它执行的是**双指针 merge/diff 算法**（`visibility/mod.rs:194-251`）：

1. 取出上一帧的 `entities_cpu_culling` 列表。
2. 遍历本帧新的可见列表，与旧列表做 lockstep 比较。
3. 旧列表中比当前实体小的，说明本帧不可见了，放入 `removed_entities`。
4. 如果旧列表中没有当前实体，说明是新出现的，放入 `added_entities`。
5. 遍历结束后，旧列表剩余的实体也放入 `removed_entities`。

这个 diff 机制使得 `DirtySpecializations` 可以精确知道哪些实体需要重新 queuing，而不是每帧全量清空重来。

### 4.6 相机排序：sort_cameras

```rust
// src/camera.rs:725-774
pub fn sort_cameras(
    mut sorted_cameras: ResMut<SortedCameras>,
    mut cameras: Query<(Entity, &mut ExtractedCamera)>,
) {
    sorted_cameras.0.clear();
    for (entity, camera) in cameras.iter() {
        sorted_cameras.0.push(SortedCamera {
            entity, order: camera.order, target: camera.target.clone(),
            hdr: camera.hdr, output_mode: camera.output_mode,
        });
    }
    // 按 (order, target) 排序，相同 target 的相机聚集在一起
    sorted_cameras.0.sort_by(|c1, c2| (c1.order, &c1.target).cmp(&(c2.order, &c2.target)));
    let mut previous_order_target = None;
    let mut ambiguities = <HashSet<_>>::default();
    let mut target_counts = <HashMap<_, _>>::default();
    for sorted_camera in &mut sorted_cameras.0 {
        let new_order_target = (sorted_camera.order, sorted_camera.target.clone());
        if previous_order_target == Some(new_order_target.clone()) {
            ambiguities.insert(new_order_target.clone());
        }
        if let Some(target) = &sorted_camera.target {
            let count = target_counts.entry((target.clone(), sorted_camera.hdr)).or_insert(0usize);
            let (_, mut camera) = cameras.get_mut(sorted_camera.entity).unwrap();
            camera.sorted_camera_index_for_target = *count;
            *count += 1;
        }
        previous_order_target = Some(new_order_target);
    }
    if !ambiguities.is_empty() {
        warn_once!("Camera order ambiguities detected for active cameras...");
    }
}
```

**关键逻辑：**
1. **按 order + target 排序**：确保同一 target 上先渲染的相机排在前面，后处理链按顺序执行。
2. **`sorted_camera_index_for_target`**：同一 target 上可能存在多个相机（如画中画），这个索引用于区分它们在后处理链路中的位置。
3. **Ambiguity warning**：如果两个 active camera 拥有完全相同的 `(order, target)`，会触发 `warn_once`，提醒用户可能误创建了重复相机。

---

## 五、设计决策分析

### 决策 1：为什么 ExtractedView 和 ExtractedCamera 要分成两个组件？

**问题**：既然每个 camera 都有 view，为什么不把它们合并成一个 `ExtractedCamera`？

**分析**：

在 Bevy 中，**view 是"观察点"，camera 是"一种特殊的观察点"**。Shadow-casting light 也需要观察点（为了渲染 shadow map），但它没有 `Camera` 组件，也不需要 target texture、clear color、MSAA 这些 camera 专属属性。

如果把两者合并：
- Light 的 shadow view 会携带大量无意义的 `Option<T>` 字段（target、viewport、clear color 等）。
- `prepare_view_targets` 等 camera-only 的 system 需要额外过滤，逻辑耦合加重。

拆成两个组件后，`ExtractedView` 成为 view 的**最小公共子集**，任何需要渲染的视点都可以拥有它；`ExtractedCamera` 作为**扩展组件**，只在真正的 camera 上存在。这种组合优于继承的设计，与 ECS "组合优于继承"的哲学一致。

### 决策 2：为什么 ViewTarget 使用双缓冲（A/B texture）？

**问题**：为什么不能只用一个 texture，后处理直接原地读写？

**分析**：

后处理 pass 通常需要**读取上一 pass 的结果作为输入，同时写入新的输出**。如果只有一个 texture，GPU 需要同时读写同一张纹理，这会触发 **RAW（Read-After-Write）hazard**，在 wgpu 的 validation 层直接报错。

双缓冲方案：`post_process_write()`（`view/mod.rs:940-960`）通过 `AtomicUsize::fetch_xor(1)` 原子翻转当前读写目标：
- 第 1 个 post-process：读 A，写 B
- 第 2 个 post-process：读 B，写 A
- 第 3 个 post-process：读 A，写 B
- ...

多个共享同一 `NormalizedRenderTarget` 的 view 共享同一个 `AtomicUsize`，这意味着它们的 post-process 链是连续的——A 相机写完后 B 相机接着读，不需要额外的 blit。这是 Bevy 处理多相机同窗口时的关键优化。

---

## 六、关联辐射

- **向上**：`CameraPlugin` 在 `PostUpdate` 阶段运行 `camera_system`，响应窗口大小变化、更新 projection；在 `ExtractSchedule` 阶段运行 `extract_cameras`，把数据搬到 render world。
- **向下**：`ViewTarget` 的 `get_color_attachment()` 返回 `RenderPassColorAttachment`，供 `TrackedRenderPass` 使用；`ViewUniformOffset` 被 bind group 绑定到 shader。
- **横向**：`RenderVisibleEntities` 由 `extract_cameras` 填充初值，再由 `collect_visible_cpu_culled_entities` 增量更新，最终供 `RenderPhase` 的 queuing 系统消费。
- **跨模块**：`DirtySpecializations` 在 `bevy_pbr` 的 specialization/queuing 系统中被读取，决定哪些实体需要重新生成 specialized pipeline 和重新进入 render phase。

---

## 七、关键源码片段

### 7.1 TemporalJitter —— TAA 的投影矩阵抖动

```rust
// src/camera.rs:779-799
#[derive(Component, Clone, Default, Reflect)]
pub struct TemporalJitter {
    pub offset: Vec2,  // range [-0.5, 0.5]
}

impl TemporalJitter {
    pub fn jitter_projection(&self, clip_from_view: &mut Mat4, view_size: Vec2) {
        let mut jitter = (self.offset * vec2(2.0, -2.0)) / view_size;
        // orthographic
        if clip_from_view.w_axis.w == 1.0 {
            jitter *= vec2(clip_from_view.x_axis.x, clip_from_view.y_axis.y) * 0.5;
        }
        clip_from_view.z_axis.x += jitter.x;
        clip_from_view.z_axis.y += jitter.y;
    }
}
```

这里不是修改投影矩阵的平移分量（`w_axis`），而是修改 `z_axis.xy`。这是因为在列主序的投影矩阵中，`z_axis.x` 和 `z_axis.y` 控制着 clip-space 的 x/y 偏移。对透视投影，这相当于对近平面做子像素偏移；对正交投影，需要根据正交缩放调整 jitter 幅度。

### 7.2 ColorGrading 的 GPU 打包

```rust
// src/view/mod.rs:710-786
impl From<ColorGrading> for ColorGradingUniform {
    fn from(component: ColorGrading) -> Self {
        let white_point_xy = D65_XY + vec2(-component.global.temperature, component.global.tint);
        let white_point_lms = vec3(0.701634, 1.15856, -0.904175)
            + (vec3(-0.051461, 0.045854, 0.953127)
                + vec3(0.452749, -0.296122, -0.955206) * white_point_xy.x)
                / white_point_xy.y;
        let white_point_adjustment = Mat3::from_diagonal(D65_LMS / white_point_lms);
        let balance = LMS_TO_RGB * white_point_adjustment * RGB_TO_LMS;
        Self {
            balance,
            saturation: vec3(component.shadows.saturation, component.midtones.saturation, component.highlights.saturation),
            contrast: vec3(component.shadows.contrast, component.midtones.contrast, component.highlights.contrast),
            gamma: vec3(component.shadows.gamma, component.midtones.gamma, component.highlights.gamma),
            gain: vec3(component.shadows.gain, component.midtones.gain, component.highlights.gain),
            lift: vec3(component.shadows.lift, component.midtones.lift, component.highlights.lift),
            midtone_range: vec2(component.global.midtones_range.start, component.global.midtones_range.end),
            exposure: component.global.exposure,
            hue: component.global.hue,
            post_saturation: component.global.post_saturation,
        }
    }
}
```

这段代码把用户友好的 `ColorGrading`（shadows/midtones/highlights 分开调节）打包成 GPU 友好的 `ColorGradingUniform`。核心计算是 **white balance 矩阵**：先把 D65 白点根据 temperature/tint 偏移，转换到 LMS 色彩空间做缩放，再转回 RGB。整个 pipeline 是 `RGB → LMS → corrected LMS → corrected RGB`，合并为单个 3x3 `balance` 矩阵。

### 7.3 camera_system —— Main World 中的相机更新

```rust
// src/camera.rs:351-446
pub fn camera_system(
    mut window_resized_reader: MessageReader<WindowResized>,
    mut window_created_reader: MessageReader<WindowCreated>,
    mut window_scale_factor_changed_reader: MessageReader<WindowScaleFactorChanged>,
    mut image_asset_event_reader: MessageReader<AssetEvent<Image>>,
    ...
    mut cameras: Query<(&mut Camera, &RenderTarget, &mut Projection)>,
) -> Result<(), BevyError> {
    // 收集所有变化的 window / image
    let mut changed_window_ids = EntityHashSet::default();
    changed_window_ids.extend(window_created_reader.read().map(|e| e.window));
    changed_window_ids.extend(window_resized_reader.read().map(|e| e.window));
    let changed_image_handles: HashSet<&AssetId<Image>> = image_asset_event_reader.read()
        .filter_map(|event| match event { AssetEvent::Modified { id } | AssetEvent::Added { id } => Some(id), _ => None })
        .collect();
    for (mut camera, render_target, mut camera_projection) in &mut cameras {
        if let Some(normalized_target) = render_target.normalize(primary_window)
            && (normalized_target.is_changed(&changed_window_ids, &changed_image_handles)
                || camera.is_added()
                || camera_projection.is_changed()
                || camera.computed.old_viewport_size != viewport_size
                || camera.computed.old_sub_camera_view != camera.sub_camera_view)
        {
            let new_computed_target_info = normalized_target.get_render_target_info(...)?;
            // DPI 变化时自动缩放 viewport
            if normalized_target.is_changed(&scale_factor_changed_window_ids, &HashSet::default()) {
                if let Some(ref mut viewport) = camera.viewport {
                    viewport.physical_position = resize(viewport.physical_position);
                    viewport.physical_size = resize(viewport.physical_size);
                }
            }
            camera.computed.target_info = Some(new_computed_target_info);
            if let Some(size) = camera.logical_viewport_size() && size.x != 0.0 && size.y != 0.0 {
                camera_projection.update(size.x, size.y);
                camera.computed.clip_from_view = camera_projection.get_clip_from_view();
            }
        }
    }
    Ok(())
}
```

这个 system 运行在 `PostUpdate`（main world），负责：
1. 检测窗口创建、resize、DPI 变化事件。
2. 如果 camera 的 render target 发生变化，重新计算 `target_info`（physical size + scale factor）。
3. DPI 变化时，自动按比例缩放 viewport，避免窗口拖到高 DPI 显示器后 viewport 变小。
4. 调用 `Projection::update()` 更新投影矩阵（如透视投影的 aspect ratio）。

---

## 八、关联阅读

- [[Notes/Bevy/第三阶段-渲染管线/Bevy-bevy_render-源码解析：RenderPhase 与绘制排序|Bevy-bevy_render-源码解析：RenderPhase 与绘制排序]]
- [[Notes/Bevy/第三阶段-渲染管线/Bevy-bevy_pbr-源码解析：MeshPipeline 与材质系统|MeshPipeline 与材质系统]]
- [[Notes/Bevy/第三阶段-渲染管线/Bevy-wgpu-源码解析：Surface 与 Swapchain|wgpu Surface 与 Swapchain 机制]]

---

## 九、索引状态

- 笔记状态：🟢 已完成
- 源码版本：bevy 0.19.0-dev (bevy_render crate)
- 最后更新：2026-05-14
- 关联笔记：RenderPhase 与绘制排序、MeshPipeline、Surface/Swapchain
