---
title: DrawCall与合批
description: RenderPass 把一帧拆成多个 Pass，但每个 Pass 里可能还有几百个 Draw Call。理解 Draw Call 的 CPU 开销、排序键、分箱、Instancing，以及如何用 ECS 组件组织支持自动合批。
date: 2026-06-28
tags:
  - graphics
  - draw-call
  - batching
  - instancing
  - sorting-key
  - render-queue
  - early-z
  - ecs
aliases:
  - Draw Call and Batching
  - 合批
  - Instancing
  - 渲染队列
---

> **前置依赖**：[[Notes/计算机图形学/引擎渲染架构/RenderPass抽象|RenderPass抽象]] — 你已经能把一帧拆成多个 Pass，每个 Pass 负责一类渲染任务
> **本模块增量**：学完这篇笔记后，你能解释 Draw Call 的 CPU 开销来源，能设计排序键和分箱策略减少状态切换，能实现基于 Instancing 的合批，并理解 ECS 组件组织如何影响合批效率。
> **下一步**：[[Notes/计算机图形学/引擎渲染架构/渲染状态管理|渲染状态管理]] — 合批后状态切换少了，但状态本身怎么管理？深度排序、透明混合、Early-Z 怎么保证正确？

---

# DrawCall 与合批

## 问题 0：Pass 里有一堆 Draw Call

在 [[Notes/计算机图形学/引擎渲染架构/RenderPass抽象|RenderPass抽象]] 里，我们把一帧拆成了多个 Pass。每个 Pass 的执行体大概长这样：

```cpp
void ScenePass::Execute(ICommandList* cmd) {
    for (auto& obj : visibleObjects) {
        cmd->SetPipeline(obj.material->pipeline);
        cmd->SetVertexBuffer(obj.mesh->vertexBuffer);
        cmd->SetIndexBuffer(obj.mesh->indexBuffer);
        cmd->SetConstantBuffer(0, obj.transformMatrix);
        cmd->DrawIndexed(obj.mesh->indexCount);
    }
}
```

如果场景里有 500 个可见物体，这个 Pass 就要发 500 次 Draw Call。每次 Draw Call 前面还有 `SetPipeline`、`SetVertexBuffer`、`SetConstantBuffer` 等状态设置。

**问题不在于 GPU 画不画得动，而在于 CPU 提交不过来。**

现代 GPU 每秒能画几十亿个三角形，但 CPU 每秒能准备的 Draw Call 数量是有限的。在移动设备上，这个数字可能只有几百；在 PC 上，几千到几万。如果每个物体都独立发一次 Draw Call，CPU 很快会成为瓶颈。

这就是合批（Batching）要解决的问题。

---

## 问题 1：为什么 Draw Call 数量是瓶颈？

最 naive 的想法是：“GPU 不是很快吗？多叫几次怎么了？”

但每次 Draw Call 背后，CPU 要做很多事：

1. **状态验证**：驱动检查当前的 PSO、顶点布局、纹理绑定是否合法
2. **命令缓冲写入**：把 `DrawIndexed` 命令写到命令缓冲区
3. **资源绑定**：确认 VBO、IBO、纹理、常量缓冲都 ready
4. **用户态/内核态切换**：某些 API 需要切换到内核态提交命令

这些开销和“画多少个三角形”关系不大。画 100 个三角形的 Draw Call 和画 10 万个三角形的 Draw Call，CPU 开销几乎一样。

**关键认知**：

> Draw Call 的 CPU 开销主要来自“调用次数”，而不是“绘制内容的大小”。

所以如果 500 个立方体每个都发一次 Draw Call，CPU 要支付 500 次固定开销；但如果能把它们合并成 20 个批次，CPU 开销就降到 20 次。

---

## 问题 2：最 naive 的方案——按遍历顺序逐个画

假设你刚刚实现了一个 ECS 渲染 System，它查询所有带 `MeshRenderer` 和 `Transform` 的实体，然后按 ECS 内部存储顺序绘制：

```cpp
void RenderSystem::Update(World* world) {
    auto query = world->Query<MeshRenderer, Transform>();
    for (auto [e, mesh, transform] : query) {
        cmd->SetPipeline(mesh.material->pipeline);
        cmd->SetVertexBuffer(mesh.vertexBuffer);
        cmd->SetIndexBuffer(mesh.indexBuffer);
        cmd->SetConstantBuffer(0, transform.matrix);
        cmd->DrawIndexed(mesh.indexCount);
    }
}
```

**立刻发现的问题**：

- **PSO 切换爆炸**：ECS 的存储顺序通常按 EntityID，与材质无关。相邻实体可能使用完全不同的 Shader，导致每帧数百次状态切换。
- **纹理绑定重复**：如果实体 1 和实体 3 用同一张纹理，但中间插了一个用不同纹理的实体 2，那么纹理 1 会被绑定两次。
- **无 Early-Z 优化**：不透明物体如果随机顺序绘制，后面的物体会做大量无效像素着色（被前面的深度测试丢弃）。
- **透明物体渲染错误**：透明物体必须从远到近绘制，随机顺序会导致混合结果错误。

**结论**：不能按 ECS 存储顺序直接画，必须先组织渲染队列。

---

## 问题 3：排序键——按状态排序后绘制

第一个改进：为每个可见物体生成一个**排序键（Sort Key）**，然后按排序键排序，再顺序绘制。

排序键是一个 64 位整数，高几位放“渲染阶段”，中间放“PSO/材质 ID”，低几位放“深度”。

```cpp
struct DrawItem {
    uint64_t sortKey;
    Entity   entity;
    MeshHandle mesh;
    MaterialHandle material;
    Mat4     worldMatrix;
};

uint64_t ComputeSortKey(RenderPhase phase, uint32_t psoId,
                        uint32_t materialId, float viewDepth) {
    uint64_t key = 0;
    key |= (uint64_t)phase       << 56;  // 高 8 位：渲染阶段
    key |= (uint64_t)psoId       << 40;  // 接下来 16 位：PSO ID
    key |= (uint64_t)materialId  << 24;  // 接下来 16 位：材质 ID

    if (phase == RenderPhase::Transparent) {
        // 透明物体：从远到近
        key |= (0xFFFFFFu - DepthToUint24(viewDepth));
    } else {
        // 不透明物体：从前到后，优化 Early-Z
        key |= DepthToUint24(viewDepth);
    }
    return key;
}

void QueueDraws(const VisibleList& visible, Array<DrawItem>* items) {
    for (Entity e : visible) {
        DrawItem item;
        RenderPhase phase = GetPhase(e);
        item.sortKey = ComputeSortKey(
            phase, GetPSO(e), GetMaterial(e), GetViewDepth(e));
        item.phase = phase;
        item.entity = e;
        item.mesh = GetMesh(e);
        item.material = GetMaterial(e);
        item.worldMatrix = GetMatrix(e);
        items->push_back(item);
    }

    std::sort(items->begin(), items->end(),
              [](const DrawItem& a, const DrawItem& b) {
                  return a.sortKey < b.sortKey;
              });
}
```

**排序后的效果**：

```
排序前：CubeA(红) → SphereB(蓝) → CubeC(红) → SphereD(蓝)
排序后：CubeA(红) → CubeC(红) → SphereB(蓝) → SphereD(蓝)
```

现在红色立方体连续绘制，只需要设置一次红色材质的 PSO 和纹理；蓝色球体也只需要设置一次。

**排序键设计的 trade-off**：

- 如果 PSO ID 放高位、深度放低位：同一 PSO 的物体聚集，状态切换最少，但深度顺序被打乱（对 Early-Z 不利）
- 如果深度放高位、PSO ID 放低位：Early-Z 最优，但相同材质的物体可能被深度隔开

**没有 universally optimal 的排序键**。默认推荐：不透明阶段把 PSO/材质放高位，深度放低位；透明阶段把深度放高位（保证从远到近）。

---

## 问题 4：分箱（Binning）——用哈希桶代替全局排序

排序键解决了状态切换问题，但每帧对所有可见物体做全局排序仍然有 CPU 开销。如果场景里有 10000 个物体，`std::sort` 的比较次数约为 13 万次。

**观察**：不透明物体其实不需要严格的深度顺序。只要 Early-Z 大致工作，偶尔的深度顺序混乱不会带来灾难性后果。真正需要严格排序的只有透明物体。

所以可以把渲染阶段分成两类：

| 阶段 | 策略 | 原因 |
|------|------|------|
| **不透明（Opaque）** | 分箱（Binning） | 相同 PSO+材质的物体进同一个箱子，箱内不排序 |
| **透明（Transparent）** | 排序键（Sorted） | 必须严格从远到近 |

```cpp
struct RenderBin {
    PipelineHandle pipeline;
    MaterialHandle material;
    Array<DrawItem> items;
};

HashMap<uint64_t, RenderBin> bins;

void BinDraws(const Array<DrawItem>& items) {
    for (const auto& item : items) {
        uint64_t binKey = CombineHashes(item.material, item.pipeline);
        bins[binKey].items.push_back(item);
    }
}

void RenderBinnedPhase(ICommandList* cmd) {
    for (auto& [key, bin] : bins) {
        cmd->SetPipeline(bin.pipeline);
        cmd->SetMaterial(bin.material);
        for (const auto& item : bin.items) {
            cmd->SetVertexBuffer(item.mesh.vertexBuffer);
            cmd->SetIndexBuffer(item.mesh.indexBuffer);
            cmd->SetConstantBuffer(0, item.worldMatrix);
            cmd->DrawIndexed(item.mesh.indexCount);
        }
    }
}
```

**分箱的代价**：

- 箱内无序，平均只有 50% 的物体能享受到 Early-Z 优化
- 无法处理透明物体

**分箱的收益**：

- 不需要全局排序，CPU 开销更低
- 相同 PSO/材质的物体保证连续绘制

---

## 问题 5：Instancing——真正的 Draw Call 合并

排序和分箱减少了状态切换，但 **Draw Call 数量没有减少**。每个物体仍然要调用一次 `DrawIndexed`。

如果 200 棵树使用完全相同的网格和材质，它们不应该发 200 次 Draw Call，而应该发 1 次 `DrawIndexedInstanced(instance_count=200)`。

**Instancing 的核心思想**：

- CPU 只设置一次 PSO、绑定一次资源
- GPU 通过 `gl_InstanceID`（或 `SV_InstanceID`）区分不同实例
- 每个实例的 per-instance 数据（如变换矩阵）放在单独的实例缓冲中

```cpp
// CPU 侧：收集实例数据
struct InstanceData {
    Mat4 worldMatrix;
};

HashMap<MeshMaterialKey, Array<InstanceData>> instanceBatches;

void BuildInstanceBatches(const Array<DrawItem>& items) {
    for (const auto& item : items) {
        auto key = MakeKey(item.mesh, item.material);
        instanceBatches[key].push_back({item.worldMatrix});
    }
}

// 上传实例数据到 GPU
void UploadInstanceBuffer(const Array<InstanceData>& data, GLuint buffer) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 data.size() * sizeof(InstanceData),
                 data.data(), GL_DYNAMIC_DRAW);
}

// 绘制
void DrawInstanced(GLuint vao, GLuint instanceBuffer,
                   uint32_t indexCount, uint32_t instanceCount) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);

    // 把 Mat4 拆成 4 个 vec4 顶点属性
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                              sizeof(Mat4),
                              (void*)(i * sizeof(Vec4)));
        glVertexAttribDivisor(3 + i, 1);  // 每 1 个实例更新一次
    }

    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                            nullptr, instanceCount);
}
```

**顶点着色器**：

```glsl
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in mat4 aInstanceMatrix;  // 4 个 vec4 组成

uniform mat4 uViewProj;

void main() {
    gl_Position = uViewProj * aInstanceMatrix * vec4(aPos, 1.0);
}
```

**Instancing 的代价**：

- 需要 Instance Buffer，每帧更新
- 不能处理材质差异（200 棵树中有一棵黄叶，必须单独绘制）
- 实例数极大时，instance buffer 的读取可能成为瓶颈

**Instancing 不是万能药，但在“大量重复物体”场景下是刚需**：草地、树林、碎石、粒子、人群。

---

## 问题 6：透明物体怎么办？

透明物体不能合批，因为合批会打乱绘制顺序；而透明物体的正确渲染依赖**从远到近**的顺序（Painter's Algorithm）。

```cpp
void RenderTransparentPhase(ICommandList* cmd, Array<DrawItem>& items) {
    // 按视图空间深度从远到近排序
    std::sort(items.begin(), items.end(),
              [](const DrawItem& a, const DrawItem& b) {
                  return a.viewDepth > b.viewDepth;
              });

    for (const auto& item : items) {
        cmd->SetPipeline(item.material.pipeline);
        cmd->SetMaterial(item.material);
        cmd->SetVertexBuffer(item.mesh.vertexBuffer);
        cmd->SetIndexBuffer(item.mesh.indexBuffer);
        cmd->SetConstantBuffer(0, item.worldMatrix);
        cmd->DrawIndexed(item.mesh.indexCount);
    }
}
```

**关键规则**：

- 不透明物体：先画，从前到后，利用 Early-Z
- 透明物体：后画，从远到近，关闭深度写入（只读深度测试）
- Alpha Test（Alpha Mask）物体：可以当作不透明从前到后绘制，利用 Early-Z 剔除被 Mask 掉的像素

---

## 问题 7：ECS 组件怎么组织以支持合批？

合批效率很大程度上取决于 ECS 组件设计。

### 反例：把材质数据直接塞进组件

```cpp
struct MeshRenderer {
    MeshHandle mesh;
    MaterialInstance materialInstance;  // 每个实体一份独立材质
};
```

如果 100 棵树共享同一个材质资产，但因为 `materialInstance` 是每实体独立的，合批系统会误判为 100 种不同材质。

### 推荐：分离共享资源句柄和 per-instance 数据

```cpp
// 共享资源：由 asset 系统管理，多个实体引用同一个句柄
struct MeshFilter {
    MeshHandle mesh;
};

struct MaterialRef {
    MaterialHandle material;  // 引用共享材质资产
};

// per-instance 数据：每个实体独立
struct Transform {
    Mat4 worldMatrix;
};
```

合批系统在做 bin key 时，只比较 `MeshFilter.mesh` 和 `MaterialRef.material`。只要这两个句柄相同，就可以进同一个 bin 或 instance batch。

### 更进一步的 ECS 组织

```cpp
// 静态合批：不移动、不销毁的物体可以预合并成一个大 Mesh
struct StaticBatch {
    MeshHandle mergedMesh;
    MaterialHandle material;
    uint32_t subMeshCount;  // 保留原始子网格信息，用于 Debug/拾取
};

// GPU Instancing：大量相同 mesh+material 的动态物体
struct GPUInstancing {
    MeshHandle mesh;
    MaterialHandle material;
    // 不需要存矩阵——Transform 组件提供
};
```

**关键设计点**：

- 共享资源（mesh、material）用**句柄引用**，不要每个实体复制一份
- per-instance 数据（矩阵、颜色变化）用**独立组件**，方便收集进 instance buffer
- 合批是**运行时根据组件数据动态决定**的，不是硬编码的

---

## 问题 8：最小可运行实现

下面是一个完整的、基于排序键 + Instancing 的最小渲染队列实现。

```cpp
enum class RenderPhase : uint8_t {
    Opaque = 0,
    Transparent = 1,
};

struct DrawItem {
    uint64_t sortKey;
    RenderPhase phase;
    Entity entity;
    MeshHandle mesh;
    MaterialHandle material;
    PipelineHandle pipeline;
    Mat4 worldMatrix;
    float viewDepth;
};

uint64_t ComputeSortKey(RenderPhase phase, PipelineHandle pso,
                        MaterialHandle mat, float viewDepth) {
    uint64_t key = 0;
    key |= (uint64_t)phase << 56;
    key |= (uint64_t)pso.index << 40;
    key |= (uint64_t)mat.index << 24;

    if (phase == RenderPhase::Transparent) {
        key |= (0xFFFFFFu - DepthToUint24(viewDepth));
    } else {
        key |= DepthToUint24(viewDepth);
    }
    return key;
}

class RenderQueue {
    Array<DrawItem> opaqueItems;
    Array<DrawItem> transparentItems;

public:
    void AddItem(const DrawItem& item) {
        if (item.phase == RenderPhase::Opaque) {
            opaqueItems.push_back(item);
        } else {
            transparentItems.push_back(item);
        }
    }

    void BuildBatches(ICommandList* cmd) {
        // 不透明：先按 sortKey 排序，然后合并相邻的相同 mesh+material 为 instance batch
        std::sort(opaqueItems.begin(), opaqueItems.end(),
                  [](const DrawItem& a, const DrawItem& b) {
                      return a.sortKey < b.sortKey;
                  });

        RenderBatchesWithInstancing(cmd, opaqueItems);

        // 透明：只按深度排序，不合批
        std::sort(transparentItems.begin(), transparentItems.end(),
                  [](const DrawItem& a, const DrawItem& b) {
                      return a.viewDepth > b.viewDepth;
                  });

        for (const auto& item : transparentItems) {
            DrawSingle(cmd, item);
        }
    }

private:
    void RenderBatchesWithInstancing(ICommandList* cmd, Array<DrawItem>& items) {
        for (size_t i = 0; i < items.size();) {
            size_t j = i;
            // 找到一段连续相同 mesh+material 的 item
            while (j < items.size() &&
                   items[j].mesh == items[i].mesh &&
                   items[j].material == items[i].material) {
                j++;
            }

            uint32_t batchSize = j - i;
            if (batchSize > 1) {
                // 走 Instancing
                Array<Mat4> matrices;
                matrices.reserve(batchSize);
                for (size_t k = i; k < j; k++) {
                    matrices.push_back(items[k].worldMatrix);
                }
                cmd->DrawIndexedInstanced(items[i].mesh, items[i].material,
                                          matrices);
            } else {
                // 单个物体，普通 Draw
                DrawSingle(cmd, items[i]);
            }

            i = j;
        }
    }

    void DrawSingle(ICommandList* cmd, const DrawItem& item) {
        cmd->SetPipeline(item.pipeline);
        cmd->SetMaterial(item.material);
        cmd->SetVertexBuffer(item.mesh.vertexBuffer);
        cmd->SetIndexBuffer(item.mesh.indexBuffer);
        cmd->SetConstantBuffer(0, item.worldMatrix);
        cmd->DrawIndexed(item.mesh.indexCount);
    }
};
```

> **注意**：这个最小实现假设 `ICommandList` 已经扩展了 `DrawIndexedInstanced` 接口。工业级实现会更复杂：需要管理 instance buffer 的内存池、处理 base instance、支持 dynamic batching 作为 fallback。

---

## 问题 9：工业级方向——从排序键到 GPU-Driven

排序键 + Instancing 能处理大多数场景，但当物体数量达到十万、百万级时，CPU 侧的组织本身也会成为瓶颈。

**更进一步的方案是 GPU-Driven Rendering**：

- 把整个场景的几何数据放进一个巨大的 GPU buffer
- 用 Compute Shader 做视锥剔除、遮挡剔除、LOD 选择
- 用 Multi-Draw Indirect（MDI）让 GPU 自己决定画哪些物体
- CPU 只发一次 `glMultiDrawElementsIndirect` 或 `vkCmdDrawIndexedIndirect`

SelfGameEngine 的 [[Notes/SelfGameEngine/渲染管线与画面/遮挡剔除与GPU-Driven渲染|遮挡剔除与GPU-Driven渲染]] 会深入这个话题。本篇的排序键和 Instancing 是 GPU-Driven 的“前置故事片”。

---

## 与 SelfGameEngine 的关系

这篇笔记对应引擎 **阶段 5.4 “渲染一帧的生命周期”** 中的渲染队列组织。

SelfGameEngine 的 [[Notes/SelfGameEngine/渲染管线与画面/渲染队列与DrawCall组织|渲染队列与DrawCall组织]] 已经深入讨论了：
- 排序键的位域设计
- Binned Phase vs Sorted Phase
- Instancing 的 instance buffer 管理
- 透明物体的深度排序
- 与 ECS 组件的映射

本篇图形学笔记回答的是：**在学习阶段，为什么 Draw Call 数量是瓶颈？排序、分箱、Instancing 的最小实现长什么样？以及 ECS 组件如何组织才能支持自动合批？**

---

## 个人项目推荐

| 阶段 | 推荐做法 |
|------|---------|
| 学习阶段 | 先实现逐实体绘制，用 RenderDoc 观察 Draw Call 数量 |
| 性能瓶颈出现 | 引入排序键，按 PSO/材质排序，减少状态切换 |
| 大量重复物体 | 实现 Instancing，草地/树林/粒子必备 |
| 长期工业级 | Binned Phase + Sorted Phase 双轨，Instance Batch 自动构建 |
| 极限场景 | GPU-Driven Rendering + Multi-Draw Indirect |

---

## 本模块还缺什么？

| 已理解 | 待实践 |
|--------|--------|
| Draw Call 的 CPU 开销来源 | Instance Buffer 的内存池管理 |
| 排序键设计 | Base Instance 与动态 batch 切换 |
| 分箱 vs 排序 | 透明物体与 Alpha Mask 的精细处理 |
| Instancing 实现 | GPU-Driven 与 Multi-Draw Indirect |
| ECS 组件组织 | 运行时合批的 Profiler 与调试工具 |

> **下一步**：[[Notes/计算机图形学/引擎渲染架构/渲染状态管理|渲染状态管理]] — 合批后状态切换少了，但状态本身怎么管理？深度排序、透明混合、Early-Z 怎么保证正确？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
