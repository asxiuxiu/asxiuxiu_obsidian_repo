---
title: ECS 架构下的渲染世界设计
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - ecs
  - render-world
  - extract
aliases:
  - ECS 架构下的渲染世界设计
---

> **前置依赖**：[[RHI抽象层与命令模型]]
> **本模块增量**：深入理解逻辑-渲染并行架构的设计空间，掌握双 World 模型（Render World）的精确语义、Extract 阶段的数据流、以及跨世界实体映射。你将能够为 ECS 引擎建立一条从主世界状态到渲染命令的安全数据通路。
>
> 本笔记探讨的核心问题是：**在 ECS 架构下，逻辑线程和渲染线程如何安全并行？主世界的状态如何在不破坏 ECS 原则的前提下进入渲染管线？**

## 问题 0：为什么必须理解"一帧的生命周期"？

想象你在编辑器里移动了一个立方体，它在屏幕上却没有立刻移动——延迟了一帧。或者你的游戏在空场景里能跑 120fps，加了 1000 个静态树木后掉到 30fps，但 GPU 使用率只有 40%。又或者你启用了后处理 Bloom，却发现画面偶尔闪烁、偶尔有撕裂的色块。

这些问题看似分散，但它们的根因都指向同一个盲区：**你不清楚一帧里数据从 CPU 到 GPU 的完整流转路径，因此不知道瓶颈卡在哪一段、状态在哪一层出了问题。**

渲染不是"调用一个 Draw 函数就完事"的单点操作。现代引擎的一帧横跨多个 CPU 线程（逻辑线程、渲染线程、RHI 线程）、多个 GPU 队列（Graphics、Compute、Copy）、以及复杂的内存状态转换。如果你把渲染当成黑盒，那么：
- **优化无从入手**——你不知道是 CPU 录制命令太慢，还是 GPU 执行太慢，还是两者之间的同步点造成了空闲。
- **多线程必然出 bug**——逻辑线程和渲染线程共享哪些数据？什么时候可以安全地修改 Transform？没有生命周期地图，这些问题只能靠猜。
- **AI 无法观察**——如果外部 Agent 想知道"当前帧有哪些物体会被画出来"，而引擎没有显式的渲染数据结构，AI 只能通过读取底层 API 的隐式状态来推断，既不可靠也不安全。

所以，理解一帧的生命周期不是"进阶知识"，而是**让引擎从玩具走向可调试、可优化、可协作系统的门槛**。


## 问题 1：逻辑更新与渲染录制，该串行还是并行？

### 场景与根因

最 naive 的渲染循环是这样的：

```cpp
while (running) {
    ProcessInput();
    UpdatePhysics();          // 改 Transform
    UpdateAnimations();       // 改 Transform
    RenderEverything();       // 读取 Transform，生成 GPU 命令
    Present();
}
```

这个循环在 100 个物体的 demo 里完全没问题。但当场景复杂起来，你会遇到一个硬 ceiling：**CPU 的利用率永远无法超过 50%**。为什么？

因为 `RenderEverything()` 在录制 GPU 命令时，CPU 在做大量的状态设置、Draw Call 组装、资源绑定。与此同时，GPU 可能还在执行上一帧的命令——但 CPU 不能提前准备下一帧的逻辑，因为它被困在渲染录制里。更严重的是，如果某一帧渲染特别重（比如突然进入一片茂密森林），CPU 在 `RenderEverything()` 里消耗了 20ms，逻辑更新也被迫暂停 20ms——游戏逻辑"卡顿"了。

**根因在于：逻辑更新和渲染录制是两种完全不同的工作负载，却被强迫串行在同一条时间线上。**

逻辑更新（Transform 变化、动画插值、物理模拟）的产出是"下一帧的世界状态"。渲染录制（视锥剔除、排序、生成 Draw Call、设置 Barrier）的产出是"当前帧的 GPU 命令"。这两件事本来没有数据依赖——渲染第 N 帧需要的是第 N 帧的世界状态（或第 N-1 帧，取决于你的延迟策略），而逻辑在准备的是第 N+1 帧。

### 分支研究

#### 分支 A：完全串行（单线程循环）

**核心思路**：一帧内先做完所有逻辑，再做完所有渲染，然后 Present。

**适用场景**：极小项目、教学 demo、平台不支持多线程（如某些嵌入式系统）。

**隐藏代价**：
- CPU 利用率上限低。逻辑和渲染不能重叠，一帧的总时间 = 逻辑时间 + 渲染时间。
- 帧率不稳定。如果某一帧渲染突然变重，逻辑也被拖累，玩家感受到的是"操作延迟"。
- 无法发挥现代多核 CPU 的优势。8 核 CPU 上只有 1 核在工作。

**失效条件**：任何需要稳定 60fps、场景复杂度会动态变化的项目。

#### 分支 B：双缓冲状态——逻辑改副本，渲染读副本

**核心思路**：维护两套世界状态：逻辑线程写"下一帧状态"，渲染线程读"当前帧状态"。每帧结束时交换两套缓冲的指针。

```cpp
struct FrameState {
    Array<Transform> transforms;
    Array<AnimationPose> poses;
};

FrameState state_a, state_b;
FrameState* logic_state = &state_a;   // 逻辑线程写这里
FrameState* render_state = &state_b;  // 渲染线程读这里

// 每帧边界交换
std::swap(logic_state, render_state);
// 现在逻辑线程继续写新的 logic_state（上一帧渲染读过的那份）
// 渲染线程读取 render_state（刚刚被逻辑写完的那份）
```

**适用场景**：需要逻辑和渲染完全并行、且能容忍一帧延迟的项目。

**隐藏代价**：
- **内存翻倍**。所有需要被渲染读取的状态都要存两份。10 万个实体的 Transform 就是 10 万 × 48 字节 × 2 = 9.6 MB，尚可接受；但如果还要双缓冲骨骼动画、粒子状态、物理结果，内存开销会累积。
- **交换时序敏感**。交换操作必须在"逻辑线程完成写入"和"渲染线程开始读取"之间的精确窗口执行，通常由主线程在帧边界协调。如果交换时机错了，渲染线程会读到半新半旧的状态。
- **只能覆盖"可双缓冲"的数据**。某些全局状态（如相机抖动、后处理参数）如果也被双缓冲，可能导致操作响应延迟一帧。

**失效条件**：内存极度紧张、或某些状态无法复制（如大型物理世界的状态同步成本太高）。

#### 分支 C：延迟一帧的渲染管线（Pipelined Rendering）

**核心思路**：不维护完整的状态双缓冲，而是让渲染管线天然滞后逻辑一帧。逻辑线程在第 N 帧计算世界状态，渲染线程在第 N 帧录制第 N-1 帧的命令，GPU 在第 N 帧执行第 N-1 帧的命令。

```
时间轴：
帧 N-1: 逻辑更新完毕 ──→ 渲染开始提取/录制 ──→ GPU 执行
帧 N:   逻辑更新进行  ──→ 渲染提取 N-1 的数据 ──→ GPU 执行 N-1
帧 N+1: 逻辑更新进行  ──→ 渲染提取 N 的数据   ──→ GPU 执行 N
```

**适用场景**：几乎所有现代游戏引擎。UE、Bevy、chaos 都采用某种形式的延迟渲染管线。

**隐藏代价**：
- **输入延迟**。玩家在帧 N 按下的跳跃键，要到帧 N+1 的逻辑更新才被处理，帧 N+2 的渲染才能看到——总计约 2~3 帧（33~50ms @ 60fps）的输入延迟。对于竞技游戏，这是不可忽视的。
- **状态一致性复杂**。如果逻辑线程在帧 N 销毁了一个实体，渲染线程在提取帧 N-1 的数据时仍然会看到它（因为提取的是旧状态）。需要一个显式的"渲染端实体生命周期"来处理这种跨帧延迟。

**失效条件**：对输入延迟要求极高的场景（如 VR 需要 < 20ms 总延迟），需要更激进的同步策略或预测补偿。

### 引擎对照

> **参考：chaos / UE / Bevy 对「逻辑-渲染并行」是怎么做的？**
>
> - **chaos** 的 `ClientTickManager::tickMain()` 中，`present_logic_task`（逻辑更新）和 `tickRender()`（渲染录制）被投递到任务系统，但渲染开始前必须 `waitForSingleTaskCounter(present_logic_task_counter)` 等待逻辑完成。这意味着 chaos 的逻辑与渲染是**部分并行**的——逻辑更新的后段和渲染录制的准备段可以重叠，但渲染提取数据前必须同步。这种设计的动机是：chaos 的渲染需要直接读取 `EntityComponentManager` 的组件数组，如果逻辑和渲染完全并行，组件数组的并发访问需要大量锁。
> - **UE** 采用了经典的三线程模型：**Game Thread** 更新逻辑并组装 `FSceneViewFamily`；**Render Thread** 接收 ViewFamily、执行场景裁剪、构建 RDG、录制 RHI 命令；**RHI Thread** 翻译 RHI 命令为底层 API 调用并提交。Game Thread 和 Render Thread 之间通过 `ENQUEUE_RENDER_COMMAND` 队列通信，天然形成一帧延迟的流水线。UE 的动机是：C++ 的 UObject 系统不适合多线程直接访问，必须通过命令队列解耦。
> - **Bevy** 采用了最激进的 ECS-native 方案：**独立的 Render World**。主 World 运行逻辑 System，每帧通过 `ExtractSchedule` 把渲染需要的数据（Transform、Mesh、Material）复制到 Render World。Render World 随后运行自己的 `Render` Schedule，生成 GPU 命令。主 World 的 `Update` 和 Render World 的 `Render` 可以**完全并行**（通过 `PipelinedRenderingPlugin`）。Bevy 的动机非常纯粹：ECS 的 World 不是线程安全的，两个 World 之间没有共享可变状态，因此并行无需锁。

### 决策分析与推荐

**默认推荐：分支 C 的 ECS 化表达——双 World 延迟管线，吸收 UE 的三线程解耦思想和 Bevy 的 Render World 隔离机制。**

理由如下：

1. **UE 的三线程模型是工业级验证的"最小正确架构"**。它不是过度设计，而是解决以下问题的系统性答案：
   - 逻辑状态（UObject 树）不适合多线程直接遍历 → Game Thread 单线程更新。
   - 渲染录制（RHI 命令生成）可以并行但不应与逻辑竞争 → Render Thread 独立执行。
   - 底层 API 调用（D3D12/Vulkan）有线程安全规则和驱动开销 → RHI Thread 统一提交。
   在 ECS 架构中，这三层对应：**主 World System 调度**（Game Thread）、**Render World System 调度 + 命令生成**（Render Thread）、**RHI 命令翻译与提交**（RHI Thread）。

2. **Bevy 的 Render World 是 ECS 架构下最干净的并行方案**。Extract 阶段虽然引入了数据拷贝开销，但它换来了**零锁并行**——主 World 和 Render World 之间没有任何共享可变状态。对于 AI 可观测性来说，Render World 是一个完整、自洽、可快照的"渲染输入数据集"，AI 可以精确查询"第 N 帧渲染看到了哪些物体"。

3. **双缓冲（分支 B）不是必需的**。Bevy 没有双缓冲整个 World——它只在 Extract 阶段做增量拷贝。UE 也没有双缓冲整个场景——它通过 `FPrimitiveSceneProxy` 在 Game Thread 和 Render Thread 之间建立轻量代理。对于默认实现，完整的状态双缓冲内存开销和同步复杂度不值得。

4. **chaos 的部分并行是妥协方案**。chaos 的 `waitForSingleTaskCounter` 意味着渲染提取前必须等待逻辑完成，这限制了并行度。在 ECS 架构中，我们可以通过 Extract 阶段的"快照语义"避免这种等待：主 World 在 Extract 点冻结渲染相关组件的视图，Render World 复制这份快照，主 World 随后立即继续下一帧逻辑——不需要等待 Render World 完成。

**具体实施策略（渐进式）**：

- **阶段 5 初期**：实现最简单的串行 Extract。主 World 的 `PostUpdate` 阶段结束后，运行 `ExtractRenderDataSystem`，遍历所有 `(MeshHandle, MaterialHandle, GlobalTransform)` 组件，复制到 Render World 的 `(RenderMesh, RenderMaterial, RenderTransform)`。然后 Render World 顺序执行 `Prepare → Queue → Render → Present`。主 World 等待 Render World 完成后才进入下一帧。**这是串行的，但数据结构已经为并行预留了空间。**
- **阶段 5 中期**：将 Extract 和 Render World 的执行从主线程分离到独立线程。主 World 在 Extract 后立即进入下一帧逻辑，Render World 在后台并行执行。需要引入一帧延迟：Render World 渲染的是上一帧 Extract 的数据。
- **阶段 5 后期**：引入 RHI 线程。Render World 的 System 生成引擎级命令缓冲（`IRHICommandList`），RHI 线程负责翻译为 D3D12 API 并提交。这是 UE 三线程模型的完整 ECS 映射。

**遗留问题**：并行架构解决了 CPU 效率，但引入了新的问题——渲染线程读取的"世界状态"可能包含数万个实体，如何快速判断"哪些实体在相机视野内"？如果不加筛选，每帧遍历所有实体生成 Draw Call，CPU 仍然会崩溃。这就是问题 2 的前置：可见性判断。


## 问题 2：游戏世界的 ECS 数据如何进入渲染管线？

### 场景与根因

假设你的主 World 里有 10000 个实体，每个实体有 `MeshHandle`、`MaterialHandle`、`GlobalTransform`。渲染管线不可能也不应该处理全部 10000 个——很多在相机背后、很多距离太远、很多被遮挡。但在"该不该画"之前，还有一个更基础的问题：**渲染管线怎么知道这些实体的存在？**

在 OOP 引擎中，答案通常是回调或注册表：`MeshRenderer` 类在构造时把自己注册到 `RenderScene`，渲染时遍历 `RenderScene` 的列表。但在 ECS 中，没有"对象自我注册"的概念——数据是平铺的组件，System 是批量查询。

**根因在于：ECS 的存储模型（Archetype / Sparse Set）是为了"同构批量迭代"优化的，而渲染管线的输入需要"按视图、按阶段、按材质类型"的异构组织。** 直接从 ECS 存储生成 GPU 命令，就像从数据库原始表直接生成报表——你需要一个"提取-转换-加载"（ETL）过程。

### 分支研究

#### 分支 A：渲染 System 直接查询主 World

**核心思路**：在 Render World（或主 World 的 Render 阶段）中，System 直接通过 `Query<(&MeshHandle, &MaterialHandle, &GlobalTransform)>` 遍历主 World，为每个匹配实体生成 Draw Call。

```cpp
void RenderDirectQuerySystem(World* world, ICommandContext* ctx) {
    auto query = world->Query<(&MeshHandle, &MaterialHandle, &GlobalTransform)>();
    for (auto [mesh, mat, transform] : query) {
        ctx->BindPipeline(mat->pipeline);
        ctx->BindVertexBuffer(mesh->vb);
        ctx->DrawIndexed(mesh->index_count);
    }
}
```

**适用场景**：极小项目、无并行需求、快速原型。

**隐藏代价**：
- **System 与 World 紧耦合**。渲染 System 必须知道主 World 的组件布局，无法支持多视图（如编辑器同时显示 Scene View 和 Game View）。
- **无法并行**。如果主 World 正在运行逻辑 System，同时渲染 System 在查询组件，需要读写锁或 World 暂停——破坏了 ECS 的调度并行性。
- **数据格式不匹配**。主 World 的 `MaterialHandle` 只是一个句柄，渲染需要的可能是 `PreparedMaterial`（含 BindGroup、PipelineKey）。直接查询意味着每次渲染都要做句柄解析。

**失效条件**：任何需要逻辑-渲染并行、多视图、或复杂材质准备的项目。

#### 分支 B：提取阶段（Extract）——数据搬运到 Render World

**核心思路**：每帧先运行一个专门的 Extract Schedule，把主 World 中渲染所需的组件数据复制到 Render World 的对应组件中。渲染 System 只查询 Render World。

```cpp
// Extract 阶段：主 World → Render World
void ExtractRenderablesSystem(
    Query<(&MeshHandle, &MaterialHandle, &GlobalTransform)> source,
    RenderWorld* render_world
) {
    for (auto [mesh, mat, transform] : source) {
        Entity render_e = render_world->CreateEntity();
        render_world->AddComponent(render_e, RenderMesh{mesh.handle});
        render_world->AddComponent(render_e, RenderMaterial{mat.handle});
        render_world->AddComponent(render_e, RenderTransform{transform.matrix});
    }
}

// Render 阶段：只查询 Render World
void QueueOpaqueDrawsSystem(RenderWorld* rw) {
    auto query = rw->Query<(&RenderMesh, &RenderMaterial, &RenderTransform)>();
    // ... 生成渲染队列
}
```

**适用场景**：ECS 原生引擎、需要逻辑-渲染并行、需要多视图。

**隐藏代价**：
- **拷贝开销**。Extract 阶段需要遍历所有可渲染实体并复制数据。对于 10 万实体，即使只复制 16 字节的 Transform + 8 字节的 Handle，也是约 2.4 MB 的内存拷贝。在高端 CPU 上这是亚毫秒级的，但在中低端设备上可能成为瓶颈。
- **实体生命周期同步**。如果主 World 在 Extract 后销毁了一个实体，Render World 仍然持有它的 `RenderMesh`——需要显式的实体同步机制（如 Bevy 的 `SyncWorldPlugin` + `RenderEntity` / `MainEntity` 双向映射）。
- **增量提取优化复杂**。如果大部分实体每帧都没变，全量拷贝是浪费。理想的方案是只拷贝"本帧变化的实体"，但这需要精确的变更检测（Change Detection）或脏标记。

**失效条件**：实体数量极大（>50万）且变更极频繁，拷贝开销超过帧预算的 10%。

#### 分支 C：渲染代理（Render Proxy）——轻量级引用

**核心思路**：不在 Render World 中复制完整组件，而是创建一个轻量"代理"结构，只包含主 World 实体的句柄和渲染所需的派生数据（如世界矩阵、排序键）。

```cpp
struct RenderProxy {
    Entity          main_entity;      // 指向主 World 的实体
    MeshHandle      mesh;
    MaterialHandle  material;
    Mat4            world_matrix;     // 已经提取并计算好的世界矩阵
    float           depth;            // 视图空间深度，用于排序
    uint32_t        sort_key;         // 合批排序键
};

// Extract 阶段只生成 Proxy 数组
void ExtractProxiesSystem(
    Query<(&MeshHandle, &MaterialHandle, &GlobalTransform)> source,
    Array<RenderProxy>* proxy_list
) {
    proxy_list->clear();
    for (auto [mesh, mat, transform] : source) {
        proxy_list->push_back({
            .main_entity = /* ... */,
            .mesh = mesh.handle,
            .material = mat.handle,
            .world_matrix = transform.matrix,
            // depth 和 sort_key 在后续阶段计算
        });
    }
}
```

**适用场景**：需要减少 Render World 的实体管理开销、希望渲染数据以连续数组形式存在以便 SIMD 处理。

**隐藏代价**：
- Proxy 数组失去了 ECS 的组件化优势。如果后续阶段需要按不同条件筛选（如"只渲染阴影投射者"），Proxy 数组需要额外维护子集列表，而 ECS 可以直接通过 `Query<(&RenderMesh, &CastShadow)>` 筛选。
- Proxy 与主 World 实体之间的双向同步仍然需要（如实体销毁时从 Proxy 数组移除）。

**失效条件**：渲染管线需要大量按组件条件筛选的场景（如多种渲染阶段各自提取不同子集）。

### 引擎对照

> **参考：chaos / UE / Bevy 对「数据提取」是怎么做的？**
>
> - **chaos** 没有显式的 Extract 阶段。渲染系统直接通过 `EntityComponentManager` 查询组件数据，在 `RenderScene::tick()` 中收集 `RenderView`。这种设计的代价是：逻辑更新和渲染收集之间必须显式同步（`waitForRender()`），否则组件数组可能在遍历过程中被逻辑系统修改（如实体销毁导致 Archetype 迁移，破坏遍历器）。
> - **UE** 使用 **FPrimitiveSceneProxy** 作为 Game Thread 和 Render Thread 之间的桥梁。`UPrimitiveComponent`（Game Thread）在注册时创建一个对应的 `FPrimitiveSceneProxy`（Render Thread 对象），Proxy 包含渲染所需的几何、材质、变换信息。Game Thread 通过 `GetDynamicMeshElements()` 等接口把变更推送到 Proxy，Render Thread 读取 Proxy 而不是直接遍历 UObject。这不是 ECS 化的 Extract，而是 OOP 的"代理模式"，但设计意图相同：**解耦逻辑状态与渲染消费**。
> - **Bevy** 使用最彻底的 **ECS Extract**。`ExtractPlugin` 在每帧开始时通过 `mem::replace` 把主 World Swap 进 Render World 作为 `MainWorld` Resource，然后运行 `ExtractSchedule`，由各种 `ExtractComponentPlugin` 把特定组件从 `MainWorld` 复制到 Render World。Bevy 还使用 `ExtractInstance` 批量提取到 HashMap，以及 `SyncWorldPlugin` 维护跨世界实体映射。Bevy 的 Extract 不是全量复制——只有注册了提取系统的组件才会被复制，而且 `previous_len` 预分配机制减少了内存分配开销。

### 决策分析与推荐

**默认推荐：分支 B（Extract 阶段 + Render World），但采用 Proxy 风格的连续数组作为渲染阶段内部的数据组织形式。**

理由如下：

1. **UE 的 Proxy 模式证明了"逻辑-渲染解耦"是工业级必备**。UE 之所以不直接让 Render Thread 遍历 UObject，是因为 UObject 的生命周期、继承树、GC 都与渲染的批量处理需求冲突。Proxy 是 UE 的 OOP 表达，Extract + Render World 是它在 ECS 中的自然映射。

2. **Bevy 的 Extract 是 ECS 下最安全的并行边界**。`mem::replace` 交换主 World 是 O(1) 操作，Extract Schedule 中的系统可以安全地读取主 World 数据而不需要任何锁。这个设计虽然激进，但已经被 Bevy 的 0.19 版本在生产环境中验证。

3. **Proxy 数组在渲染阶段内部仍有价值**。Extract 阶段把数据放进 Render World 的 ECS 组件中，这解决了"安全并行"和"多视图"问题。但在 Render World 的 `Queue` 阶段，我们需要把匹配 `Query<(&RenderMesh, &RenderMaterial)>` 的实体输出到一个连续的 `PhaseItem` 数组中——这本质上就是 Proxy 数组。Bevy 的 `BinnedRenderPhase` / `SortedRenderPhase` 内部正是这样的连续数组。

4. **chaos 的直接查询在 ECS 架构下不可持续**。如果未来引入多线程逻辑 System，直接遍历 `EntityComponentManager` 的渲染系统会与组件增删操作竞争，导致遍历器失效或数据竞争。

**具体实施策略**：

```cpp
// 主 World 组件
struct MeshHandle      { AssetId id; };
struct MaterialHandle  { AssetId id; };

// Render World 组件（Extract 阶段生成）
struct RenderMesh      { GpuBuffer vb; GpuBuffer ib; uint32_t index_count; };
struct RenderMaterial  { PipelineKey pipeline_key; BindGroupHandle bind_group; };
struct RenderTransform { Mat4 world_matrix; };

// Extract System：主 World → Render World
class ExtractRenderablesSystem : public ISystem {
public:
    void Update(World* main, World* render) {
        auto source = main->Query<(Entity, MeshHandle, MaterialHandle, GlobalTransform)>();
        for (auto [entity, mesh, mat, transform] : source) {
            // 使用跨世界实体映射找到/创建对应的 Render World 实体
            Entity render_e = sync_map->GetOrCreateRenderEntity(entity);
            render->SetComponent(render_e, RenderMesh{ /* 解析 Handle */ });
            render->SetComponent(render_e, RenderMaterial{ /* 解析 Handle */ });
            render->SetComponent(render_e, RenderTransform{transform.matrix});
        }
    }
};
```

**关键设计点**：
- **Extract 是只读的**。Extract System 绝不修改主 World 的状态。这保证了一致性——即使 Extract 和逻辑更新并行，主 World 的变更也只会影响下一帧的 Extract。
- **Handle 解析延迟到 Prepare 阶段**。Extract 阶段只复制 Handle 本身（8 字节），不解析 Handle 指向的实际 GPU 资源。`RenderMesh` 中的 `GpuBuffer vb` 是在 Render World 的 `PrepareAssets` 阶段才填充的。这避免了 Extract 阶段阻塞在等待 GPU 资源上。
- **跨世界实体映射是双向的**。`RenderEntity(MainEntity)` 和 `MainEntity(RenderEntity)` 两个组件维护映射，确保 Render World 的渲染结果可以回溯到主 World 的实体（如选中 Inspector 中的渲染对象）。

**跨世界实体映射的三种状态变化**：

| 操作 | 主 World 动作 | Extract 阶段的同步行为 | Render World 结果 |
|------|-------------|---------------------|------------------|
| 创建 | `Spawn(E)` + `SyncToRenderWorld` | 检测到无对应 `RenderEntity` | `Spawn(E')`，插入 `MainEntity(E)` |
| 更新 | `SetComponent(E, Transform)` | 读取并克隆到渲染组件 | `SetComponent(E', RenderTransform)` |
| 销毁 | `Destroy(E)` | 检测到 `RenderEntity(E)` 指向的实体已失效 | 下一帧 `Destroy(E')`，回收映射 |

> 注意一个微妙的时序：主 World 在帧 N 销毁 E，Render World 在帧 N 的渲染中仍然可以使用 `E'`——这正是延迟管线的预期行为。`E'` 的销毁被延迟到帧 N+1 的 Extract，与渲染管线的快照语义一致。

**遗留问题**：Extract 阶段把可渲染实体搬进了 Render World，但接下来还有三个关键的架构问题：哪些物体真的需要画？可见物体如何组织成 GPU 命令？多 Pass 管线的资源依赖如何自动管理？


## 问题 3：Extract 之后，渲染世界如何决定"画什么"？

### 场景与根因

假设 Extract 阶段把 10000 个实体搬进了 Render World，但相机视野内可能只有 500 个。如果在 Extract 之后不做任何筛选，后续的 Queue 阶段要为 10000 个实体生成 Phase Item，Culling 之后的管线阶段也要遍历这 10000 个实体——CPU 开销仍然巨大。

**根因在于：Extract 解决的是"数据可见性"（主 World → Render World），但还没有解决"视图可见性"（Render World → Camera View）。** 这两个筛选发生在不同的坐标空间和不同的阶段。

### 分支研究

#### 分支 A：在 Extract 之前剔除（主 World 侧）

在主 World 的 `ExtractRenderablesSystem` 中，先对每个实体做粗略的视锥测试，只把可能可见的实体搬进 Render World。

- **优点**：减少 Extract 的拷贝量和 Render World 的实体数量。
- **隐藏代价**：主 World 的 Extract System 必须读取 `Camera` 组件的视锥参数，增加了主 World 与渲染管线的耦合；且主 World 的 `GlobalTransform` 可能已经过时（如果逻辑更新和渲染并行）。
- **失效条件**：需要多视图渲染时（如阴影贴图的 Light View），主 World 的 Extract 系统需要知道所有视图的参数，架构复杂度激增。

#### 分支 B：在 Extract 之后剔除（Render World 侧）

Extract 阶段不做任何筛选，把所有可渲染实体搬进 Render World。随后在 Render World 的 `Culling` 阶段，用 `ExtractedView` 的视锥参数对 `RenderTransform` 做相交测试，产出 `ViewVisibleList`。

- **优点**：视图参数（相机矩阵、视锥、LOD 距离）全部集中在 Render World，主 World 保持纯净；天然支持多视图（每个视图独立产出一份 `ViewVisibleList`）。
- **隐藏代价**：Extract 的拷贝量比分支 A 大，但 Render World 的实体管理开销通常远小于 GPU 命令录制开销。

#### 分支 C：GPU-Driven 剔除（Compute Shader）

把可见性判断推迟到 GPU 侧，用 Compute Shader 对实体 AABB 做批量视锥测试，产出可见性位图或间接绘制参数。

- **优点**：CPU 零遍历，数十万实体可在 GPU 上毫秒级完成剔除。
- **隐藏代价**：需要维护 GPU Scene 数据结构（实体 AABB、LOD 信息的 GPU 缓冲），架构复杂度高，且对小场景是过度设计。
- **失效条件**：实体数量 < 5000 时，GPU 调度和数据上传开销可能超过 CPU 逐实体测试的收益。

### 决策分析

**默认推荐：分支 B（Render World 侧 CPU 剔除），预留分支 C 的 GPU-Driven 路径。**

理由：
1. **UE 的裁剪体系完全在 Render Thread 执行**。`FSceneRenderer::BeginInitViews` 在 Render Thread 上构建视锥、执行裁剪、产出 `PrimitiveVisibilityMap`。主线程（Game Thread）不感知裁剪逻辑——这与分支 B 的架构完全一致。
2. **Bevy 的 `Extract` 也不做裁剪**，`FrustumCullSystem` 是 Render World 的独立 System。可见性判断作为渲染管线的内部阶段，不污染主 World 的逻辑纯度。
3. **分支 A 破坏了主 World 的纯净性**。如果 Extract 系统需要读取相机参数，就意味着主 World 的 System 与渲染视图产生了耦合——当编辑器需要同时渲染 Scene View 和 Game View 时，主 World 的 Extract 系统需要处理多视图逻辑，这是错误的方向。
4. **分支 C 是阶段 5 后期的扩展路径**。在最小可行渲染管线（阶段 5 初期）中，CPU 逐实体测试足够；当场景规模达到数万实体时，再引入 GPU-Driven 剔除。

**遗留问题**：经过 Culling，我们得到了 500 个可见实体。但 GPU 每次 Draw Call 都有状态切换开销——500 个实体如果每个都单独调用 `DrawIndexed`，CPU 录制命令的开销仍然很高。如何在生成 Draw Call 之前，把这 500 个实体按 GPU 最高效的方式重新组织？


## 问题 4：可见实体如何组织成 GPU 能执行的命令？

### 场景与根因

500 个可见实体，每个都有 `RenderMesh` + `RenderMaterial` + `RenderTransform`。最 naive 的做法是直接在 `RenderSystem` 中遍历这 500 个实体，每个实体调用一次 `DrawIndexed`。

但在现代 GPU 上，**Draw Call 本身的 CPU 开销和状态切换开销是性能瓶颈**。如果 500 个实体使用了 30 种不同的 PSO（Pipeline State Object），而它们又完全随机地分布在遍历顺序中，GPU 会频繁地切换管线状态——这种切换在 CPU 侧需要重新绑定 Vertex Buffer、Index Buffer、Descriptor Set、Push Constants 等，在 GPU 侧则可能导致流水线气泡（pipeline bubble）。

**根因在于：ECS 的存储模型按实体组织数据，但 GPU 的执行效率依赖于按"状态相似性"组织命令。**

### 分支研究

#### 分支 A：直接在 ECS 遍历中生成 Draw Call

`QueueSystem` 直接遍历 `Query<(&RenderMesh, &RenderMaterial, &RenderTransform)>`，为每个实体调用 `ctx->DrawIndexed(...)`。

- **优点**：极简实现，没有中间数据结构。
- **隐藏代价**：无法排序，无法合批，透明物体无法按深度排序，导致渲染错误和性能崩溃。
- **失效条件**：任何超过 100 个 Draw Call 的场景。

#### 分支 B：收集到连续数组（Proxy / Phase Item）再排序

`QueueSystem` 不直接生成 Draw Call，而是先把每个可见实体输出为一个轻量的 `PhaseItem` 结构（含 PSO Key、材质 Key、深度、实体引用），收集到连续的 `Array<PhaseItem>` 中。然后对这个数组按排序键（Sort Key）做稳定排序，最后按排序后的顺序批量生成 Draw Call。

```cpp
struct PhaseItem {
    uint64_t sort_key;    // 高位：PSO+Material Bin，低位：深度
    Entity   render_entity;
};

// QueueSystem：收集
void QueueOpaqueSystem(Query<...> query, Array<PhaseItem>* items) {
    items->clear();
    query.ForEach([&](Entity e, const RenderMesh& m, const RenderMaterial& mat, const RenderTransform& t) {
        uint64_t key = ((uint64_t)mat.pipeline_key << 32) | ComputeDepthKey(t.depth);
        items->push_back({key, e});
    });
    std::sort(items->begin(), items->end(), [](a, b){ return a.sort_key < b.sort_key; });
}

// RenderSystem：按排序结果批量生成命令
void RenderPhaseSystem(const Array<PhaseItem>& items, CommandContext* ctx) {
    for (const auto& item : items) {
        // 相同 PSO 的实体自然相邻，可批量绑定
        ctx->DrawIndexed(...);
    }
}
```

- **优点**：连续数组适合 SIMD 和缓存；排序后相同 PSO 的实体相邻，减少状态切换；透明物体可按深度从远到近排序。
- **隐藏代价**：需要额外的内存分配和排序计算（500 个元素的排序约 0.01ms，可忽略）。

#### 分支 C：Binned Phase + Sorted Phase 混合

对不透明物体使用 **Binned Phase**：按 `PSO+Material` 分箱（Bin），同一个 Bin 内的实体自动共享 PSO 绑定，只需更新 Per-Draw 的变换矩阵。对透明物体使用 **Sorted Phase**：必须按视图空间深度排序，无法分箱。

- **优点**：不透明物体获得近似最优的合批效率；透明物体保证渲染正确性。
- **隐藏代价**：需要维护两套 Phase 数据结构；Binned Phase 的 Bin 粒度需要调优（太粗则缓存不友好，太细则分箱开销大）。

### 决策分析

**默认推荐：分支 C（Binned Phase + Sorted Phase 混合）。**

理由：
1. **UE 的 `FParallelMeshDrawCommandPass` 本质上就是 Binned Phase**。Mesh Draw Command 按 `FMeshDrawCommandSortKey` 排序，相同 Shader+PSO 的命令被分组批量执行。Bevy 的 `BinnedRenderPhase` / `SortedRenderPhase` 也是完全相同的分层设计。
2. **不透明和透明物体的排序需求根本不同**。不透明物体只需要"相同状态相邻"以最大化 Early-Z 和合批；透明物体必须"从远到近"以保证 Alpha Blending 正确。强行用同一套机制处理两者，必然牺牲一方。
3. **分支 B 是分支 C 的基础**。即使在阶段 5 初期只实现分支 B（单一排序数组），数据结构也已经为后续升级成分支 C 预留了空间——只需把排序键的位域拆分，把数组拆成多个 Phase 桶。

**遗留问题**：Queue 阶段产出了排序后的 Phase Item，但一帧的渲染往往包含多个 Pass（ShadowMap → GBuffer → Lighting → PostProcess）。每个 Pass 可能读写不同的纹理，需要在 Pass 之间插入资源屏障（Barrier）。手动管理这些屏障在多平台引擎中是灾难性的——如何在 ECS 架构中自动推导和管理 Pass 间的资源依赖？


## 问题 5：多 Pass 管线的资源依赖如何管理？

### 场景与根因

一帧的渲染管线包含多个 Pass：Shadow Map Pass 写入深度纹理，GBuffer Pass 读取深度并写入 GBuffer A/B/C，Lighting Pass 读取 GBuffer 和深度并写入 Scene Color，PostProcess Pass 读取 Scene Color 并写入 SwapChain BackBuffer。

在 D3D12/Vulkan 这类显式 API 中，**每个纹理在每次读写前都必须处于正确的资源状态**（如 `DEPTH_WRITE` → `PIXEL_SHADER_RESOURCE` → `RENDER_TARGET`）。如果状态转换缺失或顺序错误，GPU 会直接崩溃或产生未定义行为。

**根因在于：手动管理 Barrier 是"人脑无法可靠完成的组合爆炸问题"。** 10 个 Pass、20 张纹理的全局状态转换图，其可能的合法转换序列数量是天文数字。工程师在代码中手动插入 `TransitionBarrier` 不仅容易遗漏，还会让渲染管线的代码与平台 API 深度耦合——D3D12 的 `ResourceBarrier`、Vulkan 的 `ImageMemoryBarrier`、Metal 的 `MTLRenderPassDescriptor` 各自有不同的语义和限制。

### 分支研究

#### 分支 A：手动 Barrier（即时模式）

每个 Pass 的 System 在开始和结束时手动调用 `ctx->TransitionTexture(depth, DEPTH_WRITE, PIXEL_SHADER_RESOURCE)`。

- **优点**：控制精确，没有抽象层开销。
- **隐藏代价**：代码与特定 API 绑定；新增一个 Pass 时需要手动检查所有相关的 Barrier；跨平台维护成本极高；AI 无法自动推导 Pass 间的资源依赖。
- **失效条件**：任何超过 3 个 Pass 的管线。

#### 分支 B：声明式 RenderGraph

在帧开始时，用一个 `RenderGraph` 数据结构声明所有 Pass 及其输入输出资源（如 `Pass A 写入 Depth`，`Pass B 读取 Depth`）。随后由编译器自动推导拓扑顺序、裁剪无效 Pass、插入 Barrier、分配瞬态资源。

- **优点**：Pass 之间完全解耦，只需声明自己的资源需求；自动 Barrier 推导消除了手动错误；瞬态资源别名可显著节省显存。
- **隐藏代价**：需要实现一个图编译器（拓扑排序、资源生命周期分析、别名分配），初期架构复杂度高。

#### 分支 C：Bevy 的 Schedule 驱动渲染图

Bevy 在 0.12+ 版本中将独立的 `RenderGraph` 数据结构重构为基于 ECS `Schedule` 的系统链。Render Pass 作为常规 System 运行，通过 `before`/`after` 依赖显式排序。资源依赖不通过图节点声明，而是通过 System 的 `Query` 和 `Resource` 签名隐式表达。

- **优点**：没有独立的 Graph 数据结构，渲染管线完全融入 ECS 的调度体系。
- **隐藏代价**：System 之间的资源依赖不是显式声明的，Barrier 推导需要额外的静态分析或运行时跟踪；跨 Pass 的资源别名（Transient Resource Aliasing）难以在纯 Schedule 模型中实现。

### 决策分析

**默认推荐：分支 B（声明式 RenderGraph），但将 Graph 的编译结果映射到 ECS Schedule 的执行阶段。**

理由：
1. **UE 的 `FRDGBuilder` 是工业级声明式 RenderGraph 的典范**。RDG 的 `AddPass` 显式声明读写资源，`Execute()` 阶段统一编译：拓扑排序、死 Pass 剔除、自动 Barrier 插入、瞬态资源别名。这是经过 UE5 大规模项目验证的"最正确方案"。
2. **Bevy 的 Schedule 驱动模型在资源别名和跨平台 Barrier 推导上存在局限**。Bevy 选择抛弃独立 Graph 数据结构，将依赖编码在 Schedule 中，这简化了架构，但失去了"声明-编译-优化"的能力。对于工业级引擎，显式声明资源依赖的 RenderGraph 更具可观测性和优化空间。
3. **分支 B 与 ECS 并不冲突**。RenderGraph 本身是一个 ECS `Resource`，`BuildRenderGraphSystem` 和 `CompileRenderGraphSystem` 是 Render World 的常规 System。Graph 编译完成后，`ExecuteSystem` 遍历排序后的 Pass 节点，在每个节点的 Lambda 中调用 `CommandContext` 录制命令——这正是"ECS 调度 + 声明式图"的混合架构。

**遗留问题**：RHI 抽象层的接口定义完成后，D3D12 和 Vulkan 都是显式现代 API，但 RHI 层在 ECS 中应该如何表达？全局单例、ECS Resource、还是双 World 模型？


## 问题 6：ECS 架构下，RHI 层该如何设计？

经过问题 3~5 的梳理，我们已经知道 Render World 内部的渲染管线会经历 Culling → Queue → RenderGraph → Execute 等阶段。但这些阶段的最终产物——GPU 命令——总要有一个"入口"送进驱动。在 ECS 架构中，这个入口应该如何设计？

### 场景与根因

传统引擎的 RHI 层通常有一个全局单例：`GDynamicRHI`、`g_render_device`、`RenderContext::Get()`。上层代码随时随地可以访问它，发起绘制命令。

但我们的引擎是 ECS 架构。ECS 的核心约束是：**状态必须平铺在组件中，系统是纯函数——输入组件，输出副作用（或写入其他组件）**。全局可变状态是 ECS 的"反模式"，因为它破坏了系统的可测试性、可并行性和可观测性。

**深层矛盾**：GPU 驱动本质上就是一个巨大的全局状态机。无论你如何在 CPU 侧设计 ECS，最终总要有一个地方把命令送进 GPU Queue。这个"送命令的入口"在物理上就是全局唯一的。问题是：**这个全局性应该暴露到引擎的哪一层？**

### 分支 A：传统单例模式——RHI 全局入口对上层隐藏 ECS 语义

**核心思路**：RHI 层内部仍用传统单例/全局指针管理 Device 和 Context，上层渲染系统（Render System）通过全局入口录制命令。ECS 层不感知 RHI 的存在，只在渲染 System 中做传统调用。

```cpp
// 传统风格：RHI 单例
class RenderDevice {
    static RenderDevice* s_instance;
public:
    static RenderDevice* Get() { return s_instance; }
    CommandContext* CreateCommandContext();
};

// ECS System 内部直接调用全局入口
class MeshRenderSystem : public ISystem {
public:
    void Update(World* world) {
        auto* ctx = RenderDevice::Get()->CreateCommandContext();
        world->Query<Transform, Mesh, Material>().ForEach([ctx](...) {
            ctx->DrawIndexed(mesh->pso, mesh->vb, mesh->ib, ...);
        });
        RenderDevice::Get()->Submit(ctx);
    }
};
```

**适用场景**：快速迁移传统渲染代码到 ECS 引擎、RHI 层复用成熟第三方库（如 bgfx）且不愿大改。

**隐藏代价**：
- **AI 可观测性受损**：AI Agent 无法通过 ECS 查询理解"当前 GPU 状态"，因为 RHI 状态在 ECS 之外。
- **确定性难以保证**：全局单例的副作用（如内部缓存状态、命令提交时序）让帧级回放和快照变得困难。
- **多 World 隔离困难**：如果未来需要 Editor World 和 Play World 同时渲染（如 UE 的 PIE 模式），全局单例会强制两者共享同一个 GPU 上下文，增加隔离复杂度。

**失效条件**：当引擎需要深度 ECS 化（如 Bevy 的双 World 渲染），或需要 AI 直接观察和操作渲染状态时。

### 分支 B：ECS Resource——把 GPU 上下文作为 World 的资源

**核心思路**：RHI 的核心对象（Device、Queue、CommandContext）被包装为 ECS `Resource`，插入到 World 中。System 通过依赖注入获取它们，而不是通过全局单例。

```cpp
// ECS 风格：RHI 对象是 World 中的 Resource
struct RenderDevice {
    BackendDevice* backend_device;
};

struct RenderQueue {
    BackendQueue* backend_queue;
};

struct FrameContext {
    CommandContext* current_cmd_ctx;
    uint64_t frame_number;
};

// System 通过签名声明依赖
class MeshRenderSystem : public ISystem {
public:
    using Query = QueryDef<Transform, Mesh, Material>;
    using Resources = ResourceSet<RenderDevice, RenderQueue, FrameContext>;
    
    void Update(World* world, Resources res, Query query) {
        auto* ctx = res.Get<FrameContext>()->current_cmd_ctx;
        query.ForEach([ctx](const Transform& t, const Mesh& m, const Material& mat) {
            ctx->DrawIndexed(mat.pipeline, m.vb, m.ib, m.index_count, ...);
        });
    }
};
```

**适用场景**：ECS 原生引擎、需要 AI 可观测性、需要多 World 隔离（Editor/Play/Preview）。

**隐藏代价**：
- 需要把第三方库的"全局设备"概念包装成 ECS Resource，增加一层胶水代码。
- `CommandContext` 本身是有状态的（当前绑定的 PSO、Vertex Buffer 等），如果多个 System 同时获取同一个 Context 并写入命令，需要线程同步。这与 System 并行调度的目标冲突。

**失效条件**：当 System 并行度很高，且多个 System 需要同时录制命令时，共享的 Context 成为串行瓶颈。

### 分支 C：分离的 Render World——双世界模型

**核心思路**：不只在主 World 中存放 RHI Resource，而是创建一个**独立的 Render World**。主 World 运行游戏逻辑（Transform 更新、动画、物理），每帧的"提取阶段"（Extract）把需要渲染的数据从主 World 复制到 Render World。Render World 中的所有 System 都是渲染相关的，它们可以独立调度，最终由 Render World 的 `Present` System 把画面输出。

```cpp
// 双 World 架构（高度简化）
World main_world;   // 游戏逻辑：Transform、Motor、Collider...
World render_world; // 渲染逻辑：RenderMesh、RenderCamera、DrawCommand...

// 提取阶段：主世界 -> 渲染世界
class ExtractSystem : public ISystem {
    void Update(World* main, World* render) {
        main->Query<Transform, Mesh, Material>().ForEach([&](Entity e, ...) {
            Entity render_e = render->CreateEntity();
            render->AddComponent(render_e, RenderTransform{ t.matrix });
            render->AddComponent(render_e, RenderMesh{ mesh.vb_handle, mesh.ib_handle });
            render->AddComponent(render_e, RenderMaterial{ mat.pipeline, mat.bind_group });
        });
    }
};

// 渲染世界的 System 生成命令
class GenerateCommandsSystem : public ISystem {
    void Update(World* render, Resource<FrameContext> ctx) {
        // 遍历 RenderMesh，写入 CommandBuffer
    }
};
```

**适用场景**：复杂渲染管线、需要主线程和渲染线程并行（逻辑更新与命令录制重叠）、需要多个视图同时渲染（如编辑器中的多个视口）。

**隐藏代价**：
- **Extract 阶段的同步开销**：每帧把数据从主 World 复制到 Render World，有 CPU 和内存开销。如果数据量大（数万个实体），Extract 本身可能成为瓶颈。
- **架构复杂度**：两个 World 的组件类型、System 调度、生命周期管理都翻倍。
- **调试困难**：问题可能出在主 World（数据错了）、Extract 阶段（复制漏了）、或 Render World（渲染管线 bug），排查链路变长。

**失效条件**：当项目规模很小（< 1000 个可渲染实体）、渲染逻辑简单（单前向 Pass）时，双 World 的开销大于收益。

### 引擎对照

> 我们在解决的是「ECS 架构下 RHI 该如何表达」这个问题。
>
> - **chaos** 是传统的 OOP 引擎，RHI 层使用单例模式（`DynamicRHI::getInstance()`）。上层渲染模块直接持有 `RHICommandContext*` 指针。这与 ECS 无关，但我们可以从中学习它的**接口分层**（底层 graphics_interface 与高层 RHI 分离）。
> - **UE** 也不是 ECS 引擎，但它的**渲染线程模型**与分支 C 有精神共鸣：主线程（Game Thread）更新场景，渲染线程（Render Thread）读取场景代理（SceneProxy）并录制命令，RHI 线程再翻译执行。三层线程的分离本质上是一种"逻辑世界 -> 提取 -> 渲染世界"的变体。
> - **Bevy** 是**分支 C 的典范**：`bevy_render` 创建了独立的 `Render World`，`RenderDevice`、`RenderQueue`、`RenderAdapter` 都是 Render World 的 `Resource`。主 World 和 Render World 通过 `Extract` schedule 同步。Bevy 的 ECS scheduler 天然支持 Render World 的 System 并行执行。

### 决策分析

**默认推荐：分支 C（分离的 Render World / 双 World 模型），它是 UE 渲染线程模型在 ECS 架构中的自然表达。**

理由：
1. **UE 的渲染线程模型本质上就是双 World 的 OOP 版本**。UE 有三层线程解耦：Game Thread 更新场景状态（对应 ECS 主 World 的逻辑 System），Render Thread 读取 SceneProxy 并生成渲染命令（对应 Render World 的生成命令 System），RHI 线程翻译为底层 API（对应 Render World 的提交 System）。这个分层不是"为了复杂而复杂"，而是解决以下问题的**系统性方案**：
   - **CPU 与 GPU 的流水线并行**：主 World 在准备第 N+1 帧的逻辑时，Render World 在录制第 N 帧的渲染命令，GPU 在执行第 N-1 帧的绘制——三层流水线重叠。
   - **多视图隔离**：编辑器中同时存在 Scene View、Game View、PIE View，每个视图有独立的相机、裁剪、渲染设置。在双 World 模型中，每个视图对应一套 Render World 配置。
   - **逻辑帧率与渲染帧率解耦**：Gameplay 可以以 30Hz 更新，渲染以 60Hz 插值呈现。单 World 模型很难做到这一点。
   - **确定性快照**：Render World 是主 World 在某一帧的"投影"，它的状态是只读的（相对于主 World 的当前状态）。这让帧级回放和 AI 可观测性更容易——你可以快照 Render World 来完整复现一帧的渲染输入。
2. **Bevy 验证了双 World 在 ECS 中的可行性**。Bevy 的 `bevy_render` 创建了独立的 `Render World`，`RenderDevice`、`RenderQueue` 都是 Render World 的 `Resource`，主 World 和 Render World 通过 `Extract` schedule 同步。Bevy 的 ECS scheduler 天然支持 Render World 的 System 并行执行。这意味着"双 World + ECS"不是理论设想，而是已经有成熟开源实现的路径。
3. **分支 B（ECS Resource 单 World）看似简单，实则埋雷**。如果在主 World 中直接由 `MeshRenderSystem` 生成渲染命令，会出现以下问题：
   - 逻辑 System 和渲染 System 的调度耦合：如果 `PhysicsSystem` 和 `MeshRenderSystem` 都跑在主 World 的同一调度图中，你无法让渲染以不同频率运行。
   - 多视图困难：编辑器需要同时渲染 Scene View 和 Game View，单 World 中需要两个 `MeshRenderSystem` 实例共享同一个 `CommandContext`，状态管理混乱。
   - AI 可观测性受损：主 World 的状态在渲染过程中被修改（如动画 System 更新了 Transform，但同一帧的渲染应该看到这些 Transform 的上一帧状态还是当前状态？），导致非确定性。
4. **分支 A（单例）已被排除**：它与 ECS 架构的核心理念冲突，AI 可观测性从起点就被破坏。

**"渐进式双 World"实施策略**：
- **阶段 5 初期（最小可行双 World）**：
  - 主 World 包含 `Transform`、`Mesh`、`Material`、`Camera` 组件。
  - 每帧的 Extract 阶段：一个 `ExtractRenderDataSystem` 遍历主 World 的可渲染实体，把 `Transform.matrix`、`Mesh.vb_handle`、`Material.pipeline` 复制到 Render World 的 `RenderTransform`、`RenderMesh`、`RenderMaterial` 组件中。这个复制操作是**纯内存拷贝**，不涉及复杂计算。
  - Render World 包含 `RenderDevice`、`RenderQueue`、`FrameContext` Resource，以及 `RenderCamera`、`RenderMesh` 等组件。
  - `GenerateCommandsSystem` 在 Render World 中遍历 `RenderMesh`，写入 `IRHICommandList`。
  - `PresentSystem` 在 Render World 的帧末提交命令并呈现。
  - **简化点**：Extract 阶段可以单线程顺序执行（Bevy 的 `Extract` schedule 默认也是单线程的）。主 World 和 Render World 之间不共享任何可变状态——Extract 是单向只读复制。
- **阶段 5 中期**：
  - 引入**多视图支持**：`Camera` 组件带 `view_id`，Extract 阶段为每个 `view_id` 生成独立的 Render World 命令列表（或 Render World 中的独立 Pass 组）。
  - 引入**逻辑/渲染帧率解耦**：Render World 的调度频率可以与主 World 不同（如主 World 30Hz，Render World 60Hz，动画插值在 Extract 阶段完成）。
- **阶段 5 后期**：
  - 引入**异步 Extract**：主 World 的逻辑更新和 Render World 的命令生成完全并行（需要双缓冲主 World 的渲染相关组件）。这是 UE 的 "Game Thread || Render Thread" 模型的 ECS 化。

**关键设计原则**：
- **Extract 必须是无副作用的纯读取**：`ExtractRenderDataSystem` 只能读主 World 的组件，不能写。这保证了主 World 的确定性不受渲染影响。
- **Render World 的组件是主 World 组件的"渲染投影"**：`RenderMesh` 包含 `vb_handle`、`ib_handle`、`pipeline`，但不包含 `Mesh` 的游戏逻辑数据（如碰撞体引用、LOD 距离）。这种分离让 Render World 的数据量远小于主 World。
- **RHI Resource 只存在于 Render World**：`RenderDevice`、`RenderQueue`、`SwapChain` 是 Render World 的 `Resource`，主 World 不感知它们的存在。

**AI 友好设计检查**：
- **状态平铺**：`RenderDevice`/`FrameContext` 是 Render World Resource；`RenderMesh`/`RenderMaterial` 是 Render World Component。AI 可以通过 ECS Schema 查询整个 Render World 的状态。
- **自描述**：所有 Resource/Component 类型通过阶段 4.4 的反射系统注册，AI 可读 Schema。
- **确定性**：给定相同的主 World 状态，Extract 阶段生成相同的 Render World 状态；给定相同的 Render World 状态，System 调度图生成相同的命令序列。帧级回放只需快照 Render World。
- **工具边界**：AI 不应直接操作 `CommandContext`，但可以修改主 World 的 `Material` 组件（Extract 后影响 Render World）或 Render World 的 `RenderSettings` Resource。这些高层数据的 Schema 是结构化的 JSON。
- **Agent 安全**：渲染系统的组件操作受 ECS 事务保护。AI Agent 修改 `Material.color` 只影响渲染结果，不会崩溃引擎。

**遗留问题**：RHI 层的全局性问题已经解决——我们用双 World 模型把 RHI Resource 隔离在 Render World 中。但把这些模块拼成一张完整的地图：从主 World 的 Transform 修改到屏幕上的像素，数据究竟经历了哪些阶段、哪些同步点、哪些状态转换？下一节将给出从主 World 到 Render World 再到屏幕像素的完整生命周期全景。

## 问题 7：在 ECS 架构下，一帧的完整生命周期是什么样的？

### 从主 World 状态到屏幕上像素的完整数据流

经过前六个问题的逐步拆解，我们终于可以把所有模块拼接成一张完整的地图。这不是一个固定的代码模板，而是一个**可验证的思维模型**——你可以对照它检查你的实现是否遗漏了关键环节。

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        帧 N 的生命周期（ECS 映射）                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐                                                        │
│  │   主 World       │  逻辑更新阶段                                           │
│  │  (Main World)   │  ─────────────────                                      │
│  │                 │  InputSystem:        读取输入，写入 Input Resource        │
│  │  Component:     │  PhysicsSystem:      更新速度/位置 → LocalTransform      │
│  │  - LocalTransform│  AnimationSystem:    插值骨骼，更新 LocalTransform      │
│  │  - GlobalTransform│ HierarchySystem:    PropagateDirty → 更新 GlobalTransform│
│  │  - MeshHandle   │  GameLogicSystem:    游戏逻辑修改组件状态                 │
│  │  - MaterialHandle│                                                       │
│  │  - Camera       │  产出：第 N+1 帧的"目标世界状态"（但尚未被渲染读取）      │
│  └────────┬────────┘                                                        │
│           │ ExtractSchedule（帧边界）                                       │
│           ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐        │
│  │  ExtractRenderablesSystem:                                      │        │
│  │    Query<(MeshHandle, MaterialHandle, GlobalTransform)>         │        │
│  │    → 复制到 Render World 的 (RenderMesh, RenderMaterial,        │        │
│  │       RenderTransform)                                         │        │
│  │                                                                 │        │
│  │  ExtractCameraSystem:                                           │        │
│  │    Camera (主 World) → ExtractedView (Render World)             │        │
│  │    含：ViewMatrix, ProjMatrix, ViewFrustum, Viewport            │        │
│  │                                                                 │        │
│  │  注：Extract 是"快照"——主 World 在 Extract 后立即继续下一帧    │        │
│  │       逻辑，不需要等待 Render World 完成。                       │        │
│  └────────────────────────────────┬────────────────────────────────┘        │
│                                    │                                        │
│  ┌─────────────────────────────────▼────────────────────────────────┐       │
│  │                      Render World                                 │       │
│  │                 (Render Thread / 独立线程)                        │       │
│  ├──────────────────────────────────────────────────────────────────┤       │
│  │  Stage 1: PrepareAssets                                          │       │
│  │  ─────────────────                                               │       │
│  │  PrepareMeshSystem:    RenderMesh.handle → GpuBuffer vb/ib       │       │
│  │  PrepareMaterialSystem: RenderMaterial.handle → PipelineKey      │       │
│  │                         + BindGroup (Shader Resource 绑定)       │       │
│  │  PrepareTexturesSystem: 上传本帧需更新的纹理到 GPU               │       │
│  │  产出：所有 GPU 资源已就绪，Handle 已解析为实际 GPU 对象          │       │
│  ├──────────────────────────────────────────────────────────────────┤       │
│  │  Stage 2: Culling（可见性判断）                                   │       │
│  │  ────────────────────────                                        │       │
│  │  FrustumCullSystem:    遍历 RenderWorld 实体，用 ExtractedView   │       │
│  │                         的 Frustum 与 Aabb 做相交测试             │       │
│  │  OcclusionCullSystem:  [可选] 用上一帧的 Z-Buffer 做 HZB 剔除   │       │
│  │  产出：ViewVisibleList Resource（可见实体 ID 数组）               │       │
│  ├──────────────────────────────────────────────────────────────────┤       │
│  │  Stage 3: Queue（Draw Call 组织）                                 │       │
│  │  ─────────────────────                                           │       │
│  │  QueueShadowSystem:    对阴影投射者生成 ShadowPhase DrawItems    │       │
│  │  QueueOpaqueSystem:    对可见不透明物体生成 OpaquePhase Items     │       │
│  │                         按 PSO+Material Binning                   │       │
│  │  QueueTransparentSystem: 对透明物体按深度排序生成 Transparent     │       │
│  │                           Phase Items                             │       │
│  │  QueueUISystem:        生成 UI Phase Items                       │       │
│  │  产出：各 Phase 的 Item 数组已就绪                                │       │
│  ├──────────────────────────────────────────────────────────────────┤       │
│  │  Stage 4: RenderGraph（Pass 调度）                                │       │
│  │  ────────────────────────                                        │       │
│  │  BuildRenderGraphSystem: 根据 Phase Items 和管线配置构建 Render  │       │
│  │                          Graph，声明各 Pass 的读写资源            │       │
│  │  CompileRenderGraphSystem: 拓扑排序、Cull 无效 Pass、推导 Barrier │       │
│  │  产出：已编译的 RenderGraph Resource                             │       │
│  ├──────────────────────────────────────────────────────────────────┤       │
│  │  Stage 5: Execute（命令录制与提交）                               │       │
│  │  ───────────────────────                                         │       │
│  │  RecordCommandsSystem: 遍历 RenderGraph 的排序后 Pass，在        │       │
│  │                        ICommandContext 上录制命令                 │       │
│  │  SubmitSystem:         Submit CommandBuffer 到 GPU 队列           │       │
│  │  PresentSystem:        SwapChain->Present()，触发屏幕刷新         │       │
│  │  FrameFenceSystem:     等待 GPU 完成，回收延迟删除资源            │       │
│  │  产出：像素出现在屏幕上                                          │       │
│  └──────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  时间线示意（并行视图）：                                                    │
│                                                                             │
│  CPU 主线程:  [Main World 帧 N+1 逻辑] ────── [Main World 帧 N+2 逻辑]     │
│                    │                                                        │
│  CPU 渲染线程:     [Extract N] → [Render World 帧 N]                        │
│                                              │                             │
│  GPU:                                        [执行帧 N 命令] → [Present N]  │
│                                                                             │
│  注意：渲染线程滞后主线程一帧（或更多），这是延迟管线的核心特征。            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 关键决策回顾

在这个完整生命周期中，每一步都对应了前面的一个问题：

| 阶段 | 对应问题 | 核心决策 |
|------|---------|---------|
| Extract | 问题 1 & 2 | 双 World + 延迟管线。主 World 与 Render World 通过 Extract 解耦，并行无需锁。 |
| PrepareAssets | 问题 2 | Handle 解析延迟到 Prepare 阶段，避免 Extract 阻塞在资源加载上。 |
| Culling | 问题 3 | 阶段 5 初期用逐实体视锥测试，产出 `ViewVisibleList` Resource。预留 BVH/GPU-driven 路径。 |
| Queue | 问题 4 | Opaque 用 Binned Phase，Transparent 用 Sorted Phase。Instancing 作为 Bin 内自动优化。 |
| RenderGraph | 问题 5 | 简化版 Render Graph：声明式 Pass、自动 Barrier、拓扑排序、死 Pass Culling。预留瞬态资源别名。 |
| Execute/Submit | 问题 6 | 阶段 5 初期单线程录制+提交。接口预留 RHI 线程和多上下文并行。 |

### 与三引擎的架构对照

> **chaos** 的 `tickPreloads` → `tickMain` → `tickPostloads` + `tickRender` + `tickDebug` 循环映射到：
> - `tickPreloads` ≈ Extract + PrepareAssets（资源预加载和解析）。
> - `tickMain` ≈ Render World 的 Culling + Queue + RenderGraph + Execute。
> - `tickRender` 中的多线程命令列表池 ≈ 我们的 `ICommandContext` 多路并行录制（预留）。
> - `waitForRender()` 和 `waitForSingleTaskCounter` ≈ 我们的帧边界 Fence 同步。
> - chaos 的 `RenderGraph`（含 `buildDependency`、`cullPasses`）≈ 我们的简化版 Render Graph 编译器。
> - chaos 的 `RenderView`（每个视图有独立 Shadow/GBuffer/Lighting Graph）≈ 我们的 `ExtractedView` + 按视图独立的 RenderGraph。
>
> **UE** 的三线程模型映射到：
> - Game Thread ≈ 主 World 的 Update Schedule（逻辑 System）。
> - Render Thread ≈ Render World 的 Render Schedule（Culling/Queue/RenderGraph/Record）。
> - RHI Thread ≈ 我们的 RHISubmitSystem（翻译引擎命令 + Submit + Present + Fence 等待）。
> - `ENQUEUE_RENDER_COMMAND` ≈ Extract 阶段的数据拷贝 + ViewFamily 提交。
> - `FRDGBuilder` ≈ 我们的 `RenderGraph` Resource。
> - `FViewFamily` / `FViewInfo` ≈ 我们的 `ExtractedView` Resource。
> - `FFrameEndSync` ≈ 我们的 `FrameFenceSystem`。
>
> **Bevy** 的双 World 模型映射到：
> - 主 World Update Schedule ≈ 我们的主 World 逻辑阶段。
> - `ExtractSchedule` + `ExtractPlugin` ≈ 我们的 Extract 阶段 + 跨世界实体映射。
> - `mem::replace` 主 World ≈ 我们的 Extract 快照语义。
> - `RenderApp` + `Render` Schedule ≈ 我们的 Render World + Stage 1~5。
> - `BinnedRenderPhase` / `SortedRenderPhase` ≈ 我们的 Phase 分箱/排序。
> - `Core3d` / `Core2d` Schedule（Bevy 0.12+ 将独立的 `RenderGraph` 重构为基于 Schedule 的系统链）≈ 我们的 RenderGraph Compile + Execute。Render Pass 作为常规 System 运行，通过 `before`/`after` 依赖排序。Bevy 选择将图依赖直接编码在 ECS Schedule 中，而我们保留了独立的 `RenderGraph` Resource 以支持显式资源声明和瞬态别名。
> - `SystemBuffer::queue` + `PendingCommandBuffers` + `render_system` Present ≈ 我们的 Submit + Present。

### 为什么这个架构是"AI 友好"的？

回顾我们在每章末尾提到的 AI 可观测性原则，这个生命周期设计如何体现？

1. **状态平铺**：Render World 的所有中间产物（`ViewVisibleList`、`BinnedPhaseItems`、`RenderGraph`）都是显式的 ECS Resource，不是隐藏在调用栈中的局部变量。AI Agent 可以在任意 System 之间快照这些 Resource。

2. **自描述**：每个 Resource 都有明确的类型名和结构定义。`ViewVisibleList` 就是一个 `Array<Entity>`，不是某种晦涩的句柄或指针。

3. **确定性**：给定相同的主 World 状态，Extract → Render 的产出是确定的（假设没有随机数或时间相关的渲染效果）。AI 可以"重放"一帧的渲染过程。

4. **工具边界**：每个 Stage 都是一个 System 或 SystemSet，有明确的 `before`/`after` 约束。AI 可以安全地插入诊断 System（如"打印当前帧 Draw Call 数量"）而不破坏现有管线。

5. **Agent 安全**：Render World 是只读的（从主 World 角度），AI 即使在 Render World 中执行查询，也不会意外修改主 World 的游戏状态。


> **下一步**：[[可见性判断与空间加速结构]]，因为 Render World 已经建立，Extract 完成了数据复制。下一个问题是：这 10000 个实体中，哪些真的需要被画？
