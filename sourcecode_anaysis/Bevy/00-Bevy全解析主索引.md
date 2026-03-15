---
title: Bevy 全解析主索引
date: 2026-05-03
tags:
  - bevy-source
  - engine-architecture
  - rust
  - ecs
  - index
aliases:
  - Bevy全解析主索引
  - Bevy源码索引
---

# Bevy 全解析主索引

> 本系列计划对 Bevy 引擎进行逐层、逐 crate 的源码级架构解析。
>
> **原则**：从 ECS 核心到渲染管线，从底层基础设施到上层 Gameplay，从静态数据结构到动态 System 调度。全面覆盖 Bevy 的核心 crate 与关键子系统。
>
> **注意**：Bevy 是原生 ECS（Entity-Component-System）架构，分析时**不要以 OOP 思维寻找"类"**，而是关注 Component 是什么数据、System 如何访问 Query、Schedule 如何构建并行图、World 如何作为全局状态容器。

---

## 常用外部参考

- `<BEVY_SOURCE_ROOT>/` — Bevy 源码根（通过 `.engine-source-config` 配置）
- `crates/bevy_ecs/` — ECS 核心（Entity、Component、System、Schedule、World）
- `crates/bevy_app/` — App 生命周期、Plugin 系统
- `crates/bevy_render/` — 渲染核心与渲染图
- `crates/bevy_asset/` — 资源加载与管理
- `crates/bevy_reflect/` — Rust 反射系统
- `Cargo.toml` — workspace 定义与各 crate 依赖关系
- `examples/` — 官方示例代码

---

## 第一阶段：构建系统与 ECS 核心

> 目标：解析 Bevy 的 Cargo workspace 构建体系，以及 ECS 核心（bevy_ecs）的完整架构。

### 1.1 构建系统

| 状态  | 计划笔记                                            | Bevy crate/目录                       |
| --- | ----------------------------------------------- | ----------------------------------- |
| ✅   | [[Bevy-构建系统-源码解析：Cargo Workspace 与 Feature 体系]] | 根 `Cargo.toml`，各 crate `Cargo.toml` |
| ✅   | [[Bevy-构建系统-源码解析：bevy_internal 聚合与条件编译]]        | `crates/bevy_internal/`             |

### 1.2 ECS 核心（bevy_ecs）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-bevy_ecs-源码解析：World 与 Entity 生命周期]] | `crates/bevy_ecs/src/world/` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Component 存储与 Archetype]] | `crates/bevy_ecs/src/storage/`, `archetype.rs` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Query 与 SystemParam]] | `crates/bevy_ecs/src/system/`, `query/` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]] | `crates/bevy_ecs/src/schedule/` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Event 与 Commands 延迟执行]] | `crates/bevy_ecs/src/event.rs`, `system/commands/` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Resource 全局状态]] | `crates/bevy_ecs/src/system/resource.rs` |
| ✅ | [[Bevy-bevy_ecs-源码解析：Change Detection 与脏标记]] | `crates/bevy_ecs/src/change_detection.rs`, `query/state.rs` |

### 1.3 App 生命周期（bevy_app）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-bevy_app-源码解析：App 构建与 Plugin 系统]] | `crates/bevy_app/src/app.rs`, `plugin/` |
| ✅ | [[Bevy-bevy_app-源码解析：Startup 与 Update 调度]] | `crates/bevy_app/src/main_schedule.rs`, `app.rs` |
| ✅ | [[Bevy-bevy_app-源码解析：State 状态机与状态过渡]] | `crates/bevy_app/src/state/` |

---

## 第二阶段：基础层与反射系统

> 目标：解析 Bevy 的"标准库"层。理解反射、句柄、任务、工具等最底层基础设施。

### 2.1 反射系统（bevy_reflect）

| 状态  | 计划笔记                                           | Bevy crate/目录                                        |
| --- | ---------------------------------------------- | ---------------------------------------------------- |
| ✅   | [[Bevy-bevy_reflect-源码解析：Reflect trait 与动态类型]] | `crates/bevy_reflect/src/reflect.rs`, `type_info.rs` |
| ✅   | [[Bevy-bevy_reflect-源码解析：derive 宏与代码生成]]      | `crates/bevy_reflect/derive/`                        |
| ✅   | [[Bevy-bevy_reflect-源码解析：TypeRegistry 与序列化]]   | `crates/bevy_reflect/src/type_registry.rs`, `serde/` |

### 2.2 资产与加载（bevy_asset）

| 状态  | 计划笔记                                          | Bevy crate/目录                                           |
| --- | --------------------------------------------- | ------------------------------------------------------- |
| ✅   | [[Bevy-bevy_asset-源码解析：AssetServer 与 Handle]] | `crates/bevy_asset/src/server.rs`, `handle.rs`          |
| ✅   | [[Bevy-bevy_asset-源码解析：AssetLoader 与加载管线]]    | `crates/bevy_asset/src/loader.rs`, `loader_builders.rs` |
| ✅   | [[Bevy-bevy_asset-源码解析：AssetEvents 与热重载]]     | `crates/bevy_asset/src/events.rs`, `processor.rs`       |
| ✅   | [[Bevy-bevy_asset-源码解析：Asset 依赖与标签]]          | `crates/bevy_asset/src/id.rs`, `label.rs`               |

### 2.3 任务与异步（bevy_tasks）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-bevy_tasks-源码解析：TaskPool 与并行计算]] | `crates/bevy_tasks/src/task_pool.rs` |
| ✅ | [[Bevy-bevy_tasks-源码解析：ComputeTaskPool 与 IO 线程]] | `crates/bevy_tasks/src/usages.rs` |

### 2.4 基础工具 crate

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_utils-源码解析：工具类型与便利宏]] | `crates/bevy_utils/src/` |
| ✅ | [[Bevy-bevy_log-源码解析：日志与 tracing 集成]] | `crates/bevy_log/src/` |
| ⬜ | [[Bevy-bevy_time-源码解析：Time 与 Timer]] | `crates/bevy_time/src/` |
| ⬜ | [[Bevy-bevy_diagnostic-源码解析：性能诊断与帧率]] | `crates/bevy_diagnostic/src/` |

---

## 第三阶段：渲染管线

> 目标：解析 Bevy 的渲染图、渲染管线、PBR、2D/3D 渲染后端。

### 3.1 渲染核心（bevy_render）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-bevy_render-源码解析：RenderApp 与提取阶段]] | `crates/bevy_render/src/lib.rs`, `RenderApp` |
| ✅ | [[Bevy-bevy_render-源码解析：Render Schedule 与渲染管线驱动]] | `crates/bevy_render/src/renderer/`, `src/lib.rs` |
| ✅ | [[Bevy-bevy_render-源码解析：RenderPhase 与绘制排序]] | `crates/bevy_render/src/render_phase/` |
| ✅ | [[Bevy-bevy_render-源码解析：View 与 Camera]] | `crates/bevy_render/src/camera/`, `view/` |
| ✅ | [[Bevy-bevy_render-源码解析：Mesh 与 Vertex 布局]] | `crates/bevy_render/src/mesh/`, `render_resource/` |
| ✅ | [[Bevy-bevy_render-源码解析：Texture 与 BindGroup]] | `crates/bevy_render/src/texture/`, `render_resource/` |
| ✅ | [[Bevy-bevy_render-源码解析：wgpu 后端抽象]] | `crates/bevy_render/src/renderer/` |

### 3.2 核心管线与后处理（bevy_core_pipeline / bevy_post_process / bevy_anti_alias）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-bevy_core_pipeline-源码解析：后处理与特效]] | `crates/bevy_core_pipeline/src/tonemapping/`, `crates/bevy_post_process/src/bloom/`, `crates/bevy_anti_alias/src/fxaa/` |

### 3.3 PBR 与材质（bevy_pbr）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_pbr-源码解析：PBR 材质与光照模型]] | `crates/bevy_pbr/src/` |
| ⬜ | [[Bevy-bevy_pbr-源码解析：阴影映射与级联]] | `crates/bevy_pbr/src/render/` |
| ⬜ | [[Bevy-bevy_pbr-源码解析：IBL 与环境光照]] | `crates/bevy_pbr/src/render/` |

### 3.4 2D 与特殊渲染

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_sprite-源码解析：Sprite 与 2D 渲染]] | `crates/bevy_sprite/src/` |
| ⬜ | [[Bevy-bevy_gizmos-源码解析：Gizmo 与调试绘制]] | `crates/bevy_gizmos/src/` |
| ⬜ | [[Bevy-bevy_text-源码解析：文字渲染与字形]] | `crates/bevy_text/src/` |

### 3.5 专题

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ✅ | [[Bevy-专题：粒子系统现状与 bevy_hanabi 架构]] | 社区 crate `bevy_hanabi` |

---

## 第四阶段：玩法运行时层

> 目标：解析输入、窗口、音频、UI、物理、动画等 Gameplay 运行时模块。

### 4.1 输入与窗口

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_input-源码解析：输入事件与 Action]] | `crates/bevy_input/src/` |
| ⬜ | [[Bevy-bevy_window-源码解析：窗口管理与事件]] | `crates/bevy_window/src/` |
| ⬜ | [[Bevy-bevy_winit-源码解析：winit 后端适配]] | `crates/bevy_winit/src/` |

### 4.2 变换与层级

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_transform-源码解析：Transform 与 GlobalTransform]] | `crates/bevy_transform/src/components/` |
| ⬜ | [[Bevy-bevy_transform-源码解析：层级传播与脏更新]] | `crates/bevy_transform/src/systems.rs` |
| ⬜ | [[Bevy-bevy_hierarchy-源码解析：Parent-Child 关系]] | `crates/bevy_hierarchy/src/` |

### 4.3 UI 系统（bevy_ui）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_ui-源码解析：UI 节点与布局（Flexbox）]] | `crates/bevy_ui/src/` |
| ⬜ | [[Bevy-bevy_ui-源码解析：UI 渲染与材质]] | `crates/bevy_ui/src/render/` |

### 4.4 音频（bevy_audio）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_audio-源码解析：音频播放与空间化]] | `crates/bevy_audio/src/` |

### 4.5 场景与 GLTF

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_scene-源码解析：Scene 序列化与反序列化]] | `crates/bevy_scene/src/` |
| ⬜ | [[Bevy-bevy_gltf-源码解析：GLTF 加载与场景构建]] | `crates/bevy_gltf/src/` |

---

## 第五阶段：工具链与跨平台

> 目标：解析 Bevy 的辅助工具、跨平台抽象、开发工作流支撑。

### 5.1 图像与着色器（bevy_image / bevy_shader）

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_image-源码解析：图像解码与格式支持]] | `crates/bevy_image/src/` |
| ⬜ | [[Bevy-bevy_render-源码解析：Shader 与 WGSL]] | `crates/bevy_render/src/render_resource/shader.rs` |

### 5.2 平台抽象

| 状态 | 计划笔记 | Bevy crate/目录 |
|------|---------|----------------|
| ⬜ | [[Bevy-bevy_platform-源码解析：跨平台适配层]] | `crates/bevy_platform_support/` 或相关 |

---

## 第六阶段：跨领域专题深度解析

> 目标：横向打通 Bevy 多个 crate，分析贯穿引擎的核心机制，提取可迁移的通用工程原理。同时与 UE/chaos 进行跨引擎对照。

| 状态 | 计划笔记 | 涉及 Bevy crate/路径 |
|------|---------|---------------------|
| ⬜ | [[Bevy-专题：ECS 内存布局与 Archetype 演进]] | `bevy_ecs/storage/` → `bevy_ecs/archetype.rs` |
| ⬜ | [[Bevy-专题：一帧的生命周期（App Update → Render）]] | `bevy_app` → `bevy_ecs/schedule` → `bevy_render/RenderApp` → `wgpu` |
| ⬜ | [[Bevy-专题：System 并行调度与依赖图]] | `bevy_ecs/schedule/` → `bevy_tasks` |
| ⬜ | [[Bevy-专题：资源加载全链路]] | `bevy_asset` → `bevy_render/Texture` → `bevy_pbr/Material` |
| ⬜ | [[Bevy-专题：渲染一帧的完整链路]] | `Camera` → `RenderGraph` → `RenderPhase` → `wgpu::CommandEncoder` → `Present` |
| ⬜ | [[Bevy-专题：反射与序列化全链路]] | `bevy_reflect` → `bevy_scene` → `bevy_gltf` |
| ⬜ | [[Bevy-专题：输入 → UI → 渲染事件链]] | `bevy_winit` → `bevy_input` → `bevy_ui` → `bevy_render` |
| ⬜ | [[Bevy-专题：变换层级与脏传播]] | `bevy_hierarchy` → `bevy_transform` → `bevy_render/View` |
| ⬜ | [[Bevy-专题：Bevy 整体骨架与 crate 组合]] | 跨全部核心 crate |
| ⬜ | [[Bevy-专题：跨引擎 ECS 架构对照（Bevy vs chaos/UE）]] | 跨引擎架构比较，通用工程原理提取 |

---

## 维护记录

- **2026-05-03**：创建 Bevy 全解析主索引，完成目录结构与六阶段规划。等待首次源码分析开始。
- **2026-05-03**：完成第一阶段 1.2 ECS 核心全部 7 篇笔记的三层剥离法分析与产出。
- **2026-05-03**：完成第一阶段 1.3 App 生命周期（bevy_app）全部 3 篇笔记的三层剥离法分析与产出。
- **2026-05-06**：完成第一阶段 1.1 构建系统全部 2 篇笔记的三层剥离法分析与产出。第一阶段全部笔记已完成。
- **2026-05-06**：完成第二阶段 2.2 资产与加载（bevy_asset）全部 4 篇笔记的三层剥离法分析与产出。
- **2026-05-06**：完成第二阶段 2.1 反射系统（bevy_reflect）全部 3 篇笔记的三层剥离法分析与产出。
- **2026-05-14**：完成第二阶段 2.3 任务与异步（bevy_tasks）全部 2 篇笔记的三层剥离法分析与产出。
- **2026-05-14**：完成第三阶段 3.1 渲染核心（bevy_render）全部 7 篇笔记的三层剥离法全量分析与产出。

---

> [[索引/知识总索引|← 返回 知识总索引]]
