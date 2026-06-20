---
order: 36
title: 渲染队列与 DrawCall 组织
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - render-queue
  - drawcall
  - sorting
  - batching
aliases:
  - 渲染队列与 DrawCall 组织
---

> **前置依赖**：[[ECS架构下的渲染世界设计]]
> **本模块增量**：深入理解排序键（Sort Key）的设计空间、Binned Phase vs Sorted Phase 的权衡、Instancing 实例化原理、以及透明物体的深度排序策略。你将能够把 500 个 Draw Call 合并为 20 个批次。
>
> 本笔记探讨的核心问题是：**找到可见物体后，如何把它们变成 GPU 命令？如何排序才能让 Early-Z 最优、合批最多、透明混合正确？**

## 问题 4：找到可见物体后，如何把它们变成 GPU 命令？

### 场景与根因

剔除后你有 500 个可见实体。最 naive 的做法是：

```cpp
for (Entity e : visible_entities) {
    auto* mesh = GetMesh(e);
    auto* mat = GetMaterial(e);
    cmd->SetPipeline(mat->pso);
    cmd->SetBindGroup(mat->bind_group);
    cmd->SetVertexBuffer(mesh->vb);
    cmd->SetIndexBuffer(mesh->ib);
    cmd->DrawIndexed(mesh->index_count);
}
```

这在概念上正确，但性能上灾难。为什么？

**根因在于：GPU 的状态切换是极其昂贵的，而 Draw Call 的数量直接决定了 CPU 的开销。**

每一次 `SetPipeline` 都可能触发驱动重新验证几百个状态位；每一次 `SetBindGroup` 都要更新描述符表；每一次 `DrawIndexed` 都要往命令缓冲里写入数十个字节。500 个实体意味着 500 次这些操作。如果每帧预算 16ms，而 CPU 录制 500 次 Draw Call 需要 2~3ms，你只剩下 13ms 给逻辑、物理、动画、剔除、后处理……

更深层的问题是：**这 500 个实体并不是随机排列的**。它们中可能有 200 个使用同一种材质、100 个使用另一种。如果不加组织，你会在两种材质之间来回切换 200 次——而每次切换都是一次独立的 PSO 绑定开销。

所以，CPU 侧的核心任务不是"逐个发 Draw Call"，而是**把可见实体按 GPU 状态分组排序，让相同状态的实体连续绘制，从而把 500 个独立的 Draw Call 合并成 20 个批次（Batch）**。

### 分支研究

#### 分支 A：逐实体绘制，不做任何排序合批

**核心思路**：按实体遍历顺序直接绘制，不排序、不合批。

**适用场景**：实体数量极少（<100）、教学 demo、验证管线正确性。

**隐藏代价**：
- **PSO 切换爆炸**。如果实体按 EntityID 排列（与材质无关），相邻实体可能使用完全不同的 PSO，导致每帧数百次状态切换。
- **无 Early-Z 优化**。不透明物体如果按随机顺序绘制，后面的物体会执行大量无效像素着色（被前面的深度测试丢弃），浪费 GPU 算力。
- **透明物体渲染错误**。透明物体必须从远到近绘制（Painter's Algorithm），随机顺序会导致混合结果错误。

**失效条件**：实体数量 > 200、或存在透明物体、或性能敏感场景。

#### 分支 B：排序键（Sort Key）——按状态排序后绘制

**核心思路**：为每个可见实体生成一个 64 位（或 32 位）的排序键，编码渲染阶段、PSO ID、材质 ID、深度等信息。然后按排序键对整个列表排序，最后顺序绘制。排序后，相同 PSO 和材质的实体天然聚集在一起。

```cpp
struct DrawItem {
    uint64_t sort_key;
    Entity   entity;
    MeshHandle mesh;
    MaterialHandle material;
    Mat4     world_matrix;
};

// 生成排序键（概念）
uint64_t ComputeSortKey(const Material& mat, float depth, RenderPhase phase) {
    // 高 8 位：渲染阶段（Opaque = 0, AlphaMask = 1, Transparent = 2）
    // 接下来 16 位：PSO ID
    // 接下来 16 位：材质 ID
    // 低 24 位：深度（不透明用正序，透明用逆序）
    uint64_t key = 0;
    key |= (uint64_t)phase << 56;
    key |= (uint64_t)mat.pso_id << 40;
    key |= (uint64_t)mat.material_id << 24;
    
    if (phase == RenderPhase::Transparent) {
        key |= (0xFFFFFF - DepthToUint24(depth));  // 透明：从远到近
    } else {
        key |= DepthToUint24(depth);               // 不透明：从前到后（优化 Early-Z）
    }
    return key;
}

// Queue 阶段
void QueueDraws(const VisibleList& visible, Array<DrawItem>* items) {
    items->reserve(visible.size());
    for (Entity e : visible) {
        DrawItem item;
        item.sort_key = ComputeSortKey(GetMaterial(e), GetDepth(e), GetPhase(e));
        item.entity = e;
        // ...
        items->push_back(item);
    }
    std::sort(items->begin(), items->end(), 
              [](const DrawItem& a, const DrawItem& b) { return a.sort_key < b.sort_key; });
}
```

**适用场景**：几乎所有需要合批的渲染场景。

**隐藏代价**：
- **排序开销**。500 个元素用 `std::sort` 约需 O(n log n) ≈ 5000 次比较。对于 10000 个元素，比较次数约 13 万。这在 CPU 上是亚毫秒级的，但如果每帧都要全排，累积不可忽视。
- **排序键设计是权衡艺术**。如果 PSO ID 放在高位、深度放在低位，那么同一 PSO 下的物体会聚集，但深度顺序会被打乱（对 Early-Z 不利）。如果深度放在高位、PSO 放在低位，Early-Z 最优，但 PSO 切换增加。没有 universally optimal 的排序键——它取决于场景特征（PSO 多样性 vs Overdraw 严重性）。
- **动态物体每帧重排**。如果大部分物体每帧都在动（如粒子系统），排序键变化频繁，无法利用帧间连续性。

**失效条件**：实体数量极大（>10万）且高度动态，全量排序成为 CPU 瓶颈。

#### 分支 C：分箱（Binning）——用哈希桶代替全局排序

**核心思路**：不为每个实体生成排序键，而是直接把实体丢进按 PSO+材质组合的"箱子"（Bin）里。不透明物体不需要严格的深度顺序（只要同箱内连续绘制即可），箱子之间按阶段顺序遍历。

```cpp
struct RenderBin {
    PipelineKey pso;
    MaterialKey material;
    Array<Entity> entities;
};

HashMap<uint64_t, RenderBin> bins;

void BinDraws(const VisibleList& visible) {
    for (Entity e : visible) {
        uint64_t bin_key = CombineHash(GetPSO(e), GetMaterial(e));
        bins[bin_key].entities.push_back(e);
    }
}

// 绘制时遍历所有 bin
for (auto& [key, bin] : bins) {
    cmd->SetPipeline(bin.pso);
    cmd->SetMaterial(bin.material);
    for (Entity e : bin.entities) {
        cmd->SetTransform(GetMatrix(e));
        cmd->DrawIndexed(GetMesh(e)->index_count);
    }
}
```

**适用场景**：不透明物体为主、PSO 种类相对少、不需要严格深度排序。

**隐藏代价**：
- **箱内无序**。如果一个大建筑（近距离）和一片草地（远距离）进了同一个 bin（因为用了相同材质），建筑先画还是草地先画是随机的。如果草地先画，建筑会覆盖它——这是正确的；但如果建筑先画，草地的像素仍然会被深度测试丢弃——Early-Z 帮我们节省了像素着色开销。但如果 bin 内顺序随机，平均而言只有 50% 的物体能享受到 Early-Z 优化。
- **透明物体无法用 binning**。透明物体必须严格从远到近，binning 破坏了这一点。

**失效条件**：透明物体多、或对 Overdraw 极度敏感的场景（如复杂室内）。

#### 分支 D：实例化（Instancing）——真正的 Draw Call 合并

**核心思路**：如果 200 棵树使用完全相同的网格和材质，它们不应该发 200 次 Draw Call，而应该发 1 次 `DrawIndexedInstanced(instance_count=200)`。CPU 只需要设置一次 PSO、绑定一次资源，GPU 通过 `SV_InstanceID` 区分不同实例并读取各自的变换矩阵。

```cpp
// CPU 侧：收集所有相同 mesh+material 的实例数据
struct InstanceData {
    Mat4 world_matrix;
    uint material_variant;  // 如果有材质变体
};

HashMap<MeshMaterialKey, Array<InstanceData>> instance_batches;

void BuildInstanceBatches(const VisibleList& visible) {
    for (Entity e : visible) {
        auto key = MakeKey(GetMesh(e), GetMaterial(e));
        instance_batches[key].push_back({GetMatrix(e), /* ... */});
    }
}

// 上传 InstanceData 到 GPU 的 Instance Buffer
// 绘制时：cmd->DrawIndexedInstanced(index_count, instance_count, ...)
```

**适用场景**：大量重复物体（树木、草丛、碎石、人群、粒子）。

**隐藏代价**：
- **需要 Instance Buffer**。所有实例的 per-instance 数据（变换矩阵、颜色变化等）必须打包到一个连续的 GPU 缓冲区中，每帧更新。
- **不能处理材质差异**。如果 200 棵树中有一棵是秋天的黄叶（不同纹理），它必须脱离实例批次单独绘制。
- **GPU 侧有额外开销**。`SV_InstanceID` 虽然便宜，但实例数量极大时（>1000），instance buffer 的读取可能成为瓶颈。

**失效条件**：场景中几乎没有重复物体、或实例差异大无法共享同一批次。

### 引擎对照

> **参考：chaos / UE / Bevy 对「Draw Call 组织」是怎么做的？**
>
> - **chaos** 从源码推断使用了 `MeshDrawCommand` 结构来封装单个绘制所需的一切状态，支持多线程并行生成命令后统一提交。这接近分支 B（排序键）的设计——先生成命令包，再按某种键排序提交。chaos 的 `RenderPipeline::renderSingleViewTask()` 内部可能有按 Pass 和材质分组的逻辑。
> - **UE** 使用了**工业级最复杂的合批体系**：`FMeshBatch` → `FMeshDrawCommand` → `FParallelMeshDrawCommandPass`。`FMeshBatch` 是 Game Thread 提交的原始渲染请求；`FMeshDrawCommand` 是 Render Thread 生成的、包含完整排序键和 GPU 状态的命令；`FParallelMeshDrawCommandPass` 按排序键排序并合并为最终的 Draw Call。UE 还支持 **GPU Scene**（Nanite）和 **Instanced Stereo**（VR 中双眼实例化），将合批推向了极致。UE 的动机是：大型开放世界可能有数十万个图元，没有极致的合批，CPU 根本无法驱动。
> - **Bevy** 在 0.19 中引入了 **BinnedRenderPhase** 和 **SortedRenderPhase** 两种策略，这正是分支 C 和分支 B 的对应实现：
>   - `BinnedRenderPhase` 用 `BinKey`（PSO + DrawFunction + Material BindGroup + Mesh）把实体分箱，箱内不排序，适合不透明物体。
>   - `SortedRenderPhase` 用视图空间深度排序，适合透明物体。
>   - Bevy 还引入了 **GPU preprocessing** 和 **multidraw indirect**：CPU 只维护 entity→bin 映射，Compute Shader 生成 instance buffer 和 indirect draw parameters，进一步减少 CPU 开销。

### 决策分析与推荐

**默认推荐：分支 B（排序键）+ 分支 C（分箱）的混合——不透明物体用 Binned Phase，透明物体用 Sorted Phase。预留 Instancing 接口作为优化路径。**

理由如下：

1. **UE 的 `FMeshDrawCommand` 证明了排序键是工业级渲染的基石**。无论合批策略如何变化，"为每个可见物体生成一个包含所有排序信息的命令结构"这一层是不可跳过的。它是连接"可见性判断"和"GPU 命令录制"的标准接口。

2. **Bevy 的 Binned + Sorted 双 Phase 是 ECS 下最清晰的组织方式**。它不需要全局排序所有物体，而是按渲染阶段（Opaque、AlphaMask、Transparent、Shadow 等）分别组织。每个阶段选择最适合的策略：Opaque 用 Binning 最小化 PSO 切换，Transparent 用 Sorting 保证混合正确。这与 ECS 的"按组件类型分组"哲学天然契合。

3. **Instancing 是优化，不是架构**。Instance Batch 的构建可以在 Binned Phase 内部完成——当某个 Bin 中的实体数量 > 1 且使用相同 Mesh+Material 时，自动升级为 Instance Batch。这不需要改变 Phase 的外部接口。

4. **排序键设计应预留足够位宽**。UE 使用 64 位排序键，Bevy 也使用多字段组合键。建议预留：高 8 位为 Phase，接下来 24 位为 PSO/Material，低 32 位为深度。这种分配在不透明阶段允许 2^24 种材质组合，在透明阶段允许 2^32 的深度精度，对默认实现完全充足。

**具体实施策略**：

```cpp
// 渲染阶段枚举
enum class RenderPhase : uint8_t {
    ShadowMap = 0,
    Opaque3D  = 1,
    AlphaMask = 2,
    Transparent3D = 3,
    UI        = 4,
};

// 排序键：64 位
union SortKey {
    uint64_t value;
    struct {
        uint32_t depth;         // 透明：view-space Z；不透明：可复用为其他标志
        uint16_t material_id;   // 材质/PSO 组合标识
        uint8_t  phase;         // RenderPhase
        uint8_t  reserved;      // 未来扩展
    };
};

// Phase Item：进入渲染队列的最小单元
struct PhaseItem {
    SortKey sort_key;
    Entity  render_entity;
    uint32_t instance_start;  // 用于 instancing
    uint32_t instance_count;  // 1 = 普通绘制，>1 = 实例化绘制
};

// Binned Phase：按 sort_key 的高 32 位分箱
class BinnedRenderPhase {
    HashMap<uint32_t, Array<PhaseItem>> bins;  // key = phase + material_id
public:
    void AddItem(PhaseItem item) {
        uint32_t bin_key = item.sort_key.value >> 32;
        bins[bin_key].push_back(item);
    }
    void Render(ICommandContext* ctx) {
        for (auto& [key, items] : bins) {
            // 设置 PSO、材质（同一 bin 共享）
            BindPipelineAndMaterial(key);
            for (auto& item : items) {
                if (item.instance_count > 1)
                    DrawInstanced(item);
                else
                    DrawSingle(item);
            }
        }
    }
};

// Sorted Phase：透明物体，全局按 depth 排序
class SortedRenderPhase {
    Array<PhaseItem> items;
public:
    void AddItem(PhaseItem item) { items.push_back(item); }
    void Prepare() { std::sort(items.begin(), items.end(), 
                               [](auto& a, auto& b){ return a.sort_key.depth < b.sort_key.depth; }); }
    void Render(ICommandContext* ctx) {
        for (auto& item : items) {
            BindPipelineAndMaterial(item.sort_key);
            DrawSingle(item);
        }
    }
};
```

**关键设计点**：
- **Phase 是显式的 ECS Resource**。`ViewBinnedPhases` 和 `ViewSortedPhases` 作为 Render World 的 Resource，由 `Queue` 阶段的 System 填充，由 `Render` 阶段的 System 消费。这允许用户插件注册自定义 Phase（如自定义的后期效果前通道）。
- **Instancing 是自动的**。在 `PrepareResourcesBatchPhases` 阶段，扫描每个 Bin，如果 Bin 内所有 item 的 mesh 和 material 完全相同，合并为一个 `instance_count = N` 的 item，并生成 instance buffer。
- **排序是稳定的**。对于透明物体，相同深度的物体按材质 ID 二次排序，减少 PSO 切换。

**遗留问题**：现在我们已经有了按 Phase 组织的 Draw Item 列表，但渲染不只是"画物体"。一帧通常包含多个 Pass：Shadow Map、GBuffer、Lighting、PostProcess……每个 Pass 可能读取或写入不同的纹理和缓冲。如果手动管理这些资源的状态转换（Barrier）和生命周期，代码会迅速变成不可维护的意大利面条。如何系统地管理一帧内的多 Pass 资源依赖？


> **下一步**：[[RenderGraph与多Pass资源管理]]，因为单个 Pass 的渲染队列已经跑通。下一个问题是：Shadow → GBuffer → Lighting → PostProcess 等多 Pass 之间的资源依赖和状态转换如何系统化管理？
