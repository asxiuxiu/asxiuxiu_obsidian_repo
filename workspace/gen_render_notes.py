import os

os.makedirs("Notes/Bevy/第三阶段-渲染管线", exist_ok=True)

# Note 2
note2 = """---
title: Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动
date: 2026-05-14
tags:
  - bevy-source
  - bevy_render
  - 第三阶段-渲染管线
aliases:
  - Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy 全解析主索引]]

# Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动

> **分析范围**：`crates/bevy_render/src/lib.rs`（RenderSystems、Render Schedule）、`src/renderer/mod.rs`、`src/renderer/render_context.rs`、`src/renderer/render_device.rs`、`src/renderer/wgpu_wrapper.rs`、`src/settings.rs` | **分析轮次**：第一轮 + 第二轮 + 第三轮全量 | **源码版本**：Bevy 0.19.0-dev
>
> **注意**：在 Bevy 0.19 中，传统的 "RenderGraph 节点图" 已被重构为基于 ECS Schedule 的渲染管线驱动。`RenderGraph` 现在是一个 `ScheduleLabel`，而非节点图。本笔记基于最新源码结构进行分析。

---

## 零、Render Schedule 与渲染管线驱动是什么？为什么需要它？

渲染管线驱动回答的核心问题是：**一帧中，GPU 命令按照什么顺序生成和提交？**

在早期 Bevy 版本中，这由一个叫 `RenderGraph` 的节点图系统管理：每个节点代表一个渲染步骤（如"生成阴影贴图"、"主场景渲染"、"后处理"），节点之间通过边表达依赖关系。但这个设计在复杂场景下遇到了问题：节点图与 ECS 的集成不够自然，难以利用 Bevy 的 System 并行调度。

在 Bevy 0.19 中，渲染管线被**彻底 ECS 化**：
- `RenderGraph` 变成了一个 `ScheduleLabel`，运行一个由 `RenderGraphSystems`（Begin → Render → Submit → Finish）组成的简单 Schedule
- 真正的渲染阶段顺序由 `RenderSystems`（ExtractCommands → PrepareAssets → ... → Render → Cleanup）这个 ECS SystemSet 链控制
- 每个阶段都是普通的 ECS System，可以被用户插件插入、排序、并行化

> **一句话总结**：Bevy 的渲染管线现在是一个标准的 ECS Schedule，渲染阶段是普通 SystemSet，充分利用了 Bevy 的并行调度能力。

---

## 一、模块定位与构建定义

### 1.1 关键文件职责表

| 文件 | 行数 | 职责 |
|------|------|------|
| `src/lib.rs` | ~587 | `RenderSystems` 枚举、`Render` Schedule 基础结构、`RenderScheduleOrder` |
| `src/renderer/mod.rs` | ~366 | `RenderGraph` ScheduleLabel、`render_system` 帧驱动、`RenderQueue`/`RenderAdapter`/`RenderInstance` |
| `src/renderer/render_context.rs` | ~301 | `RenderContext` 系统参数、`PendingCommandBuffers`、`RenderContextState` |
| `src/renderer/render_device.rs` | ~310 | `RenderDevice`：GPU 资源工厂（shader、pipeline、buffer、texture） |
| `src/renderer/wgpu_wrapper.rs` | ~50 | `WgpuWrapper`：跨平台线程安全包装器 |
| `src/settings.rs` | ~335 | `WgpuSettings`、`RenderCreation`、`RenderResources` |

---

## 二、第一轮：接口层（What）

### 2.1 RenderSystems — 渲染管线的阶段定义

> 文件：`src/lib.rs`，第 154~208 行

```rust
#[derive(Debug, Hash, PartialEq, Eq, Clone, SystemSet)]
pub enum RenderSystems {
    ExtractCommands,              // 应用提取命令
    PrepareAssets,                // 准备资产
    PrepareMeshes,                // 准备网格
    CreateViews,                  // 创建视图（阴影等）
    Specialize,                   // 管线特化
    PrepareViews,                 // 准备视图 uniform
    Queue,                        // 将实体加入渲染阶段
    QueueMeshes,                  // 子阶段：网格入队
    QueueSweep,                   // 子阶段：移除不可见实体
    PhaseSort,                    // 阶段排序
    Prepare,                      // 准备渲染资源
    PrepareResources,             // 子阶段：初始化 buffer/texture/uniform
    PrepareResourcesBatchPhases,  // 子阶段：为渲染阶段创建批次
    PrepareResourcesWritePhaseBuffers,   // 子阶段：写阶段 buffer
    PrepareResourcesCollectPhaseBuffers, // 子阶段：收集阶段 buffer
    PrepareResourcesFlush,        // 子阶段：刷新资源
    PrepareBindGroups,            // 子阶段：创建 BindGroup
    Render,                       // 执行渲染
    Cleanup,                      // 清理
    PostCleanup,                  // 最终清理临时实体
}
```

### 2.2 Render::base_schedule() — 阶段链式配置

> 文件：`src/lib.rs`，第 290~338 行

```rust
impl Render {
    pub fn base_schedule() -> Schedule {
        let mut schedule = Schedule::new(Self);
        schedule.configure_sets(
            (
                ExtractCommands, PrepareMeshes, CreateViews,
                Specialize, PrepareViews, Queue, PhaseSort,
                Prepare, Render, Cleanup, PostCleanup,
            ).chain(),
        );
        schedule.configure_sets((ExtractCommands, PrepareAssets, PrepareMeshes, Prepare).chain());
        schedule.configure_sets(
            (QueueMeshes, QueueSweep)
                .chain()
                .in_set(Queue)
                .after(prepare_assets::<RenderMesh>),
        );
        schedule.configure_sets(
            (
                PrepareResources, PrepareResourcesBatchPhases,
                PrepareResourcesWritePhaseBuffers, PrepareResourcesCollectPhaseBuffers,
                PrepareResourcesFlush, PrepareBindGroups,
            ).chain().in_set(Prepare),
        );
        schedule
    }
}
```

这个配置创建了两条主要链：
1. **顶层链**：`ExtractCommands → PrepareMeshes → CreateViews → Specialize → PrepareViews → Queue → PhaseSort → Prepare → Render → Cleanup → PostCleanup`
2. **Prepare 子链**：`PrepareResources → BatchPhases → WritePhaseBuffers → CollectPhaseBuffers → Flush → PrepareBindGroups`

### 2.3 RenderGraph — 根级 Schedule

> 文件：`src/renderer/mod.rs`，第 34~64 行

```rust
#[derive(ScheduleLabel, Debug, Clone, PartialEq, Eq, Hash, Default)]
pub struct RenderGraph;

impl RenderGraph {
    pub fn base_schedule() -> Schedule {
        let mut schedule = Schedule::new(Self);
        schedule.configure_sets(
            (
                RenderGraphSystems::Begin,
                RenderGraphSystems::Render,
                RenderGraphSystems::Submit,
                RenderGraphSystems::Finish,
            ).chain(),
        );
        schedule
    }
}

#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub enum RenderGraphSystems {
    Begin,   // 帧前设置
    Render,  // 主渲染（运行 Render Schedule）
    Submit,  // 提交命令缓冲
    Finish,  // 帧后收尾
}
```

`RenderGraph` Schedule 是**每帧最外层的驱动循环**。`render_system` 先运行 `RenderGraph`，然后提交最终的截图/GPU 回读命令，最后 present 窗口。

### 2.4 RenderDevice — GPU 资源工厂

> 文件：`src/renderer/render_device.rs`，第 15~30 行

```rust
#[derive(Resource, Clone)]
pub struct RenderDevice {
    device: WgpuWrapper<wgpu::Device>,
}
```

`RenderDevice` 封装了 `wgpu::Device`，提供了所有 GPU 资源的创建方法：
- `create_shader_module` / `create_and_validate_shader_module`
- `create_bind_group` / `create_bind_group_layout`
- `create_render_pipeline` / `create_compute_pipeline`
- `create_command_encoder` / `create_render_bundle_encoder`
- `create_texture` / `create_sampler` / `create_buffer`

### 2.5 RenderContext — 渲染系统的核心系统参数

> 文件：`src/renderer/render_context.rs`，第 132~136 行

```rust
#[derive(SystemParam)]
pub struct RenderContext<'w, 's> {
    state: Deferred<'s, RenderContextState>,
    render_device: Res<'w, RenderDevice>,
    diagnostics_recorder: Option<Res<'w, DiagnosticsRecorder>>,
}
```

`RenderContext` 是每个渲染系统最常用的参数，提供：
- `command_encoder()` — 获取或创建当前帧的命令编码器
- `begin_tracked_render_pass()` — 开始一个带状态追踪的 RenderPass
- `render_device()` — 获取 `RenderDevice`

---

## 三、第二轮：数据层（How - Structure）

### 3.1 渲染管线结构关系

```
render_system (每帧入口)
|
+- world.run_schedule(RenderGraph)
|   |
|   +- RenderGraphSystems::Begin
|   |   +- 用户/插件的帧前设置系统
|   |
|   +- RenderGraphSystems::Render
|   |   +- world.run_schedule(Render)  <- 核心！
|   |       |
|   |       +- ExtractCommands
|   |       +- PrepareAssets (Mesh, Texture, Shader...)
|   |       +- PrepareMeshes
|   |       +- CreateViews (shadow cascades, etc.)
|   |       +- Specialize (pipeline specialization)
|   |       +- PrepareViews (uniforms, targets)
|   |       +- Queue (phase item enqueue)
|   |       +- PhaseSort (sort/bin phase items)
|   |       +- Prepare (resources -> bind groups)
|   |       |   +- PrepareResources
|   |       |   +- PrepareResourcesBatchPhases
|   |       |   +- PrepareResourcesWritePhaseBuffers
|   |       |   +- PrepareResourcesCollectPhaseBuffers
|   |       |   +- PrepareResourcesFlush
|   |       |   +- PrepareBindGroups
|   |       +- Render (issue draw calls via TrackedRenderPass)
|   |       +- Cleanup
|   |       +- PostCleanup
|   |
|   +- RenderGraphSystems::Submit
|   |   +- 提交 PendingCommandBuffers
|   |
|   +- RenderGraphSystems::Finish
|       +- 用户/插件的帧后收尾
|
+- 创建 final encoder，提交 screenshot + readback 命令
+- render_queue.submit([encoder.finish()])
|
+- Present swapchain textures
```

### 3.2 核心资源结构

```
World (RenderApp)
├── RenderDevice              // wgpu Device 包装
├── RenderQueue               // wgpu Queue 包装
├── RenderInstance            // wgpu Instance 包装
├── RenderAdapter             // wgpu Adapter 包装
├── RenderAdapterInfo         // GPU 信息
├── PendingCommandBuffers     // 待提交的命令缓冲池
├── RenderContextState        // 当前帧编码器状态
├── RenderScheduleOrder       // 可扩展的 Schedule 执行顺序
└── FutureRenderResources     // 异步初始化的 GPU 资源
```

### 3.3 RenderContextState — 命令编码器的生命周期

> 文件：`src/renderer/render_context.rs`，第 64~104 行

```rust
#[derive(Default)]
struct RenderContextStateInner {
    command_encoder: Option<CommandEncoder>,
    command_buffers: Vec<CommandBuffer>,
    render_device: Option<RenderDevice>,
}

pub struct RenderContextState(WgpuWrapper<RenderContextStateInner>);
```

`RenderContextState` 是**每帧隐式管理命令缓冲区的状态机**：
- 首次访问 `command_encoder()` 时**懒创建**编码器
- 每个渲染系统结束后，`SystemBuffer::queue` 自动把编码器 finish 成 `CommandBuffer`，移入 `PendingCommandBuffers`
- 这种设计让多个渲染系统可以顺序生成 GPU 命令，而无需手动管理编码器生命周期

### 3.4 WgpuWrapper — 跨平台线程安全

> 文件：`src/renderer/wgpu_wrapper.rs`

```rust
pub struct WgpuWrapper<T>(
    #[cfg(not(all(target_arch = "wasm32", target_feature = "atomics")))] T,
    #[cfg(all(target_arch = "wasm32", target_feature = "atomics"))] send_wrapper::SendWrapper<T>,
);
```

这是一个**条件编译包装器**：
- **原生平台**：直接包含 `T`，无运行时开销
- **WASM + atomics**：使用 `send_wrapper::SendWrapper`，确保 wgpu 对象只能在创建线程访问（WebGPU 要求），同时实现 `Send + Sync`

---

## 四、第三轮：逻辑层（How - Behavior）

### 4.1 render_system：每帧渲染的总入口

> 文件：`src/renderer/mod.rs`，第 69~121 行

```rust
pub fn render_system(
    world: &mut World,
    state: &mut SystemState<Query<(&ViewTarget, &ExtractedCamera)>>,
) {
    // 1. 运行 RenderGraph Schedule（核心渲染流程）
    world.run_schedule(RenderGraph);

    // 2. 创建最终命令编码器，处理截图和 GPU 回读
    let render_device = world.resource::<RenderDevice>();
    let render_queue = world.resource::<RenderQueue>();
    let mut encoder = render_device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
    crate::view::screenshot::submit_screenshot_commands(world, &mut encoder);
    crate::gpu_readback::submit_readback_commands(world, &mut encoder);
    render_queue.submit([encoder.finish()]);

    // 3. Present 所有需要显示的窗口
    world.resource_scope(|world, mut windows: Mut<ExtractedWindows>| {
        let views = state.get(world).unwrap();
        for window in windows.values_mut() {
            let view_needs_present = views.iter().any(|(view_target, camera)| {
                matches!(camera.target, Some(NormalizedRenderTarget::Window(w)) if w.entity() == window.entity)
                    && view_target.needs_present()
            });
            if view_needs_present || window.needs_initial_present {
                window.present();
                window.needs_initial_present = false;
            }
        }
    });

    crate::view::screenshot::collect_screenshots(world);
}
```

**关键决策点**：
- 为什么截图和 readback 在 `RenderGraph` 之后单独处理？因为它们需要访问 `RenderGraph` 生成的渲染结果（如颜色附件），且通常只需要一个额外的 copy 命令。
- `needs_initial_present` 是 Wayland 平台的特殊处理：Wayland 窗口需要至少 present 一次才会显示。

### 4.2 命令缓冲区的隐式提交链

```rust
// RenderContextState 实现 SystemBuffer，每系统结束后自动调用：
impl SystemBuffer for RenderContextState {
    fn queue(&mut self, _system_meta: &SystemMeta, mut world: DeferredWorld) {
        // 1. finish 当前编码器 -> CommandBuffer
        if let Some(encoder) = inner.command_encoder.take() {
            inner.command_buffers.push(encoder.finish());
        }
        // 2. 所有缓冲区移入 PendingCommandBuffers
        if !inner.command_buffers.is_empty() {
            let mut pending = world.resource_mut::<PendingCommandBuffers>();
            pending.push(core::mem::take(&mut inner.command_buffers));
        }
        // 3. 清除 render_device 引用（下系统会重新设置）
        inner.render_device = None;
    }
}
```

**关键决策点**：
- 为什么每个系统结束后都要 flush？因为 ECS 系统可以并行执行，如果不 flush，两个并行系统可能同时写入同一个 `CommandEncoder`（wgpu 不允许）。通过 `SystemBuffer` 机制，每个系统结束后编码器被 finish，下系统重新创建新编码器，天然线程安全。
- `PendingCommandBuffers` 在 `RenderGraphSystems::Submit` 阶段统一提交，确保所有渲染命令在 present 前到达 GPU。

### 4.3 RenderScheduleOrder — 可扩展的 Schedule 执行顺序

> 文件：`src/lib.rs`，第 251~283 行

```rust
#[derive(Resource, Debug)]
pub struct RenderScheduleOrder {
    pub labels: Vec<InternedScheduleLabel>,
}

impl Default for RenderScheduleOrder {
    fn default() -> Self {
        Self { labels: vec![Render.intern()] }
    }
}
```

默认只执行 `Render` Schedule，但插件可以通过 `insert_after` / `insert_before` 添加自定义 Schedule。例如，后处理插件可以插入 `PostProcess` Schedule 在 `Render` 之后运行。

---

## 五、设计决策分析

### 5.1 设计决策：为什么把 RenderGraph 从节点图改为 ECS Schedule？

**问题背景**：早期 Bevy 使用节点图（`Node` trait + `Edge` 依赖）来组织渲染步骤。但随着功能增加，节点图与 ECS 的集成越来越别扭。

**naive 方案（旧版）**：保留节点图，每个节点内部可以访问 ECS World。
- 优点：可视化清晰，依赖关系显式。
- 缺点：
  - 节点是"黑盒"，Bevy 的 System 并行调度器无法优化节点内部
  - 节点与 ECS 的集成需要大量样板代码
  - 用户难以插入自定义渲染步骤

**实际方案（新版）**：用 `SystemSet` 链代替节点图。
- 优点：
  - 渲染阶段就是普通 ECS System，完全利用现有调度基础设施
  - 用户可以像写 gameplay 系统一样写渲染系统（`Query`、`Res`、`RenderContext`）
  - 并行调度器自动识别无依赖的系统并行执行
  - `RenderScheduleOrder` 提供了 Schedule 级别的扩展点
- 代价：
  - 失去了显式的"节点图"可视化概念
  - 复杂的跨阶段依赖需要用 `before`/`after` 显式配置

**对比其他引擎**：
- **UE**：使用 `FRenderResource` + `FRDG`（Render Dependency Graph），仍然是显式图结构，但集成在 C++ 中而非 ECS。
- **chaos**：通常使用简单的函数调用链或自定义调度器，较少有通用的渲染阶段抽象。

### 5.2 设计决策：为什么命令缓冲区用 SystemBuffer 自动 flush？

**naive 方案**：每个渲染系统手动创建/提交 `CommandEncoder`。
- 优点：控制精确。
- 缺点：大量样板代码；容易忘记提交；并行系统不安全。

**实际方案**：`RenderContext` + `Deferred<RenderContextState>` + `SystemBuffer` 自动 flush。
- 优点：
  - 渲染系统只需调用 `render_context.begin_tracked_render_pass()`，无需关心编码器生命周期
  - 自动保证并行安全（每个系统独占编码器）
  - 命令缓冲区按系统拓扑顺序收集，保持 GPU 命令顺序与系统依赖一致
- 代价：
  - 隐式行为可能让新手困惑（"我的命令什么时候提交？"）
  - 每个系统结束时的 flush 可能产生多个小命令缓冲区，略微增加驱动开销

---

## 六、关联辐射（Context）

### 6.1 与上层模块的关系
- **bevy_app**：`SubApp` 和 `Schedule` 是渲染管线的基础。`RenderScheduleOrder` 模仿了 `MainScheduleOrder`。
- **bevy_ecs**：`SystemSet`、`Schedule::configure_sets`、`.chain()` 是阶段链的核心机制。
- **bevy_tasks**：`Render` Schedule 中的无依赖系统可以自动并行，由 `bevy_tasks` 的线程池执行。

### 6.2 与下层模块的关系
- **render_phase**：`Queue` 和 `PhaseSort` 阶段产生 `BinnedRenderPhase` / `SortedRenderPhase`，由 `Render` 阶段消费。
- **render_resource**：`PrepareResources` / `PrepareBindGroups` 阶段创建 `Buffer`、`Texture`、`BindGroup`。
- **view**：`CreateViews` / `PrepareViews` 阶段设置相机、视口、uniform。

### 6.3 跨引擎对照
| 维度 | Bevy (ECS Schedule) | UE (FRDG) |
|------|---------------------|-----------|
| 阶段组织 | SystemSet 链 | 显式 Render Dependency Graph |
| 并行性 | ECS 调度器自动并行 | 手动标记 `EPass` 并行性 |
| 用户扩展 | 加 System 到 SystemSet | 继承 `FRenderTarget` / 自定义 Pass |
| 命令管理 | `SystemBuffer` 隐式 flush | `FRHICommandList` 手动提交 |

### 6.4 设计亮点总结
1. **ECS-native 渲染**：渲染系统就是普通 System，无特殊语法或概念负担。
2. **懒创建编码器**：`RenderContext` 按需创建 `CommandEncoder`，避免空转。
3. **自动 flush 保证安全**：`SystemBuffer` 在系统边界隐式处理命令缓冲，天然线程安全。
4. **可扩展 Schedule 顺序**：`RenderScheduleOrder` 让插件可以在不修改核心代码的情况下插入新渲染阶段。

---

## 七、关键源码片段

### 7.1 Render 阶段链配置

> 文件：`src/lib.rs`，第 294~338 行

```rust
impl Render {
    pub fn base_schedule() -> Schedule {
        let mut schedule = Schedule::new(Self);
        schedule.configure_sets(
            (
                ExtractCommands, PrepareMeshes, CreateViews,
                Specialize, PrepareViews, Queue, PhaseSort,
                Prepare, Render, Cleanup, PostCleanup,
            ).chain(),
        );
        // ... 子链配置
        schedule
    }
}
```

### 7.2 render_system 帧驱动

> 文件：`src/renderer/mod.rs`，第 69~90 行

```rust
pub fn render_system(world: &mut World, state: &mut SystemState<Query<(&ViewTarget, &ExtractedCamera)>>) {
    world.run_schedule(RenderGraph);
    let render_device = world.resource::<RenderDevice>();
    let render_queue = world.resource::<RenderQueue>();
    let mut encoder = render_device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default());
    crate::view::screenshot::submit_screenshot_commands(world, &mut encoder);
    crate::gpu_readback::submit_readback_commands(world, &mut encoder);
    render_queue.submit([encoder.finish()]);
    // ... present
}
```

### 7.3 RenderContext 懒编码器

> 文件：`src/renderer/render_context.rs`，第 88~98 行

```rust
fn command_encoder(&mut self) -> &mut CommandEncoder {
    let render_device = self.0.render_device.clone()
        .expect("RenderDevice must be set before accessing command_encoder");
    self.0.command_encoder.get_or_insert_with(|| {
        render_device.create_command_encoder(&wgpu::CommandEncoderDescriptor::default())
    })
}
```

---

## 八、关联阅读

- [[Bevy-bevy_render-源码解析：RenderApp 与提取阶段]] — 数据如何从主世界进入渲染世界
- [[Bevy-bevy_render-源码解析：RenderPhase 与绘制排序]] — Queue 和 PhaseSort 阶段详解
- [[Bevy-bevy_render-源码解析：wgpu 后端抽象]] — RenderDevice、RenderQueue 和 wgpu 初始化
- [[Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]] — SystemSet 链式配置原理

---

## 九、索引状态

- **所属阶段**：第三阶段-渲染管线
- **对应索引条目**：`[[Bevy-bevy_render-源码解析：RenderGraph 与节点调度]]`（注：新版中已重构为 Schedule 驱动）
- **分析轮次**：第一轮 + 第二轮 + 第三轮全量
- **覆盖范围**：
  - ✅ RenderSystems 阶段定义与链式配置
  - ✅ RenderGraph Schedule（Begin/Render/Submit/Finish）
  - ✅ render_system 帧驱动流程
  - ✅ RenderContext 与命令缓冲区管理
  - ✅ RenderScheduleOrder 扩展机制
  - ✅ RenderDevice / RenderQueue / RenderInstance 资源抽象
"""

with open("Notes/Bevy/第三阶段-渲染管线/Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动.md", "w", encoding="utf-8") as f:
    f.write(note2)

print("Note 2 written, length:", len(note2))
PYEOF