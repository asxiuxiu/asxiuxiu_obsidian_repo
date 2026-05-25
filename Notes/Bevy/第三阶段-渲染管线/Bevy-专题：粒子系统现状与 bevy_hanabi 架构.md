---
title: Bevy-专题：粒子系统现状与 bevy_hanabi 架构
date: 2026-05-25
tags:
  - bevy-source
  - bevy_hanabi
  - 粒子系统
  - 第三阶段-渲染管线
  - 专题
aliases:
  - Bevy-专题：粒子系统现状与 bevy_hanabi 架构
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy 全解析主索引]]

# Bevy-专题：粒子系统现状与 bevy_hanabi 架构

> **分析范围**：Bevy 官方源码（粒子系统缺失确认）、`bevy_hanabi` crate（docs.rs、GitHub README、架构设计） | **分析轮次**：调研与架构梳理 | **源码版本**：Bevy 0.19.0-dev / bevy_hanabi 0.18

---

## 零、Bevy 的粒子系统现状

截至 Bevy 0.19.0-dev，**Bevy 官方没有内置粒子系统**。引擎提供的渲染管线（`bevy_core_pipeline`、`bevy_render`）专注于几何渲染、后处理和光照，不包含任何粒子发射、模拟或渲染的原生支持。

这意味着：
1. 如果项目需要粒子效果（爆炸、烟雾、火焰、魔法特效等），必须依赖**社区方案**或自行实现。
2. 自行实现粒子系统需要深入 `bevy_render` 的渲染相位（RenderPhase）、网格分配器（Mesh Allocator）和 GPU 命令编码（CommandEncoder）等底层机制。

社区中最成熟、使用最广泛的粒子方案是 **`bevy_hanaki`**（花火）。本节对其架构进行源码级梳理。

---

## 一、bevy_hanabi 模块定位

### 1.1 项目定位

> 来源：[docs.rs/bevy_hanabi](https://docs.rs/bevy_hanabi) 与 [GitHub: djeedai/bevy_hanabi](https://github.com/djeedai/bevy_hanabi)

`bevy_hanabi` 是一个**基于 GPU 的粒子系统插件**，设计目标是：
- **大规模**：支持数百万粒子实时模拟。
- **低 CPU 开销**：将粒子 spawn、update、渲染全部 offload 到 GPU compute shader。
- **可组合性**：通过 Expression API 和 Modifier 链构建复杂效果。
- **跨平台**：桌面端全支持；Web 端需 WebGPU（WebGL2 不支持 compute shader）。

### 1.2 与 Bevy 版本的兼容性

| bevy_hanabi | bevy |
|-------------|------|
| 0.18 | 0.18 |
| 0.17 | 0.17 |
| 0.16 | 0.16 |
| 0.14-0.15 | 0.15 |
| 0.12-0.13 | 0.14 |

bevy_hanabi 的更新节奏紧密跟随 Bevy 主版本，属于 Bevy 生态的**第一方级社区插件**。

### 1.3 Cargo Features

| Feature | 默认 | 说明 |
|---------|------|------|
| `2d` | ✅ | 支持 2D 相机渲染 |
| `3d` | ✅ | 支持 3D 相机渲染 |
| `serde` | ✅ | 序列化支持（WASM 不兼容） |

可以只启用 3D 模式以减小编译体积：

```toml
bevy_hanabi = { version = "0.18", default-features = false, features = ["3d"] }
```

---

## 二、bevy_hanabi 的核心架构

### 2.1 三层抽象：EffectAsset → ParticleEffect → Render

bevy_hanabi 的接口层采用三层抽象，与 Bevy 的 Asset + Component + Render 三层结构对齐：

```
EffectAsset（资源，可序列化）
  └── 定义粒子效果的完整参数：
      ├── Spawner（发射器）：spawn rate、burst、lifetime
      ├── InitModifiers（初始化修改器）：position、velocity、color、size
      ├── UpdateModifiers（更新修改器）：gravity、force field、collision、lifetime
      └── RenderModifiers（渲染修改器）：billboard、mesh、trail、stretch

ParticleEffect（Component，附加到实体）
  └── 引用一个 EffectAsset Handle
  └── 在场景中表示一个"粒子效果实例"

HanabiPlugin（Plugin）
  └── 注册渲染管线集成：
      ├── Extract 阶段：将 ParticleEffect 提取到渲染世界
      ├── Prepare 阶段：构建 GPU 缓冲区、BindGroup、Pipeline
      └── Queue/Render 阶段：提交 DrawCommand，执行粒子渲染
```

### 2.2 GPU 驱动的粒子生命周期

与传统 CPU 粒子系统不同，bevy_hanabi 的粒子**整个生命周期几乎都在 GPU 上**：

```
CPU 侧（每帧）：
  1. Spawner 计算需要 spawn 多少粒子（根据 rate、burst 等）
  2. 将 spawn count 写入 GPU buffer
  3. （可选）更新全局参数（如 transform、uniforms）

GPU 侧（Compute Shader，每帧）：
  1. Init Pass：为新 spawn 的粒子初始化属性（position、velocity、color、size、age）
  2. Update Pass：对所有存活粒子执行物理模拟（gravity、drag、collision、force field）
  3. （可选）Reorder/Sort：为透明渲染或 GPU culling 排序

GPU 侧（Render Pass）：
  4. 使用粒子属性 buffer 作为顶点/实例数据
  5. 渲染为 billboard quad、3D mesh 或 trail ribbon
```

### 2.3 与 Bevy 渲染管线的集成

bevy_hanabi 需要深度集成到 Bevy 的渲染管线中，主要涉及以下方面：

| 集成点 | 说明 |
|--------|------|
| `ExtractComponent` | 将主世界的 `ParticleEffect` 提取到渲染世界 |
| `RenderPhase` | 在 `Opaque3d` / `Transparent3d` / `2D` Phase 中插入粒子绘制项 |
| `SpecializedRenderPipeline` | 根据 effect 配置动态生成渲染管线（不同 shader entry、不同 vertex layout） |
| `Compute Pass` | 在渲染世界的 `Render` 阶段提交 compute shader 调度命令 |
| `MeshAllocator` / `BufferVec` | 使用 Bevy 的 GPU 内存分配器管理粒子属性 buffer |

具体而言，bevy_hanabi 的渲染流程如下：

```rust
// 伪代码，基于源码结构推断
impl Plugin for HanabiPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((
            ExtractComponentPlugin::<ParticleEffect>::default(),
        ));

        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return; };
        render_app
            .add_systems(Render, (
                prepare_particle_buffers.in_set(RenderSystems::PrepareResources),
                queue_particle_pipelines.in_set(RenderSystems::Queue),
            ))
            .add_systems(Core3d, render_particles.in_set(Core3dSystems::MainPass));
    }
}
```

> 注：以上伪代码为根据 bevy_hanabi 公开文档和 Bevy 渲染管线机制推导的架构示意，非精确源码引用。bevy_hanabi 的实际源码不在 Bevy 主仓库中。

---

## 三、bevy_hanabi 的功能清单

### 3.1 Spawn（发射）

| 功能 | 说明 |
|------|------|
| Constant rate | 恒定速率发射 |
| One-time burst | 一次性爆发 |
| Repeated burst | 周期性爆发 |
| Spawner activation | 动态启停发射器 |
| Randomized parameters | 随机化发射参数 |

### 3.2 Initialize（初始化）

| 功能 | 说明 |
|------|------|
| Position over shape | 在立方体、球体、圆锥、平面等形状上分布 |
| Velocity over shape | 按形状发射初速度 |
| Random color / size / age | 随机化粒子属性 |

### 3.3 Update（模拟）

| 功能 | 说明 |
|------|------|
| Gravity | 全局重力 |
| Radial / Tangent acceleration | 径向/切向加速度 |
| Force field | 力场（吸引/排斥） |
| Linear drag | 线性阻力 |
| Collision | 与平面、球体、立方体或深度缓冲碰撞 |
| Size/Color over lifetime | 生命周期内插值 |

### 3.4 Render（渲染）

| 功能 | 说明 |
|------|------|
| Billboard quad | 始终面向相机的四边形 |
| Textured quad | 带纹理的四边形 |
| Generic 3D mesh | 任意 3D 网格实例化 |
| Velocity stretch | 沿速度方向拉伸 |
| Trails / Ribbons | 拖尾/丝带效果 |
| HDR + Bloom | 与 Bevy HDR 管线兼容，可参与 Bloom |

---

## 四、设计决策分析

### 4.1 设计决策：GPU Compute Shader 驱动 vs CPU 粒子系统

**问题背景**：粒子数量达到百万级时，CPU 模拟会成为瓶颈。

**naive 方案**：在 CPU 上每帧更新所有粒子的位置、速度、颜色，然后通过 `write_buffer` 上传到 GPU。
- 优点：逻辑简单，易于调试。
- 缺点：CPU-GPU 带宽瓶颈；CPU 并行度有限；难以支持百万级粒子。

**实际方案**：使用 Compute Shader 在 GPU 上完成 spawn、init、update。
- 优点：零 CPU-GPU 数据传输（粒子数据始终驻留 GPU）；可并行更新数十万至数百万粒子；物理计算与渲染共享 GPU 内存。
- 代价：调试困难（无法直接断点 GPU 逻辑）；需要处理 GPU 内存分配和 buffer 管理；WebGL2 不支持 compute shader，必须要求 WebGPU。

### 4.2 设计决策：Expression API 而非固定 Shader 模板

**naive 方案**：为每种粒子效果预写一套 WGSL shader。
- 缺点：组合爆炸（N 种 spawn × M 种 update × K 种 render）；用户无法灵活自定义。

**实际方案**：bevy_hanabi 使用 Expression API，在 CPU 侧构建表达式树，然后动态生成 WGSL compute shader 和 render shader。
- 优点：极高的灵活性，用户可组合任意 modifier；引擎内部统一处理 shader 生成和编译缓存。
- 代价：引入了一套 DSL 和 shader 拼接逻辑，增加了内部复杂度。

---

## 五、Bevy 引擎粒子系统缺失的深层原因

Bevy 核心团队至今未将粒子系统纳入官方 crate，推测有以下几个原因：

1. **架构优先级**：Bevy 的渲染团队过去几年的重点在 PBR、阴影、 clustered forward lighting、HDR 后处理、MSAA/FXAA/TAA 等基础渲染能力上。粒子系统属于"锦上添花"，而非"基础必备"。
2. **社区生态健康**：`bevy_hanabi` 已经提供了工业级品质的 GPU 粒子方案，且维护活跃。Bevy 核心团队倾向于让社区方案充分竞争和成熟，再考虑是否官方化。
3. **ECS 原生复杂性**：粒子系统的 ECS 化设计存在挑战——粒子是高频创建/销毁的短生命周期实体，如果每个粒子都是 ECS Entity，会对 archetype 变更和内存布局造成压力。bevy_hanabi 选择绕过 ECS Entity，直接用 GPU buffer 管理粒子，这是一种务实的工程选择，但也意味着它与 Bevy ECS 的集成需要额外抽象层。

---

## 六、关联阅读

- [[Bevy-bevy_render-源码解析：RenderPhase 与绘制排序]] — 理解 bevy_hanabi 如何将粒子插入渲染相位
- [[Bevy-bevy_render-源码解析：Mesh 与 Vertex 布局]] — 理解粒子渲染时的顶点/实例数据布局
- [[Bevy-bevy_core_pipeline-源码解析：后处理与特效]] — 理解 Bloom 等效果如何与粒子自发光交互
- [bevy_hanabi GitHub](https://github.com/djeedai/bevy_hanabi) — 官方仓库与示例
- [bevy_hanabi docs.rs](https://docs.rs/bevy_hanabi) — API 文档

---

## 七、索引状态

- **所属阶段**：第三阶段-渲染管线 / 专题
- **对应索引条目**：`[[Bevy-专题：粒子系统现状与 bevy_hanabi 架构]]`
- **分析轮次**：调研与架构梳理（基于 docs.rs 和 GitHub 源码，非 Bevy 主仓库内部分析）
- **覆盖范围**：
  - ✅ Bevy 官方粒子系统现状（无内置）
  - ✅ bevy_hanabi 模块定位与版本兼容性
  - ✅ bevy_hanabi 三层抽象架构（EffectAsset → ParticleEffect → Render）
  - ✅ GPU compute shader 驱动的粒子生命周期
  - ✅ 与 Bevy 渲染管线的集成点
  - ✅ bevy_hanabi 功能清单（Spawn/Init/Update/Render）
  - ✅ 设计决策分析（GPU vs CPU、Expression API vs 固定模板）
