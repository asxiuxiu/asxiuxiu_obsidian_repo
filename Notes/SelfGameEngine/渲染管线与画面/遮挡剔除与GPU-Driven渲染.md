---
order: 35
title: 遮挡剔除与 GPU-Driven 渲染
date: 2026-05-28
tags:
  - self-game-engine
  - rendering
  - occlusion-culling
  - hzb
  - gpu-driven
  - meshlet
  - visibility-buffer
aliases:
  - 遮挡剔除与GPU-Driven渲染
---

> **前置依赖**：[[视锥剔除与空间加速结构]]、[[ECS架构下的渲染世界设计]]
> **本模块增量**：掌握遮挡剔除的核心原理（HZB 两遍剔除）、GPU-Driven 渲染的完整技术分级（L1~L5）、以及可见性系统在 ECS 架构中的工业级表达。读完这篇笔记后，你将能为引擎建立**从 CPU 剔除到 GPU-Driven 的渐进演进路径**，并理解 Nanite 级虚拟几何的底层逻辑。
>
> 本笔记探讨的核心问题是：**物体在相机视野内，但被前面的墙挡住了——CPU 如何知道"不用画它"？当场景规模大到 CPU 遍历本身成为瓶颈时，如何把剔除工作搬到 GPU？**

---

## 问题 6：遮挡剔除——物体在视锥内，但被前面的墙挡住了，怎么办？

### 场景与根因

想象玩家站在一条室内走廊里，走廊两侧各有 5 个房间。相机朝向走廊尽头，视锥体覆盖了走廊和两侧的房间门。按照上篇的视锥剔除，这 11 个空间区域（走廊 + 10 个房间）全部在视野内，因此所有房间内的家具、装饰、NPC 都会被 CPU 提交给 GPU。

但实际上，玩家只能看见走廊本身和最近的一两个房间深处。走廊中间有一堵承重墙，完全挡住了房间 3~5 的视线；走廊的转角挡住了房间 6~10 的视线。这些被挡住的房间里可能有 500 个实体、20 万三角形——它们被视锥剔除保留，被 GPU 的 Early-Z 丢弃，但 CPU 已经为它们付出了遍历、绑定、命令录制的全部开销。

**根因在于：视锥剔除只回答了"在不在相机视野范围内"，没有回答"前面有没有东西挡住"。** 这两者是正交的问题，必须分别解决。

### 分支研究

#### 分支 A：软件遮挡查询（Software Occlusion Culling）

**核心思路**：在 CPU 上用极简的光栅化器，把场景中的"遮挡物"（Occluder，如墙壁、大地形块）渲染到一个低分辨率的深度缓冲中。然后用这个 CPU 端深度缓冲测试被遮挡物体的包围盒。

```cpp
// 概念性伪代码
void SoftwareOcclusionCulling() {
    // 1. 把大型遮挡物光栅化到 256x144 的 CPU 深度缓冲
    RasterizeOccluders(cpu_depth_buffer, large_walls_and_terrain);
    
    // 2. 对被遮挡候选物做测试
    for (const auto& candidate : occludees) {
        if (IsOccludedByDepthBuffer(cpu_depth_buffer, candidate.aabb)) {
            // 被遮挡，丢弃
        } else {
            visible.push_back(candidate.entity);
        }
    }
}
```

**适用场景**：移动端或低端 CPU（没有高效的 GPU Compute）、或需要与 CPU 剔除管线紧密集成的场景。Intel 的"Software Occlusion Culling"库是此方案的著名实现。

**隐藏代价**：
- **CPU 光栅化开销大**。即使是 256x144 的分辨率，对复杂遮挡物做软件光栅化也需要大量 SIMD 优化才能不成为瓶颈。
- **遮挡物选择困难**。哪些物体应该被当作"遮挡物"？如果选少了，剔除率低；如果选多了，光栅化开销反而超过收益。
- **维护成本高**。软件光栅化器是 mini 渲染器，需要处理三角形裁剪、深度插值、背面剔除等。

**失效条件**：桌面级平台有 Compute Shader 支持时，软件光栅化器通常是过度设计。

#### 分支 B：硬件遮挡查询（Hardware Occlusion Query）

**核心思路**：利用 GPU 的 `GL_ARB_occlusion_query` / D3D `OcclusionQuery` 机制。对疑似被遮挡的物体，先渲染其包围盒（或不渲染只查询深度），GPU 返回"通过了深度测试的像素数"。如果像素数为 0，说明该物体完全被遮挡。

```cpp
// 概念性伪代码
for (const auto& candidate : occludees) {
    BeginOcclusionQuery(query_id);
    DrawAABBWireframe(candidate.aabb); // 只写深度，不写颜色
    EndOcclusionQuery(query_id);
    
    // 下一帧（或几帧后）读取结果
    uint64_t visible_pixels = GetQueryResult(query_id);
    if (visible_pixels > 0) {
        visible.push_back(candidate.entity);
    }
}
```

**适用场景**：需要精确知道"某个物体是否被遮挡"，且不介意 1~3 帧延迟的场景。

**隐藏代价**：
- **延迟高**。GPU 查询结果通常需要 1~3 帧才能回读到 CPU（因为 GPU 流水线深度）。这意味着如果玩家快速转向，新出现的物体会延迟 1~3 帧才被发现——表现为"物体突然弹出"。
- **CPU-GPU 往返开销**。每次查询都需要 CPU 发命令、GPU 执行、CPU 读回，破坏了 CPU 和 GPU 的并行流水线。
- **逐物体查询不可扩展**。1000 个候选物就需要 1000 个查询对象，驱动层的查询管理开销巨大。

**失效条件**：对延迟敏感的场景（竞技游戏、VR）、或候选物数量 > 100。

#### 分支 C：HZB（Hierarchical Z-Buffer）遮挡剔除

**核心思路**：利用上一帧（或本帧早期）渲染的深度缓冲，构建一个**层级深度图（Hierarchical Z-Buffer）**。HZB 是深度缓冲的 Mipchain——每一级是上一级的 2×2 像素取最小深度（或最大深度，取决于比较方向）。然后，用 HZB 在 CPU 或 GPU 上快速判断"一个包围盒是否完全被已知的最前端深度挡住"。

**适用场景**：现代桌面平台、D3D12/Vulkan、Compute Shader 可用。工业级标准方案。

**隐藏代价**：
- 需要维护上一帧的深度缓冲和 HZB Mipchain，增加显存占用（约 1/3 的原始深度缓冲额外开销）。
- 快速移动相机时，上一帧 HZB 可能不准确，导致本帧遗漏一些新出现的物体（需要两遍剔除策略解决）。
- 实现复杂度高于视锥剔除。

**这是下篇的核心，问题 7 将深入其数学原理。**

### 决策分析与推荐

**默认推荐：分支 C（HZB 遮挡剔除）为工业级默认路径，分支 B（硬件查询）仅在特殊 fallback 场景使用，分支 A 不推荐。**

理由：
1. **HZB 是 UE5 Nanite 和几乎所有现代高端引擎的遮挡剔除核心**。它结合了 GPU 的并行计算能力和图像金字塔的空间层级，在效率上远超软件光栅化和硬件查询。
2. **硬件遮挡查询的延迟问题无法根除**。1~3 帧的延迟对于 60fps 游戏是不可接受的（16~48ms 的弹出时间）。HZB 的延迟是 1 帧（使用上一帧深度），且可以通过两遍策略进一步降低。
3. **软件光栅化在 2020 年代已失去竞争力**。Compute Shader 的普及让"在 GPU 上做遮挡测试"比"在 CPU 上模拟 mini 光栅化器"更简单、更快、更精确。

**遗留问题**：HZB 听起来很神奇，但"用上一帧的深度图判断本帧的可见性"具体怎么操作？一个三维包围盒怎么和一张二维深度图比较？如果相机在转动，上一帧的深度还有参考价值吗？

---

## 问题 7：HZB 遮挡剔除的具体原理是什么？

### 场景与根因

假设上一帧渲染完成后，我们有一张 1920×1080 的深度缓冲。对于本帧的一个候选物体（如走廊尽头的椅子），它的 AABB 在世界空间中是一个长方体。我们需要回答：这个长方体是否被上一帧已经渲染的像素完全挡住？

最 naive 的做法是：把 AABB 的 8 个角点投影到屏幕空间，找到它们在深度缓冲中对应的像素，逐个比较深度值。如果所有角点的深度都大于深度缓冲中的值，则物体被遮挡。

**这个方法有三个致命缺陷**：
1. AABB 投影到屏幕后可能覆盖数千个像素，逐像素比较太慢。
2. 我们只测试了 8 个角点——即使 8 个角点都被遮挡，AABB 的边或面仍可能穿过遮挡缝隙（ conservative test 不成立）。
3. 深度缓冲是 "pixel-perfect" 的，对于远处的微小物体，单个像素的深度噪声就会导致误判。

**根因在于：我们需要一种"多尺度"的深度表示，既能快速覆盖大面积区域，又能在小尺度上保持足够的深度精度。** 这就像图像处理中的高斯金字塔——顶层看全局，底层看细节。

### 关键机制：HZB Mipchain 的构建

HZB 是深度缓冲的 **Min-Mipchain**（或 Max-Mipchain，取决于比较方向）。构建方式：

```
Level 0 (原始深度): 1920 x 1080, 每个像素存储最近深度
Level 1:             960 x 540,   每 2x2 像素取最小深度
Level 2:             480 x 270,   每 2x2 像素取最小深度
Level 3:             240 x 135
...
Level N:             ~1 x 1
```

```cpp
// Compute Shader 构建 HZB（概念性 HLSL）
[numthreads(8, 8, 1)]
void BuildHZB(uint3 id : SV_DispatchThreadID) {
    uint2 dst_coord = id.xy;
    uint2 src_coord = dst_coord * 2;
    
    float d0 = depth_texture.Load(int3(src_coord, 0));
    float d1 = depth_texture.Load(int3(src_coord + int2(1,0), 0));
    float d2 = depth_texture.Load(int3(src_coord + int2(0,1), 0));
    float d3 = depth_texture.Load(int3(src_coord + int2(1,1), 0));
    
    // 取最小深度（ conservative —— 最小深度代表"最前端"）
    hzb_texture[dst_coord] = min(min(d0, d1), min(d2, d3));
}
```

**关键洞察**：在每一级 mip 上，像素的值代表了对应 2×2 区域内**最近的深度**。这意味着，如果我们要测试一个覆盖了 32×32 像素的包围盒，不需要读 1024 个像素——只需读 HZB Level 5（约 60×34）中对应 1~2 个像素即可。

### 状态变化图：HZB 的保守性原理

```
原始深度缓冲（Level 0）:
┌───┬───┬───┬───┐
│ 5 │ 3 │ 8 │ 9 │    数字 = 深度值（越小越近）
├───┼───┼───┼───┤
│ 2 │ 1 │ 7 │ 6 │
├───┼───┼───┼───┤
│ 4 │ 10│ 12│ 11│
├───┼───┼───┼───┤
│ 15│ 14│ 13│ 16│
└───┴───┴───┴───┘

HZB Level 1 (2x2 min):
┌───┬───┐
│ 1 │ 6 │   ← (1=min(5,3,2,1), 6=min(8,9,7,6))
├───┼───┤
│ 4 │ 11│   ← (4=min(4,10,15,14), 11=min(12,11,13,16))
└───┴───┘

HZB Level 2 (继续 2x2 min):
┌───┐
│ 1 │   ← 整个区域的最小深度 = 最前端深度
└───┘

保守性保证：
- 如果物体深度 < HZB 对应像素值 → 物体一定在最前端某处之前 → 可见
- 如果物体深度 > HZB 对应像素值 → 物体可能在已知最前端之后 → 被遮挡（保守估计）
```

**为什么取最小深度是 "conservative"？**

因为 HZB 的用途是**剔除被遮挡的物体**。如果一个物体的深度大于 HZB 的值，说明"已知的最前端深度比物体更近"，物体**可能**被遮挡——我们保守地认为它被遮挡并丢弃。这种判断可能会误杀一些实际上从缝隙中可见的物体（False Positive），但绝不会漏掉真正可见的物体（False Negative）。对渲染来说，False Positive 只是多剔除几个（可能偶尔可见性保守），False Negative 则是画面错误（物体消失）——所以保守性是必须的。

### 用 HZB 测试一个 AABB

步骤如下：

1. **投影 AABB 到屏幕空间**，计算其在屏幕上的矩形包围盒（Screen Space AABB）。
2. **根据矩形大小选择 mip 级别**。如果矩形是 64×64 像素，选择 HZB 中大约覆盖 4×4 像素的 mip 级（Level 4）。
3. **读取该 mip 级中对应 2×2 或 4×4 像素的最小深度**。
4. **比较候选物体的最近深度与 HZB 深度**。如果物体的最近深度 > HZB 深度 → 被遮挡。

```cpp
// CPU 或 GPU 上的 HZB 遮挡测试（概念性）
bool IsOccludedByHZB(const AABB& world_aabb, const Mat4& view_proj, const HZB& hzb) {
    // 1. 把 AABB 投影到 NDC 空间，计算屏幕包围矩形
    Rect screen_rect;
    float min_depth_ndc = ProjectAABBToScreen(world_aabb, view_proj, &screen_rect);
    
    // 2. 选择 mip 级别：让屏幕矩形在 HZB 中覆盖约 4x4 像素
    int mip_level = ChooseHZBMipLevel(screen_rect.width, screen_rect.height);
    
    // 3. 读取 HZB 对应区域的最小深度
    float hzb_min_depth = SampleHZBMinDepth(hzb, mip_level, screen_rect);
    
    // 4. 比较（注意深度比较方向，取决于你的投影是 0=near 1=far 还是 reverse-Z）
    // 假设使用 reverse-Z（0=far, 1=near）：
    // 物体的 min_depth_ndc 越大 → 越靠近相机
    // 如果物体的 min_depth_ndc < hzb_min_depth → 物体在已知最前端之后 → 被遮挡
    return min_depth_ndc < hzb_min_depth;
}
```

### 分支研究

#### 分支 A：单层 Z-Buffer 测试

**核心思路**：不使用 HZB Mipchain，而是直接用原始深度缓冲做测试。

**适用场景**：快速原型、教学理解。

**隐藏代价**：
- 对于一个覆盖 100×100 像素的物体，需要读 10000 个像素并取最小值——太慢。
- 无法有效处理不同大小的物体。

**失效条件**：任何性能敏感场景。

#### 分支 B：HZB Mipchain 单遍测试

**核心思路**：只用上一帧的 HZB 测试本帧所有候选物体。

**适用场景**：相机移动缓慢、场景静态为主的场景。

**隐藏代价**：
- **相机快速转动时的漏判**。如果玩家快速右转，上一帧 HZB 的左侧深度无法反映本帧右侧的新物体。这些新物体会被误判为"被上一帧深度遮挡"而剔除，导致**物体消失**（这是不可接受的错误）。

**失效条件**：相机可能快速移动或转动的场景（几乎所有游戏）。

#### 分支 C：两遍剔除（Two-Pass Occlusion Culling）

**核心思路**：这是 UE5 Nanite 和高端引擎采用的标准策略。

**Pass 1**：用 **Previous Frame HZB**（上一帧的深度缓冲构建的 HZB）测试所有候选物体。
- 通过测试的物体 → **确定为可见**，立即绘制。
- 被剔除的物体 → 放入"待确认"列表（可能是被遮挡，也可能是上一帧 HZB 不准导致的误判）。

**绘制 Pass 1 的可见物体后，构建 Current Frame HZB**（从本帧已绘制内容的深度缓冲构建）。

**Pass 2**：用 **Current Frame HZB** 重新测试"待确认"列表中的物体。
- 此时 HZB 已经包含了本帧实际渲染的最前端深度。
- 通过测试的 → 绘制（这些是上一帧 HZB 漏掉、但本帧实际可见的物体）。
- 仍然被剔除的 → 真正被遮挡，丢弃。

```
时间线：

帧 N-1 结束 ──→ 深度缓冲 Depth[N-1] ──→ 构建 HZB[N-1]
                                              ↓
帧 N 开始 ─────→ Pass 1: 用 HZB[N-1] 测试所有候选
                    │
                    ├── 可见 ──→ 绘制（第一批）
                    │
                    └── 被剔除 ──→ 待确认列表
                                      │
                                      ↓
                              从第一批绘制结果
                              构建 HZB[N]（部分 HZB）
                                      │
                                      ↓
                              Pass 2: 用 HZB[N] 测试待确认列表
                                  │
                                  ├── 可见 ──→ 绘制（第二批，补漏）
                                  └── 被剔除 ──→ 真正丢弃
```

**适用场景**：大型开放世界、工业级引擎、Nanite 虚拟几何。

**隐藏代价**：
- **几乎跑两遍渲染管线**。虽然第二批次通常只占总物体的一小部分（< 10%），但管线切换（Pass 1 结束 → 构建 HZB → Pass 2 开始）增加了同步点和 GPU 空闲时间。
- **实现复杂**。需要维护"待确认列表"、两批绘制命令、HZB 构建的 Compute Shader、以及精确的时序控制。

**失效条件**：团队规模小、没有专门渲染程序员、或项目处于原型阶段。

### 引擎对照

> **参考：chaos / UE / Bevy 对「HZB 遮挡剔除」是怎么做的？**
>
> - **UE5 Nanite** 使用最激进的两遍剔除策略，但其细节更复杂。Nanite 不仅用 HZB 做遮挡剔除，还用 HZB 做 LOD 选择的保守估计。Nanite 的第一遍用 Previous HZB + Previous Transform 测试当前选中的 Cluster；第二遍用 Current HZB + Current Transform 测试被遮挡的 Cluster。由于 Nanite 的 Cluster 可能因流送（streaming）在帧之间进出内存，Nanite 还需要处理"上一帧可见的 Cluster 本帧已不在内存"的边界情况。
> - **UE 的传统渲染管线**（非 Nanite）在 `FSceneRenderer::Render` 中使用 `FOcclusionQuery` 或 HZB 做遮挡剔除。对于传统静态网格，UE 倾向于在 CPU 侧用上一帧的 HZB 做粗略剔除，然后依赖 GPU 的 Early-Z 处理剩余情况。
> - **Bevy** 在 0.19 版本中**没有内置 HZB 遮挡剔除**。Bevy 的可见性系统主要依赖 CPU 视锥剔除 + GPU Early-Z。这与 Bevy 的"跨平台优先"哲学一致——HZB 需要 Compute Shader 和 Mipmap 深度图，对某些 WebGL2 和移动端平台支持有限。但 Bevy 的架构预留了扩展点，用户可以通过自定义 Render Pass 和 Compute Shader 接入 HZB。

### 决策分析与推荐

**默认推荐：阶段 5 初期不实现 HZB，架构预留 Two-Pass HZB 接口；阶段 5 后期引入单遍 Previous-HZB 做粗略遮挡剔除；阶段 6+ 引入完整两遍剔除。**

理由如下：

1. **HZB 的实现复杂度远超视锥剔除**。它需要：Compute Shader 构建 Mipchain、深度图回读或 GPU 端测试、两遍管线的时序控制、以及处理相机快速移动的边界情况。对于阶段 5 初期的目标是"屏幕上出现带纹理的旋转立方体"，HZB 是过度设计。

2. **两遍剔除是工业级完备方案，但需要成熟的 RenderGraph 支撑**。UE5 的两遍剔除依赖 RenderGraph 精确管理 Pass 依赖和资源生命周期。在阶段 5 初期，如果 RenderGraph 尚未成熟，强行引入两遍剔除会增加不稳定性。

3. **单遍 Previous-HZB 是性价比最高的中期方案**。它只需要：每帧渲染结束后构建 HZB、下一帧的 Culling System 读取 HZB Texture 做 GPU 或 CPU 测试。虽然快速转动相机会有漏判，但可以通过降低 HZB 剔除的激进程度来缓解（如只对远距离大物体做 HZB 剔除）。

4. **Bevy 的"无 HZB"路径验证了一个事实**：对于中小规模场景，Early-Z 的像素级剔除足够高效。HZB 的回报曲线在实体数 > 5 万、或大量室内遮挡关系时才显著上升。

**具体实施策略**：

```cpp
// 阶段 5 预留的 Culling 接口
enum class CullingMode {
    FrustumOnly,           // 初期：只有视锥剔除
    FrustumPlusHZB,        // 中期：视锥 + 单遍 HZB
    TwoPassOcclusion       // 后期：两遍 HZB
};

struct VisibilityConfig {
    CullingMode mode = CullingMode::FrustumOnly;
    HZBHandle hzb_texture; // 当 mode >= FrustumPlusHZB 时有效
};

// CullingSystem 根据配置自动选择路径
void CullingSystem(
    Query<(Entity, &RenderAABB)> query,
    Res<ExtractedCamera> camera,
    Res<VisibilityConfig> config,
    ResMut<ViewVisibleList> visible
) {
    visible->clear();
    
    for (auto [e, aabb] : query) {
        if (!FrustumIntersectsAABB(camera->frustum, aabb))
            continue; // 视锥剔除
        
        if (config->mode >= CullingMode::FrustumPlusHZB) {
            if (IsOccludedByHZB(aabb, camera->view_proj, config->hzb_texture))
                continue; // HZB 遮挡剔除
        }
        
        visible->push_back(e);
    }
}
```

**遗留问题**：即使有了 HZB，如果场景有 10 万个实体，CPU 遍历这 10 万个实体做视锥测试 + HZB 查询本身仍可能成为瓶颈。CPU 的 O(n) 遍历有物理极限——当实体数量继续增长时，必须把剔除工作本身搬到 GPU。这就是 GPU-Driven 渲染的核心动机。

---

## 问题 8：GPU-Driven 剔除——什么时候该把剔除搬到 GPU？

### 场景与根因

你的引擎已经运行良好：5000 个实体，视锥剔除 + BVH 加速，CPU 开销约 0.5ms。但美术团队制作了一片森林——10 万棵独立实例化的树木，每棵树有独特的位置和缩放。BVH 遍历仍然只需要 0.3ms，但 **Extract 阶段** 复制 10 万个 `RenderTransform` 到 Render World 需要 1ms，**Queue 阶段** 遍历 10 万个可见实体生成 Phase Items 需要 2ms。CPU 总开销达到 3~4ms——帧率从 60fps 掉到 45fps。

更深层的问题是：即使 CPU 能把 10 万实体缩减到 5000 个可见实体，为这 5000 个实体生成 5000 个 Draw Call 仍然是巨大的开销。现代 GPU 可以并行处理数万个三角形，但 CPU 侧录制 5000 条绘制命令的串行开销是物理瓶颈。

**根因在于：CPU 的串行执行模型与大规模并行剔除+绘制任务之间存在根本性错配。** 当实体数量超过 CPU 的串行处理能力时，我们需要把"谁可见"和"怎么画"的决策权交给 GPU——GPU 的数千个并行核心更适合做这种大规模数据并行工作。

### GPU-Driven 的技术分级（L1 ~ L5）

GPU-Driven 不是单一技术，而是一个连续演进谱系。行业实践（参考 Maister's Graphics Adventures、Ubisoft SIGGRAPH 2015、UE5 Nanite）通常将其分为 5 级：

#### 分支 L1：Indirect Draw 实例化

**核心思路**：CPU 不做逐实体剔除，而是把所有实例数据（位置、缩放、旋转）打包成一个大的 Instance Buffer，用一次 `DrawInstanced` 或 `DrawIndirect` 绘制所有实例。GPU 的顶点着色器接收 `InstanceID`，从 Instance Buffer 中读取自己的变换矩阵。

```cpp
// CPU 侧：只做一次绘制调用
void RenderForest_GpuDriven_L1(CommandContext* ctx, GpuBuffer instance_buffer, uint32_t instance_count) {
    ctx->BindPipeline(tree_pipeline);
    ctx->BindVertexBuffer(tree_mesh.vb);
    ctx->BindIndexBuffer(tree_mesh.ib);
    // 一次性绘制所有实例，GPU 顶点着色器用 InstanceID 索引 instance_buffer
    ctx->DrawIndexedInstanced(tree_mesh.index_count, instance_count, 0, 0, 0);
}
```

**适用场景**：大量相同 mesh 的实例（如森林、草地、建筑群）。

**隐藏代价**：
- 所有实例共享同一个 PSO 和材质。如果 10 万棵树有 10 种不同的材质变体，L1 无法处理。
- CPU 仍然需要准备 Instance Buffer（上传 10 万个矩阵）。
- 没有剔除——所有实例都会被 GPU 处理，只是顶点着色器会做简单的视锥剔除（discard 不可见顶点）。但这仍然意味着 GPU 要遍历所有实例的顶点数据。

**失效条件**：实例数量极大（>100 万）、或材质差异大、或需要 LOD。

#### 分支 L2：GPU 剔除 + Indirect Draw

**核心思路**：CPU 只负责把**所有**实体的 AABB 和变换数据上传到 GPU。由 Compute Shader 并行执行视锥剔除和遮挡剔除，产出可见实例列表。然后用 `DrawIndirect` / `MultiDrawIndirect` 消费这个列表。

```cpp
// CPU 侧：只上传数据，不做剔除
void UploadCullingData(GpuBuffer culling_data_buffer, Span<InstanceData> instances) {
    culling_data_buffer->Upload(instances.data(), instances.size() * sizeof(InstanceData));
}

// GPU Compute Shader（概念性）
[numthreads(64, 1, 1)]
void CullInstances(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= instance_count) return;
    
    InstanceData inst = instance_buffer[idx];
    AABB world_aabb = TransformAABB(inst.local_aabb, inst.world_matrix);
    
    if (!FrustumIntersectsAABB(world_aabb, view_frustum)) return;
    if (IsOccludedByHZB(world_aabb, hzb_texture)) return;
    
    uint slot;
    InterlockedAdd(visible_count[0], 1, slot);
    visible_instances[slot] = idx;
}

// Graphics Shader 通过 DrawIndirect 读取 visible_instances
// indirect_args = { index_count_per_instance, visible_count, first_index, ... }
DrawIndirect(indirect_args_buffer);
```

**适用场景**：实例数量 > 1 万、有 Compute Shader 支持、相同 mesh 或少量 mesh 变体。

**隐藏代价**：
- 需要维护 GPU 端的场景数据缓冲（GPU Scene），增加显存占用（10 万实体 × 64 字节 = 6.4 MB，尚可接受）。
- 剔除后的 Instance Buffer 需要原子操作追加，GPU 上的原子竞争可能成为瓶颈（可通过 Wave/Subgroup 前缀和优化）。
- 仍然要求相同 mesh 才能合并成一个 Indirect Draw。如果场景有 500 种不同 mesh，需要 500 个 Indirect Draw 调用。

**失效条件**：mesh 种类极多、或需要每实例独特的材质参数（复杂材质变体）。

#### 分支 L3：Mesh Cluster + MultiDrawIndirect + Bindless

**核心思路**：把 mesh 切分为固定大小的**簇（Cluster/Meshlet，通常为 64~128 个三角形）**。每个簇有自己的 AABB。GPU 以簇为单位做剔除（视锥 + 遮挡 + 背面），然后通过 `MultiDrawIndirect` 批量提交多个 draw call。配合 Bindless Texture/Buffer（通过索引访问资源，而非传统绑定），可以大幅减少 PSO 切换。

```cpp
// 概念：每个 Cluster 的 GPU 数据结构
struct MeshCluster {
    uint32_t vertex_offset;
    uint32_t triangle_count;
    uint32_t material_id;     // Bindless 材质索引
    AABB     local_aabb;
};

// Compute Shader 以 Cluster 为单位剔除
[numthreads(64, 1, 1)]
void CullClusters(uint3 id : SV_DispatchThreadID) {
    uint cluster_idx = id.x;
    MeshCluster cluster = cluster_buffer[cluster_idx];
    
    AABB world_aabb = TransformAABB(cluster.local_aabb, instance_matrix);
    if (!FrustumIntersectsAABB(world_aabb, frustum)) return;
    if (IsOccludedByHZB(world_aabb, hzb)) return;
    
    // 追加到可见 cluster 列表
    // MultiDrawIndirect 的参数由每个 material bucket 分别生成
}
```

**适用场景**：高端桌面平台（D3D12/Vulkan）、需要极致 CPU 卸载的大型场景。Ubisoft《刺客信条：大革命》（2014）和《看门狗：军团》是此方案的先驱。

**隐藏代价**：
- **Mesh 切分是资产管线级别的改造**。导入模型时需要离线切分 cluster、构建 BVH、生成 LOD Chain。
- **Bindless 资源在旧平台不支持**。某些移动端 GPU 和 WebGL2 没有 `GL_EXT_descriptor_indexing` 或类似扩展。
- **材质系统需要重构**。传统按 PSO 分箱的渲染队列与 Bindless 的"单一 PSO + 材质索引"模型冲突。

**失效条件**：移动端、或资产管线无法支持 mesh 切分。

#### 分支 L4：Visibility Buffer + 软光栅（Nanite 路线）

**核心思路**：不直接渲染几何到 G-Buffer，而是先在 GPU 上用 Compute Shader 或软件光栅化器把"可见的簇 ID + 三角形 ID + 深度"写入一个 **Visibility Buffer**（64-bit per pixel：32-bit 深度 + 32-bit Cluster/Triangle ID）。然后通过后处理 Pass，根据 Visibility Buffer 的每个像素 ID 查找对应的材质和几何属性，写入 G-Buffer。

```cpp
// Visibility Buffer 渲染（概念性）
// Pass 1: GPU 剔除 + 光栅化到 Visibility Buffer
[numthreads(64, 1, 1)]
void RasterizeClusters(uint3 id : SV_DispatchThreadID) {
    // 每个线程处理一个 cluster
    // 用保守光栅化或软件光栅化把 cluster 的三角形写入 visibility buffer
    // 64-bit atomic max: depth (high 32) | cluster_id (low 32)
}

// Pass 2: 全屏 Pass，把 Visibility Buffer 解析为 G-Buffer
// 每个像素读取 cluster_id，查找顶点属性、材质 ID，执行着色
```

**适用场景**：UE5 Nanite、需要渲染数亿三角形的影视级场景。

**隐藏代价**：
- **工程复杂度极高**。Visibility Buffer 需要完整的软件光栅化器、64-bit Atomic 操作、复杂的属性查找和插值。
- **传统材质系统完全不兼容**。所有着色必须在 Visibility Buffer 解析阶段完成，无法使用传统的 Vertex/Fragment Shader 管线。
- **Fallback 路径性能损失大**。Nanite 在不支持 Mesh Shader 的硬件上需要把 meshlet 展开为非索引绘制，性能下降 5~15%（实测《Immortals of Aveum》）。

**失效条件**：任何非 UE5 级别资源投入的项目。

#### 分支 L5：Mesh Shader 硬件加速

**核心思路**：利用 NVIDIA Turing+ / AMD RDNA2+ / Intel Arc 的 Mesh Shader 管线。Task Shader 决定生成哪些 Meshlet，Mesh Shader 动态生成顶点/索引数据，直接送往光栅化器。

**适用场景**：高端桌面 GPU、需要硬件级 GPU-Driven 的项目。

**隐藏代价**：
- **硬件覆盖率低**。截至 2025 年，Mesh Shader 仅在高端桌面 GPU 支持，移动端和主机（除 Xbox Series X/S）基本不支持。
- API 限制。需要 D3D12 Ultimate 或 Vulkan 1.3 + `VK_EXT_mesh_shader`。

**失效条件**：跨平台项目、或目标硬件不支持 Mesh Shader。

### 状态变化图：从 CPU-Driven 到 GPU-Driven 的数据流变化

```
CPU-Driven（传统）:
CPU: 遍历所有实体 → 视锥测试 → 遮挡测试 → 排序 → 逐个 DrawCall
     ↑_________________________________________________↓
GPU: ←←←←←←←←←←←← 执行绘制 ←←←←←←←←←←←←←←←←←←←←←←←

GPU-Driven L2+:
CPU: 上传所有实体数据 ──────→ 只发 1~N 个 Indirect Draw
                               ↓
GPU: Compute Shader 剔除 ──→ Indirect Buffer ──→ Graphics Pipeline 绘制
      ↑______________________________________________↓
      （剔除和绘制全部在 GPU 内部完成，CPU 不参与逐实体决策）
```

### 决策分析与推荐

**默认推荐：阶段 5 初期不实现 GPU-Driven，架构预留 `CullingBackend` 接口；阶段 5 后期评估 L2（GPU Compute 剔除 + Indirect Draw）；L3+ 作为长期演进目标。**

理由如下：

1. **L2 是"性价比最高"的 GPU-Driven 切入点**。它不需要改造资产管线（无需 mesh 切分），不需要 Bindless 资源（可用传统绑定 + 按 PSO 分批次），只需要一个 Compute Shader 做剔除 + `DrawIndirect` 消费结果。对于 1~10 万实例的森林/城市，L2 能把 CPU 遍历开销从 2~3ms 降到 0.2ms 以下。

2. **L3 需要资产管线级别的配合**。Mesh Cluster 切分、LOD Chain 构建、Bindless 材质系统——这些不是渲染管线的"独立优化"，而是牵一发而动全身的架构改造。必须在材质系统、资产导入管线、GPU 资源管理都成熟后才能启动。

3. **L4/L5 是 Nanite 级别的专项工程**。UE5 的 Nanite 团队花了数年、数十人投入才达到生产级。个人/小团队不应该在阶段 5 追求 L4/L5，但应该理解其原理，以便在未来技术演进时做出正确决策。

4. **Bevy 的 GPU Preprocessing 是 L2 的 ECS 化表达**。Bevy 0.19 的 `GpuPreprocessingMode::Culling` 使用 Compute Shader 做视锥剔除并生成 indirect parameters，与我们的 L2 推荐完全一致。Bevy 将其作为 opt-in 优化而非默认路径，原因是跨平台兼容性——这与我们的"架构预留、按需开启"策略一致。

**ECS 映射**：
- `CullingBackend` Resource：枚举 `CpuDriven` / `GpuDrivenL2` / `GpuDrivenL3`。
- `GpuScene` Resource：包含 `GpuBuffer instance_data`、`GpuBuffer cluster_data`、`GpuBuffer indirect_args`。
- `CullingSystem`：根据 backend 类型，要么在 CPU 上遍历（`CpuDriven`），要么 Dispatch Compute Shader（`GpuDrivenL2`）。
- 关键设计：`VisibleList` Resource 在 `CpuDriven` 模式下是 `Array<Entity>`；在 `GpuDriven` 模式下是 `GpuBuffer` handle，下游 Queue System 通过 GPU 读取或回读（readback）消费。

**遗留问题**：无论是 CPU 剔除还是 GPU 剔除，最终都需要一个数据结构来承载"哪些实体可见"这个结果，并把它传递给下游的渲染队列（Queue）阶段。在 ECS 架构中，这个结果应该是什么形态？是每帧全量重建的数组，还是增量更新的列表？是每个视图独立一份，还是全局共享？

---

## 问题 9：在 ECS 架构下，可见性系统如何表达？

### 场景与根因

经过上篇和本篇的讨论，我们已经有了多种剔除策略：视锥剔除、BVH 加速、HZB 遮挡剔除、GPU-Driven Compute 剔除。但策略的多样性带来了一个架构问题：**下游的 Queue System 不应该关心可见性列表是怎么产出的。** 无论是 CPU 遍历、BVH 查询、还是 GPU Compute 原子追加，Queue System 只需要一份"当前视图下可见的实体列表"。

**根因在于：可见性判断是渲染管线的"内部优化"，它的实现应该被抽象隔离，不泄露到管线的其他阶段。** 这与 ECS "状态平铺、系统纯函数"的哲学一致——可见性结果是一份显式的状态数据，而不是隐含在遍历逻辑中的副作用。

### 分支研究

#### 分支 A：可见性结果作为全量数组（`VisibleList` Resource）

**核心思路**：每帧全量清空并重建 `Array<Entity>`。Culling System 把可见实体 ID 追加进去，Queue System 遍历这个数组。

```cpp
struct ViewVisibleList {
    Array<Entity> entities;
    
    void clear() { entities.clear(); }
    void push_back(Entity e) { entities.push_back(e); }
};

// CullingSystem 产出
void CullingSystem(...) {
    visible_list->clear();
    // ... 执行剔除，把通过的实体 push_back ...
}

// QueueSystem 消费
void QueueOpaqueSystem(
    Res<ViewVisibleList> visible,
    Query<(&RenderMesh, &RenderMaterial)> query,
    Array<PhaseItem>* items
) {
    for (Entity e : visible->entities) {
        // 从 Query 中获取该实体的渲染组件
        if (auto* mesh = query.GetComponent<RenderMesh>(e)) {
            // 生成 PhaseItem
        }
    }
}
```

**适用场景**：实现简单、CPU 剔除、实体数量 < 10 万。

**隐藏代价**：
- 每帧全量分配/清空数组。如果数组容量预分配得当（`clear()` 不释放内存），开销可忽略。
- GPU-Driven 模式下，可见列表在 GPU 上，需要回读（Readback）到 CPU 才能让 Queue System 消费——这引入了 CPU-GPU 同步点。

#### 分支 B：可见性结果保留在 GPU（GPU-Driven 路径）

**核心思路**：Culling System 在 GPU 上产出 `visible_instances` buffer。Queue System 也在 GPU 上执行——用 Compute Shader 把可见实例直接转换为 Phase Items 或 Indirect Draw Args，不需要回读 CPU。

```cpp
// GPU-Driven 完整数据流（概念性）
// Step 1: Compute Shader 剔除
//    Input:  instance_buffer (所有实体)
//    Output: visible_instances (可见实体索引), visible_count

// Step 2: Compute Shader 生成 Phase Items / Indirect Args
//    Input:  visible_instances, visible_count
//    Output: phase_items_buffer, indirect_args_buffer

// Step 3: Graphics Pipeline 执行 Indirect Draw
//    Input:  indirect_args_buffer
//    GPU 自动读取参数并绘制
```

**适用场景**：GPU-Driven L2+、极致 CPU 卸载。

**隐藏代价**：
- Queue System 需要重写为 GPU Compute Shader，不能再用 ECS 的 CPU System 表达。
- 调试困难——GPU buffer 的内容无法直接打印或 Inspector 查看。
- 与 CPU 侧的 RenderGraph 集成复杂（RenderGraph 需要知道有多少可见实例才能分配资源）。

#### 分支 C：增量更新 + 脏标记（Bevy 的 diff 机制）

**核心思路**：不全量重建可见列表，而是维护"本帧新增的可见实体"和"本帧移除的可见实体"。Queue System 只对增量部分做重新排序和管线特化（Pipeline Specialization）。

```cpp
struct ViewVisibleEntities {
    Array<(Entity, MainEntity)> entities;        // 当前可见实体（已排序）
    Array<(Entity, MainEntity)> added;           // 本帧新增
    Array<(Entity, MainEntity)> removed;         // 本帧移除
};

// collect_visible_cpu_culled_entities 执行双指针 merge/diff
// 旧列表: [A, B, C, D]
// 新列表: [A, C, E, F]
// diff 结果: removed=[B, D], added=[E, F], entities=[A, C, E, F]
```

**适用场景**：实体可见性变化频率低（如静态场景相机缓慢移动）、需要避免每帧全量排序/特化的开销。

**隐藏代价**：
- diff 算法本身有开销（双指针 merge，O(n)）。
- 如果可见性每帧剧烈变化（如快速转视角），diff 的开销加上处理增删的开销可能超过全量重建。

**失效条件**：可见性变化剧烈的动态场景。

#### 分支 D：按视图独立产出

**核心思路**：每个视图（View）有自己的 `ViewVisibleList`。主相机的可见列表、阴影贴图的可见列表、反射探针的可见列表——彼此独立，互不干扰。

```cpp
struct ViewVisibleList {
    Array<Entity> entities;
};

// 按视图存储
struct VisibilityState {
    HashMap<ViewId, ViewVisibleList> per_view_lists;
};

// CullingSystem 为每个视图执行一次
void CullingSystem(
    Query<(Entity, &RenderAABB)> query,
    Query<(&ExtractedCamera)> cameras,
    ResMut<VisibilityState> visibility
) {
    visibility->per_view_lists.clear();
    for (auto [cam_entity, camera] : cameras) {
        ViewId view_id = camera.view_id;
        ViewVisibleList& list = visibility->per_view_lists[view_id];
        
        // 对该视图执行剔除
        for (auto [e, aabb] : query) {
            if (FrustumIntersectsAABB(camera.frustum, aabb))
                list.push_back(e);
        }
    }
}
```

**适用场景**：多视图渲染（Split Screen、阴影级联、反射探针、编辑器多视口）。

**隐藏代价**：
- 视图数量增加时，内存和计算开销线性增长。4 个阴影级联 + 1 个主相机 = 5 份可见列表。
- 需要管理视图生命周期（创建/销毁/复用）。

### 引擎对照

> **参考：chaos / UE / Bevy 对「可见性结果表达」是怎么做的？**
>
> - **UE** 的 `FViewInfo::PrimitiveVisibilityMap` 是一个 `TArray<bool>` 或位图，按 Primitive 索引标记可见性。对于多视图（主相机 + 阴影 + 反射），每视图独立有一份 VisibilityMap。UE 的动机是：渲染线程上的系统需要快速随机访问"某个 Primitive 是否对当前视图可见"，位图/数组是 O(1) 查询的最快结构。
> - **Bevy** 的 `RenderVisibleEntities` 是一个按 `VisibilityClass`（TypeId）分类的组件。每个视图实体上挂载 `RenderVisibleEntities`，内含 `entities_cpu_culling`（已排序数组）和 `added_entities` / `removed_entities`（增量列表）。Bevy 的 `collect_visible_cpu_culled_entities` 执行双指针 diff 算法，精确追踪增量。Bevy 的动机是：避免每帧全量重新生成 Phase Item 和重新执行 Pipeline Specialization，只对增删的实体做增量更新。
> - **chaos** 从源码推断采用了视图独立的可见性结果。`RenderView` 包含自己的裁剪结果集合，`tickRender()` 中为每个 View 独立执行剔除。这与 UE 的多视图 VisibilityMap 设计一致。

### 决策分析与推荐

**默认推荐：分支 A（全量 `VisibleList` Resource）为起点，分支 D（按视图独立）为中期必备，分支 C（增量更新）为后期优化，分支 B（纯 GPU 路径）与 GPU-Driven 演进绑定。**

理由如下：

1. **全量数组是最简单、最透明的起点**。`Array<Entity>` 是 ECS 最自然的数据结构，AI 可观测性最好（Inspector 可以直接显示数组长度和内容）。Bevy 的 `entities_cpu_culling` 本质上也是一个数组。

2. **按视图独立是工业级必备，不是可选优化**。任何引擎最终都需要支持阴影贴图、反射、多玩家分屏。如果在架构起点不为多视图预留接口，后期重构成本极高。推荐在 `VisibleList` 外层套一个 `HashMap<ViewId, VisibleList>`，即使初期只有一个视图。

3. **增量更新（Bevy diff）是阶段 5 后期的锦上添花**。它的收益在"可见性变化缓慢"的场景最显著。对于阶段 5 初期的快速迭代，全量重建更简单、更不容易出 bug。当 Profiling 显示 Queue 阶段的特化开销成为瓶颈时，再引入增量机制。

4. **纯 GPU 可见性列表与 GPU-Driven 绑定**。只有当 CullingBackend 切换到 `GpuDrivenL2` 时，才需要把可见列表保留在 GPU。此时 Queue System 也需要重写为 GPU Compute——这是一个"全有或全无"的架构切换，不应与 CPU 剔除路径混用。

**ECS 映射与 AI 友好设计检查**：

```cpp
// 推荐的数据结构
using ViewId = uint32_t;

struct ViewVisibleList {
    Array<Entity> entities;           // 本帧对该视图可见的实体
    uint32_t      view_id;
    uint32_t      total_tested;       // 被测试的实体总数（用于调试/统计）
};

struct VisibilityState : public Resource {
    HashMap<ViewId, ViewVisibleList> per_view;
    uint64_t frame_number;            // 用于验证数据新鲜度
};

// CullingSystem 接口
class FrustumCullSystem : public ISystem {
public:
    using Query = QueryDef<(Entity, &RenderAABB, Optional<&BoundingSphere>)>;
    using Resources = ResourceSet<ExtractedCamera, VisibilityState>;
    
    void Update(World* world, Resources res, Query query) {
        auto& visibility = res.Get<VisibilityState>();
        visibility.per_view.clear();
        
        // 为每个视图执行剔除
        for (auto [cam_entity, camera] : world->Query<(&ExtractedCamera)>()) {
            auto& list = visibility.per_view[camera.view_id];
            list.view_id = camera.view_id;
            
            for (auto [e, aabb, maybe_sphere] : query) {
                list.total_tested++;
                
                // 可选：球体快速预筛选
                if (maybe_sphere && !FrustumIntersectsSphere(camera.frustum, *maybe_sphere))
                    continue;
                
                if (FrustumIntersectsAABB(camera.frustum, aabb)) {
                    list.entities.push_back(e);
                }
            }
        }
    }
};
```

| 检查项 | 实现状态 |
|--------|---------|
| **状态平铺** | `VisibilityState` 是 Render World Resource，`per_view` HashMap 显式存储每视图可见性。所有状态可查。 |
| **自描述** | `ViewVisibleList` 的字段通过反射系统注册，AI 可读 Schema。`total_tested` 和 `entities.count` 提供可观测的统计。 |
| **确定性** | 给定相同的 `RenderAABB` 和 `ExtractedCamera`，`FrustumCullSystem` 产出相同的 `VisibilityState`。支持帧级快照。 |
| **工具边界** | AI 可通过 MCP 查询 `VisibilityState.per_view[0].entities.count` 获取可见实体数。不应直接修改列表内容（由 System 生成）。 |
| **Agent 安全** | `VisibilityState` 是只读 Resource（从下游 System 角度）。AI 修改 `RenderAABB` 或 `ExtractedCamera` 会影响下一帧的可见性，但不会破坏当前帧的渲染一致性。 |

---

## 工业级设计清单与扩展路径

### 阶段 5 初期（已可落地）

- [ ] `Frustum` 六平面表示，支持 AABB 和 Sphere 相交测试
- [ ] `FrustumCullSystem` 在 Render World 中遍历 `Query<(&RenderAABB, Entity)>`
- [ ] `ViewVisibleList` 作为 Resource，按视图独立存储
- [ ] `BoundingSphere` 可选组件，做快速预筛选
- [ ] Inspector 面板显示当前视图的 `visible / tested` 比率

### 阶段 5 中期（推荐引入）

- [ ] 静态场景 BVH（Binned SAH，连续数组存储）
- [ ] 双轨制：静态 BVH + 动态列表
- [ ] SIMD 优化的批量 AABB-Frustum 测试（SSE/AVX）
- [ ] 单遍 Previous-HZB 遮挡剔除（Compute Shader 构建 HZB Mipchain）

### 阶段 5 后期（按需评估）

- [ ] 两遍 HZB 遮挡剔除（Two-Pass Occlusion Culling）
- [ ] GPU-Driven L2：Compute Shader 剔除 + DrawIndirect
- [ ] Bevy 风格的增量可见性更新（added/removed diff）
- [ ] GPU-Driven L3：Mesh Cluster + MultiDrawIndirect + Bindless 预留

### 阶段 6+（长期演进）

- [ ] GPU-Driven L4/L5：Visibility Buffer、Mesh Shader（硬件支持时）
- [ ] 与 LOD 系统的集成：在剔除阶段同时选择 LOD Level
- [ ] 与流送系统的集成：Nanite 风格的虚拟几何按需加载

---

> **下一步**：[[渲染队列与DrawCall组织]]，因为无论 CPU 还是 GPU 剔除，最终都产出了"可见实体列表"。下一个问题是：如何把这些可见实体按最高效的方式组织成 GPU 命令——不透明物体如何合批？透明物体如何按深度排序？多材质场景如何减少 PSO 切换？
>
> 如果你正在按 roadmap 构建引擎，完成本篇后你应该已经拥有：
> - 对 HZB 遮挡剔除原理的深度理解（Mipchain 构建、保守深度、两遍策略）
> - 对 GPU-Driven 渲染分级（L1~L5）的清晰认知，知道每个级别需要什么前置条件
> - 可见性系统在 ECS 中的工业级表达（`VisibilityState` Resource、按视图独立、预留 GPU-Driven 切换接口）
>
> 建议的验证方法：在 Inspector 中观察相机转动时 `ViewVisibleList` 的实体数量变化；在走廊/室内测试场景中对比"仅视锥剔除"和"视锥+HZB"的可见实体数量差异。
