---
order: 37
title: RenderGraph 与多 Pass 资源管理
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - render-graph
  - barriers
  - resource-management
aliases:
  - RenderGraph 与多 Pass 资源管理
---

> **前置依赖**：[[ECS架构下的渲染世界设计]]
> **本模块增量**：掌握声明式 RenderGraph 的设计原理——自动 Barrier 推导、拓扑排序、死 Pass 剔除、瞬态资源别名。你将能够系统化管理多 Pass 渲染管线的资源状态和依赖关系。
>
> 本笔记探讨的核心问题是：**多个渲染 Pass（Shadow、GBuffer、Lighting、PostProcess）之间，资源如何创建、读写、转换状态、最终呈现到屏幕？手动管理 Barrier 的困境如何破解？**

## 问题 5：多个渲染 Pass 如何管理资源和依赖？

### 场景与根因

假设你正在实现一个最小化的延迟渲染管线：

1. **Shadow Pass**：渲染深度图到 `shadow_map` 纹理。
2. **GBuffer Pass**：渲染场景到 `gbuffer_color`、`gbuffer_normal`、`gbuffer_depth`。
3. **Lighting Pass**：读取 GBuffer，计算光照，写入 `lit_scene`。
4. **PostProcess Pass**：读取 `lit_scene`，应用 Bloom，写入 `swapchain`。

每个 Pass 都涉及资源的读写。Shadow Pass **写入** `shadow_map`；Lighting Pass **读取** `shadow_map`。GBuffer Pass **写入** `gbuffer_depth`；Lighting Pass **读取** `gbuffer_depth`。

在 D3D12/Vulkan 中，这些读写关系不是"声明一下就好"的。你必须在命令流中显式插入**资源屏障（Resource Barrier）**，告诉 GPU："shadow_map 刚刚被作为 Depth Target 写入，现在我要把它作为 Shader Resource 读取，请确保缓存一致性和布局转换完成。"

手动管理这些屏障是灾难性的：
- **漏 Barrier**：GPU 读到了还没写完成的纹理，画面出现随机噪点或黑块。
- **多 Barrier**：在每一对读写之间都插 Barrier，GPU 频繁刷新缓存，流水线空闲。
- **生命周期混乱**：`gbuffer_color` 只在当前帧内使用（瞬态资源），但如果不显式回收，它会一直占用显存。

**根因在于：现代显式 API（D3D12/Vulkan）把"资源状态管理"的责任从驱动推给了应用。应用必须精确知道每一帧中每个资源在何时被谁以何种方式访问。**

### 分支研究

#### 分支 A：手动 Barrier——每个 Pass 自己管

**核心思路**：在 Pass 的录制代码中，手写 `ResourceBarrier` 调用。

```cpp
void RenderShadowPass(ICommandContext* ctx) {
    ctx->ResourceBarrier(shadow_map, DepthWrite);
    ctx->BeginRenderPass({.depth = shadow_map});
    // ... 绘制阴影投射者
    ctx->EndRenderPass();
    ctx->ResourceBarrier(shadow_map, ShaderResource);  // 转给 Lighting Pass 读
}

void RenderLightingPass(ICommandContext* ctx) {
    ctx->ResourceBarrier(gbuffer_color, ShaderResource);
    ctx->ResourceBarrier(gbuffer_normal, ShaderResource);
    ctx->ResourceBarrier(gbuffer_depth, ShaderResource);
    ctx->BeginRenderPass({.color = lit_scene});
    // ... 绑定 GBuffer SRV，计算光照
    ctx->EndRenderPass();
}
```

**适用场景**：极简单管线（1~2 个 Pass）、学习目的、快速验证。

**隐藏代价**：
- **极易出错**。如果 Lighting Pass 的开发者忘了给 `gbuffer_normal` 插 Barrier，画面会出现微妙的法线错误，可能在特定 GPU 上才显现，调试极其困难。
- **无法优化**。开发者通常会在"读取前"立即插入 Barrier，但最优的 Barrier 位置可能在更前面——例如，如果 Shadow Pass 和 GBuffer Pass 之间没有资源依赖，它们可以并行执行，但手动 Barrier 往往会在两者之间制造不必要的同步点。
- **扩展困难**。新增一个 Pass（如 SSAO）时，需要检查所有相邻 Pass 的资源访问关系，修改多处 Barrier。

**失效条件**：Pass 数量 > 3、或多人协作、或需要频繁调整管线。

#### 分支 B：Render Graph（Frame Graph）——声明式依赖管理

**核心思路**：不再在每个 Pass 里手写 Barrier，而是**先声明**每个 Pass 的输入（读哪些资源）和输出（写哪些资源），然后由一个中央编译器自动推导 Barrier、资源分配和 Pass 执行顺序。

```cpp
// 声明式渲染图（概念）
RenderGraph graph;

auto shadow_map = graph.CreateTexture("ShadowMap", {.size = 2048, .format = Depth32});
auto gbuffer    = graph.CreateTexture("GBuffer",    {.size = window_size, .format = RGBA16F});
auto lit_scene  = graph.CreateTexture("LitScene",   {.size = window_size, .format = RGBA16F});
auto swapchain  = graph.ImportTexture("BackBuffer", swapchain_handle);

graph.AddPass("Shadow", [&](PassBuilder& b) {
    b.WriteDepth(shadow_map);
    b.Execute([=](ICommandContext* ctx) {
        for (auto& item : shadow_phase.items) DrawItem(ctx, item);
    });
});

graph.AddPass("GBuffer", [&](PassBuilder& b) {
    b.WriteColor(0, gbuffer);
    b.Execute([=](ICommandContext* ctx) {
        for (auto& item : opaque_phase.items) DrawItem(ctx, item);
    });
});

graph.AddPass("Lighting", [&](PassBuilder& b) {
    b.Read(shadow_map);       // 声明读 ShadowMap
    b.Read(gbuffer);          // 声明读 GBuffer
    b.WriteColor(0, lit_scene);
    b.Execute([=](ICommandContext* ctx) {
        ctx->BindTexture(0, shadow_map);
        ctx->BindTexture(1, gbuffer);
        ctx->DrawFullscreenQuad();
    });
});

graph.AddPass("PostProcess", [&](PassBuilder& b) {
    b.Read(lit_scene);
    b.WriteColor(0, swapchain);
    b.Execute([=](ICommandContext* ctx) {
        ctx->BindTexture(0, lit_scene);
        ctx->DrawFullscreenQuad();
    });
});

graph.Compile();  // 自动推导 Barrier、分配瞬态资源、剔除无用 Pass
graph.Execute();  // 按拓扑序执行 Pass
```

**适用场景**：任何需要管理多个 Pass、复杂资源依赖、希望自动化 Barrier 的引擎。

**隐藏代价**：
- **实现复杂度高**。Render Graph 的编译器需要实现：拓扑排序、死 Pass 剔除（Culling）、资源生命周期扫描、瞬态资源别名（Aliasing）、Barrier 自动生成、Async Compute 调度……这是一个小型编译器。
- **调试抽象泄漏**。当画面出现 Barrier 相关错误时，开发者看到的只是 `graph.AddPass` 的声明，实际的 Barrier 是编译器生成的。需要强大的可视化工具（如 UE 的 RDG Insights）才能理解编译器决策。
- **不适合极度简单的管线**。如果只有 2 个 Pass，Render Graph 的 overhead（编译时间、内存分配）可能超过收益。

**失效条件**：固定管线且 Pass 数量极少（<3）、或团队没有精力实现/维护 Render Graph 编译器。

#### 分支 C：半自动 Barrier——状态追踪上下文

**核心思路**：介于手动和完全自动之间。RHI 层内部维护一个资源状态追踪器，当 Pass 绑定资源时，自动插入必要的 Barrier。

```cpp
class TrackedCommandContext : public ICommandContext {
    HashMap<TextureHandle, ResourceState> tracked_states;
    
public:
    void BindTexture(uint32_t slot, TextureHandle tex) override {
        auto& state = tracked_states[tex];
        if (state != ShaderResource) {
            InsertBarrier(tex, state, ShaderResource);
            state = ShaderResource;
        }
        // ... 实际绑定
    }
    
    void BeginRenderPass(const RenderPassDesc& desc) override {
        for (auto& color : desc.color_targets) {
            auto& state = tracked_states[color.texture];
            if (state != RenderTarget) {
                InsertBarrier(color.texture, state, RenderTarget);
                state = RenderTarget;
            }
        }
        // ...
    }
};
```

**适用场景**：中等复杂度管线、不想实现完整 Render Graph、但希望减少手动 Barrier 错误。

**隐藏代价**：
- **Barrier 位置非最优**。状态追踪器在"绑定时刻"插入 Barrier，但最优 Barrier 可能在更早的位置（例如，在上一 Pass 结束后立即转换，而不是等到本 Pass 开始才转换）。
- **无法做瞬态资源别名**。因为没有全局的帧视图，不知道资源的生命周期是否重叠，无法安全地复用显存。
- **跨 Pass 依赖不可见**。如果 Pass A 写入纹理 T，Pass B 读取 T，但中间还有一个不相关的 Pass C，状态追踪器可能在 C 的某个操作中意外转换 T 的状态，导致 B 读错。

**失效条件**：需要瞬态资源内存优化、或 Async Compute 等跨队列同步。

### 引擎对照

> **参考：chaos / UE / Bevy 对「Render Graph」是怎么做的？**
>
> - **chaos** 从源码分析中确认使用了 `RenderGraph` 类，具有 `setup()`、`compile()`（含 `buildDependency` 和 `cullPasses`）、`render()` 的完整生命周期。每个 `RenderView` 持有多个 RenderGraph 实例（`shadow_render_graph`、`deferred_unified_render_graph`、`forward_pipeline_render_graph`）。chaos 的 RenderGraph 支持 **Persistent Texture**（跨帧保留，如 TAA 历史帧）和 **Pooled Texture**（帧内临时 RT，由 `RenderGraphResourceManager` 池化分配）。这表明 chaos 的 RenderGraph 已经实现了瞬态资源管理和 Pass 裁剪。
> - **UE** 的 **RDG（Render Dependency Graph）** 是业界最成熟的实现之一。`FRDGBuilder` 提供 `AddPass`、`CreateTexture`、`CreateBuffer` 接口，Pass 通过 `_RDG` 宏标记参数读写状态。RDG 在 `Execute()` 时：
>   1. 拓扑排序 Pass。
>   2. 基于反向可达性从最终输出裁剪无用 Pass（Refcount-based culling）。
>   3. 扫描资源生命周期，分配瞬态内存（`FPooledRenderTarget` 池或临时分配）。
>   4. 自动插入 Barrier（Transition、UAV、Aliasing）。
>   5. 合并相邻的 RenderPass（如 GBuffer 和 Lighting 合并为 subpass，在 TBDR 移动 GPU 上减少内存带宽）。
>   UE 的 RDG 还支持 AsyncCompute Pass（`ERDGPassFlags::AsyncCompute`），自动处理 Graphics 和 Compute 队列之间的 Fence 同步。
> - **Bevy** 在 0.19 中**已经移除了传统的节点图 RenderGraph**，改为基于 **ECS Schedule** 的渲染管线驱动。`RenderGraph` 现在是一个 `ScheduleLabel`，运行 `Begin → Render → Submit → Finish` 的 SystemSet 链。真正的渲染阶段顺序由 `RenderSystems`（ExtractCommands → PrepareAssets → ... → Render → Cleanup）控制。这意味着 Bevy 的"Pass 依赖"不是通过显式图结构表达的，而是通过 **SystemSet 的 `before`/`after` 链** 隐式表达的。Barrier 管理则交给底层 wgpu——wgpu 的 `CommandEncoder` 会自动推导资源布局转换（虽然不如 RDG 精细）。Bevy 的动机是：渲染阶段就是普通 ECS System，充分利用现有的调度基础设施。

### 决策分析与推荐

**默认推荐：分支 B（Render Graph），但阶段 5 初期实现"简化版 Render Graph"——只包含 Pass 声明、拓扑排序、自动 Barrier，暂不实现瞬态资源别名和 Async Compute 调度。**

理由如下：

1. **UE 的 RDG 证明了 Render Graph 是管理复杂管线的"最小正确抽象"**。手动 Barrier 在 3 个 Pass 时还能应付，到 10 个 Pass 时错误率指数级上升。RDG 不是"过度工程"，而是把"资源状态管理"这个本质上属于编译时的问题，从运行时手工操作提升到了编译时自动推导。

2. **chaos 的 RenderGraph 是同一设计思想的验证**。chaos 同时维护多个 RenderGraph（阴影、延迟、前向），每个 View 独立编译执行。这说明 Render Graph 的"声明式"特性天然支持多视图——每个视图有自己的资源集和 Pass 序列，互不干扰。

3. **Bevy 的 ECS Schedule 驱动有其优雅之处，但不适合需要精细资源控制的 C++ 引擎**。Bevy 把 Barrier 交给 wgpu，这简化了实现，但也意味着无法做跨 Pass 的瞬态资源别名（因为 wgpu 看不到整帧的视图）。对于个人 C++ 引擎，我们希望控制 D3D12 的 `PlacedResource` 来复用显存，这需要引擎自己管理资源生命周期——Render Graph 是必要条件。

4. **简化版 Render Graph 的复杂度被高估了**。一个最小可用的 Render Graph 编译器只需要：
   - 一个 Pass 数组 + 一个资源数组。
   - 拓扑排序（Kahn 算法，几十行代码）。
   - 从最终输出反向遍历，标记可达 Pass（Culling）。
   - 为每个资源记录"首次写入 Pass"和"最后读取 Pass"，在边界插入 Barrier。
   这些功能合计不超过 500 行代码，但能消除 90% 的手动 Barrier 错误。

**"简化版 Render Graph"的具体实施策略**：

```cpp
// 简化版 Render Graph 核心结构
class RenderGraph {
    struct Pass {
        StringID name;
        Array<ResourceHandle> writes;
        Array<ResourceHandle> reads;
        Function<void(ICommandContext*)> execute;
        bool culled = false;
    };
    
    struct Resource {
        StringID name;
        TextureDesc desc;
        bool imported;          // true = 外部资源（如 SwapChain BackBuffer）
        bool transient;         // true = 帧内临时资源，可别名
        Pass* first_writer = nullptr;
        Pass* last_reader = nullptr;
    };
    
    Array<Pass> passes;
    Array<Resource> resources;
    
public:
    TextureHandle CreateTexture(const char* name, const TextureDesc& desc);
    TextureHandle ImportTexture(const char* name, TextureHandle external);
    
    void AddPass(const char* name, Function<void(PassBuilder&)> setup);
    void Compile();  // 拓扑排序 + Culling + 生命周期扫描
    void Execute(ICommandContext* ctx);
};

// 编译阶段（简化示意）
void RenderGraph::Compile() {
    // 1. 拓扑排序
    auto sorted = TopologicalSort(passes);
    
    // 2. 从最终输出反向标记可达 Pass
    for (auto& res : resources) {
        if (res.imported && IsBackBuffer(res)) {
            // 反向传播：标记所有能到达 BackBuffer 的 Pass 为可达
            MarkReachable(res.last_reader);
        }
    }
    for (auto& pass : passes) {
        if (!pass.reachable) pass.culled = true;
    }
    
    // 3. 扫描资源生命周期（只考虑未 cull 的 Pass）
    for (auto& pass : sorted) {
        if (pass.culled) continue;
        for (auto& r : pass.writes) {
            if (!resources[r].first_writer) resources[r].first_writer = &pass;
        }
        for (auto& r : pass.reads) {
            resources[r].last_reader = &pass;
        }
    }
    
    // 4. Barrier 推导（Execute 时动态插入）
}

// 执行阶段
void RenderGraph::Execute(ICommandContext* ctx) {
    for (auto& pass : sorted_passes) {
        if (pass.culled) continue;
        
        // 为每个读资源插入 ShaderResource Barrier
        for (auto& r : pass.reads) {
            auto& res = resources[r];
            if (res.last_transition != ShaderResource)
                ctx->ResourceBarrier(res.handle, ShaderResource);
        }
        // 为每个写资源插入 RenderTarget/UAV Barrier
        for (auto& r : pass.writes) {
            auto& res = resources[r];
            if (res.last_transition != RenderTarget)
                ctx->ResourceBarrier(res.handle, RenderTarget);
        }
        
        pass.execute(ctx);
    }
}
```

**关键设计点**：
- **瞬态资源标记**。`CreateTexture` 创建的资源默认是瞬态的（`transient = true`），编译器知道它们不会跨帧存活。阶段 5 初期不做别名分配（每个瞬态资源独立分配 GPU 内存），但架构上预留 `transient` 字段，未来可以引入内存池复用。
- **Imported Resource**。SwapChain BackBuffer、历史帧纹理（TAA）、用户创建的持久纹理通过 `ImportTexture` 引入，不会被 Cull，也不会被别名。
- **Pass Culling 自动处理**。如果某个 Debug Pass（如线框覆盖）没有被连接到最终输出，它在 `Compile()` 后自动被标记为 `culled = true`，不分配资源、不录制命令。
- **与 ECS 的映射**：Render Graph 本身是一个 Render World 的 Resource。`Queue` 阶段填充各个 Phase 的 Draw Item，`Render` 阶段的一个 System 持有 `RenderGraph` Resource，调用 `Compile()` 和 `Execute()`。

**遗留问题**：Render Graph 管理了一帧内的 Pass 执行顺序和资源转换，但命令最终需要被提交到 GPU。谁来执行 `Execute()`？在主线程上单线程录制所有 Pass 的命令，在复杂场景下 CPU 会成为瓶颈。如何把命令录制扩展到多线程？以及，命令录制完成后，如何与 SwapChain 交互、如何把画面呈现到屏幕上？


## 问题 6：命令如何到达 GPU 并呈现到屏幕？

### 场景与根因

你已经通过 Render Graph 按正确顺序执行了所有 Pass，每个 Pass 的 `execute` Lambda 往 `ICommandContext` 里写了 Draw Call。现在 `RenderGraph::Execute()` 结束了，但屏幕上什么都没有。

**根因在于：现代 GPU 是异步处理器。你"写"的命令只是存在 CPU 内存的一块缓冲里，GPU 根本不知道它们的存在。必须显式"提交"（Submit）命令到 GPU 队列，GPU 才开始执行。而且，提交之后 CPU 不能立即销毁命令缓冲或相关资源——必须等 GPU 完成。**

更复杂的是，SwapChain 的 BackBuffer 是平台管理的特殊资源。你不能直接 "画到屏幕"，只能画到 SwapChain 当前提供的 BackBuffer，然后调用 `Present()` 请求平台把它翻转（Flip）到显示器上。`Present` 通常还会引入 VSync 等待——如果 GPU 上一帧还没画完，CPU 在 `Present` 处会被阻塞。

### 分支研究

#### 分支 A：单线程即时提交

**核心思路**：所有 Pass 的命令在同一个 `ICommandContext` 上顺序录制，Render Graph 执行完毕后，立即 `Submit` 这个上下文，然后 `Present`。

```cpp
void RenderFrame() {
    auto* ctx = device->CreateCommandContext();
    render_graph.Execute(ctx);    // 单线程录制所有 Pass
    device->Submit(ctx);          // 提交到 GPU 队列
    device->Present();            // 呈现
}
```

**适用场景**：简单管线、Pass 数量少、Draw Call < 2000、快速验证。

**隐藏代价**：
- **CPU 录制瓶颈**。如果 Render Graph 有 10 个 Pass，每个 Pass 录制约 1ms，总录制时间 10ms——你的帧预算已经消耗了大半。
- **无重叠**。CPU 在录制时 GPU 空闲；GPU 在执行时 CPU 空闲（如果 CPU 没有其他工作）。无法利用 CPU-GPU 并行性。
- **同步简单但粗暴**。`Present` 内部通常会自动等待上一帧完成（通过 SwapChain 的同步机制），但帧与帧之间的 CPU-GPU 重叠度很低。

**失效条件**：Draw Call > 2000、或 Pass 数量 > 5、或需要 Async Compute。

#### 分支 B：多线程并行录制 + 主线程合并提交

**核心思路**：Render Graph 的 Pass 之间如果有并行性（无资源依赖），可以由多个线程各自录制独立的命令列表（D3D12 CommandList / Vulkan Secondary CommandBuffer），最后由主线程合并提交。

```cpp
// 假设 Shadow Pass 和 GBuffer Pass 无互相依赖，可并行
void RecordShadowPass(ICommandContext* ctx) { /* ... */ }
void RecordGBufferPass(ICommandContext* ctx) { /* ... */ }

ThreadPool tp;
auto* shadow_ctx = device->CreateCommandContext();
auto* gbuffer_ctx = device->CreateCommandContext();

tp.Submit([&]{ RecordShadowPass(shadow_ctx); });
tp.Submit([&]{ RecordGBufferPass(gbuffer_ctx); });
tp.WaitAll();

// 主线程按依赖顺序提交
device->Submit(shadow_ctx);
device->Submit(gbuffer_ctx);  // 如果 Lighting 依赖两者，这里只是录制，提交顺序在最终队列中控制
device->Present();
```

**适用场景**：Pass 数量多、存在天然并行性（如 Shadow + GBuffer）、有线程池基础设施。

**隐藏代价**：
- **命令列表合并复杂度**。D3D12 允许多个 CommandList 批量提交（`ExecuteCommandLists` 接收数组），但提交顺序必须满足 Pass 依赖。Vulkan 的 Secondary CommandBuffer 需要 `vkCmdExecuteCommands` 进 Primary CB。
- **资源状态追踪跨列表困难**。如果 Shadow Pass 把 `shadow_map` 转为 ShaderResource，这个状态转换发生在 `shadow_ctx` 中。`gbuffer_ctx` 如果也触碰 `shadow_map`（虽然本例中没有），需要知道状态已经被转换——跨列表的状态追踪是 Render Graph 编译器的额外责任。
- **内存开销**。每个并行录制的线程需要独立的 Command Allocator。每帧结束后这些 Allocator 必须被回收或重置。

**失效条件**：Pass 之间高度串行（如后处理链必须 A→B→C→D）、或线程池调度开销超过并行收益。

#### 分支 C：RHI 线程 + 命令缓冲队列

**核心思路**：渲染线程只生成"引擎级命令"（如 `CmdDrawIndexed`、`CmdSetPipeline`），把这些命令序列化到一个线程安全的缓冲队列中。专门的 RHI 线程从队列消费命令，翻译成底层 API 调用。渲染线程和 RHI 线程通过队列解耦。

```cpp
// 渲染线程（Render Thread / Render World）
void RenderThreadLoop() {
    while (running) {
        auto frame_data = WaitForExtractedData();  // 等待 Extract 完成
        RenderGraph graph;
        BuildRenderGraph(frame_data, &graph);
        graph.Compile();
        graph.Execute(rhi_command_buffer);  // 写入引擎级命令，不是直接调 D3D12
        rhi_queue->Enqueue(rhi_command_buffer);
    }
}

// RHI 线程
void RHIThreadLoop() {
    while (running) {
        auto* buffer = rhi_queue->Dequeue();
        TranslateAndSubmit(buffer);  // 把引擎命令翻译成 ID3D12GraphicsCommandList 调用
        swapchain->Present(vsync);
        
        // 帧同步：等待 GPU 完成，然后回收资源
        WaitForGPUFence(current_frame_fence);
        ProcessDelayedDeletes();
    }
}
```

**适用场景**：工业级引擎、需要最大化 CPU-GPU 并行、需要精细控制提交时序。

**隐藏代价**：
- **额外一层抽象开销**。引擎级命令到 D3D12 API 的翻译有 CPU 开销，虽然单次很小，但数万条命令累积可能达到 0.5~1ms。
- **调试链路变长**。渲染错误可能出在：Render Thread 生成的命令、RHI Thread 的翻译逻辑、或底层 API 的使用。定位问题需要跨线程追踪。
- **延迟删除的复杂度**。RHI 线程是唯一知道"GPU 完成了哪一帧"的线程，因此延迟删除队列必须由 RHI 线程消费。这增加了线程间协调的复杂度。

**失效条件**：默认实现初期、团队规模小、对调试效率要求高于极致性能。

### 引擎对照

> **参考：chaos / UE / Bevy 对「命令提交」是怎么做的？**
>
> - **chaos** 使用了**多线程命令列表池**。`DynamicRHI::getCommandListFromPool()` 允许各 RenderTask（如 `GBufferRenderTask`、`shadowRelatedRenderTask`）从池中获取独立的 `RHICommandList`，实现并行录制，最终由 `RHICommandListExecutor` 统一提交。`tickRender()` 内部会 `waitForRender()` 等待上一帧 GPU 完成，避免 CPU 过度超前。chaos 的 Present 任务被投递到任务队列并等待完成，确保帧边界同步。
> - **UE** 使用了**经典的三线程命令模型**：Render Thread 生成 `FRHICommandList`（引擎级命令），RHI Thread 从 `FRHICommandList` 解包并调用 `IRHICommandContext` 的平台方法。`FRHICommandList` 使用内存栈分配器（`FMemStack`）存储命令，避免了每帧的堆分配。UE 的 `FFrameEndSync::Sync(EndFrame)` 是 Game Thread 等待 Render Thread 的关键同步点；`FRHICommandListImmediate::ImmediateFlush()` 用于需要 GPU 结果的场景。UE 的 `EndFrameRenderThread` 内部调用 `SwapChain->Present()`。
> - **Bevy** 的提交模型最为简洁：**RenderContextState** 在每帧懒创建 `CommandEncoder`，每个渲染 System 结束后通过 `SystemBuffer::queue` 自动把编码器 finish 成 `CommandBuffer`，移入 `PendingCommandBuffers`。`RenderGraphSystems::Submit` 阶段统一提交所有 Pending Buffer 到 `RenderQueue`，然后 `render_system` 调用 `window.present()`。由于 wgpu 内部处理了大部分同步，Bevy 上层不需要显式的 Fence 管理。但这也意味着 Bevy 无法做精细的帧级 CPU-GPU 同步（如动态分辨率调整时的精确等待）。

### 决策分析与推荐

**默认推荐：分支 C 的简化版——单渲染线程生成引擎级命令，单 RHI 线程翻译提交。阶段 5 初期甚至可以合并为单线程（渲染线程直接调 D3D12 API），但接口设计必须预留 RHI 线程的抽象。**

理由如下：

1. **UE 的三线程模型是工业级渲染的"黄金标准"**。它不是为大型项目专有的奢侈品，而是解决以下问题的最小正确架构：
   - 渲染录制与底层 API 调用解耦 → 渲染代码不需要关心 D3D12 的线程安全规则。
   - 命令缓冲的内存管理（栈分配、延迟释放）→ RHI 线程统一处理。
   - 帧边界同步（Fence、Present、VSync）→ RHI 线程是唯一的同步点。
   在 ECS 架构中，Render World 的 System 扮演 Render Thread 的角色，`RHISubmitSystem` 扮演 RHI Thread 的角色。

2. **chaos 的命令列表池验证了多线程录制的可行性**。虽然阶段 5 初期不需要并行录制，但 `ICommandContext` 的设计应该支持"多上下文各自录制后批量提交"——例如 `RenderDevice::Submit(Array<ICommandContext*>)`。这为未来扩展预留了空间。

3. **Bevy 的自动 flush 机制在 ECS 下很优雅，但不适合需要精细控制的 C++ 引擎**。wgpu 的自动提交隐藏了太多细节——你无法在特定 Pass 后插入 CPU-GPU 同步点（如读取 GPU 生成的遮挡查询结果），也无法控制 SwapChain 的呈现时机。个人引擎应该选择"显式控制"而非"隐式自动"。

4. **单线程直接 API 在阶段 5 初期是可接受的**。"单渲染线程直接调用 D3D12"等价于"渲染线程和 RHI 线程合并"。只要 `ICommandContext` 的接口设计是"命令式"的（`DrawIndexed` 追加到内部缓冲，而非立即进入驱动），未来拆分出 RHI 线程时，上层代码不需要修改。

**具体实施策略**：

```cpp
// 阶段 5 初期：单线程简化版
void RenderWorldSystem(World* render_world, Res<RenderDevice> device) {
    auto* ctx = device->CreateCommandContext();
    
    // 运行 Render Graph
    auto& graph = render_world->Resource<RenderGraph>();
    graph.Compile();
    graph.Execute(ctx);
    
    // 直接提交并呈现
    device->Submit(ctx);
    device->Present(vsync_enabled);
    
    // 帧同步：等待 GPU 完成上一帧（简化版）
    device->WaitForFrameFence(frame_index);
    device->ProcessDelayedDeletes();
    frame_index++;
}

// 阶段 5 后期目标：RHI 线程分离版（接口不变，实现改变）
void RenderWorldSystem(World* render_world, Res<RenderCommandQueue> queue) {
    auto* cmd_buffer = queue->AllocateBuffer();
    
    auto& graph = render_world->Resource<RenderGraph>();
    graph.Compile();
    graph.Execute(cmd_buffer);  // 写入引擎级命令
    
    queue->Enqueue(cmd_buffer);  // 交给 RHI 线程
}

// RHI 线程（独立线程）
void RHIThread(Res<RenderCommandQueue> queue, Res<RenderDevice> device) {
    while (running) {
        auto* buffer = queue->Dequeue();  // 阻塞等待
        auto* native_ctx = device->CreateNativeContext();
        TranslateCommands(buffer, native_ctx);
        device->Submit(native_ctx);
        device->Present(vsync_enabled);
        device->WaitForFrameFence(frame_index++);
        device->ProcessDelayedDeletes();
    }
}
```

**关键设计点**：
- **帧索引（Frame Index）与 Fence**。每帧提交时向 GPU 队列插入一个 Fence 信号，CPU 端维护一个 `completed_frame` 计数。延迟删除的资源标记为"在第 N 帧使用"，只有当 `completed_frame >= N` 时才真正释放。这是 UE 的 `FRHIResource` 延迟删除机制的核心。
- **SwapChain 三重缓冲**。默认创建 2~3 个 BackBuffer。`Present()` 后 CPU 继续录制下一帧，GPU 在后台绘制当前帧。`WaitForFrameFence` 确保 CPU 不会超前 GPU 超过 1~2 帧。
- **VSync 是可选阻塞**。`Present(1)` 开启 VSync 时，如果上一帧尚未扫描到屏幕，CPU 会在 Present 处阻塞。对于性能分析，建议提供 `Present(0)` 路径以观察无上限帧率。
- **AI 可观测性**：`RenderCommandQueue` 的长度、`PendingCommandBuffers` 的数量、当前 `frame_index` 与 `completed_frame` 的差值，都应该作为 ECS Resource 暴露给 Inspector 和 AI Agent。

---


> **下一步**：[[后处理栈架构]]，因为 RenderGraph 已经能编排多 Pass。后处理（ToneMapping、Bloom、TAA）正是一系列典型的全屏 Pass，天然适合用 RenderGraph 管理。
