---
title: RHI 抽象层与命令模型
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - rhi
  - architecture
aliases:
  - RHI 抽象层与命令模型
---

> **前置依赖**：[[最简图形后端]]、[[数学基础]]、[[组件系统架构]]
> **本模块增量**：读完这篇笔记后，你将理解"为什么引擎需要一层自己的 GPU 抽象"、"这层抽象的厚度边界在哪里"、"命令录制模型如何选择"、"命令内存该如何布局与管理"、"资源句柄如何跨后端一致且安全"、"错误处理与调试标注如何统一"，以及"第一个后端该选谁"。你会获得一个可落地的 RHI 设计骨架。
>
> 本笔记探讨的核心问题是：**如何让上层渲染代码写一次，就能在不同 GPU API 上运行，同时不牺牲命令录制的灵活性？** 我们将从"直接调 API"的 naive 方案出发，逐层剥开问题的表象，在多条可行路径中做出取舍。

## 问题 1：为什么不能直接用底层图形 API 的裸调用？

在 [[最简图形后端]] 中，我们为了尽快让 ImGui 显示在窗口上，直接调用了 OpenGL 的裸接口：`glClear()`、`glViewport()`、初始化 GL 上下文。这在阶段 1 是完全合理的——目标只有一个"清屏并画出 ImGui"。但到了阶段 5，我们要画带纹理的立方体、切换材质、做后处理 Bloom，甚至未来可能部署到不同平台。

**场景绑定**：假设你的引擎现在用 OpenGL 裸调用写了一套延迟渲染管线。半年后你想让它在 macOS 上跑——但 macOS 从 2018 年起就弃用了 OpenGL，主推 Metal。你面临两个选择：要么重写整套渲染代码，要么在每一行 OpenGL 调用旁边写一个 Metal 等价版本。这不仅是工作量问题，更是维护噩梦：任何 bug 修复、任何优化，都要在两个（甚至更多）代码路径上同步执行。

**根因分析**：底层图形 API 之间的差异远不止"函数名不同"。它们代表了三种根本不同的 GPU 编程模型：

1. **全局状态机模型**（OpenGL / OpenGL ES）：存在隐式的全局上下文状态。绑定一个纹理后，后续所有绘制都会用到它，直到被另一个绑定覆盖。没有显式的"命令列表"概念，每次调用几乎立即进入驱动。
2. **显式命令列表模型**（D3D12 / Vulkan）：没有全局状态。你必须显式创建管线状态对象（PSO）、描述符集、命令缓冲区，把每条绘制指令录制到一个命令列表里，再批量提交给 GPU。资源状态转换（Barrier）也由应用层负责。
3. **编码器模型**（Metal / WebGPU）：介于两者之间。使用 CommandEncoder 录制命令到 CommandBuffer，然后提交到 Queue。比 OpenGL 显式，但比 Vulkan 少一些手动同步负担。

这三种模型的差异不是"语法糖"层面的，而是"编程范式"层面的。如果上层渲染代码直接绑定其中一种，移植到另一种就意味着重写核心逻辑。

**naive 方案：用 `#ifdef` 做平台分支**

```cpp
void DrawMesh(Mesh* mesh) {
#ifdef PLATFORM_OPENGL
    glUseProgram(mesh->shader->gl_program);
    glBindVertexArray(mesh->gl_vao);
    glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, 0);
#elif defined(PLATFORM_D3D12)
    cmd_list->SetPipelineState(mesh->d3d12_pso);
    cmd_list->IASetVertexBuffers(...);
    cmd_list->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
#elif defined(PLATFORM_METAL)
    // ...
#endif
}
```

这个方案的问题不仅是"代码丑陋"。更深层的陷阱在于：
- **状态管理语义冲突**：OpenGL 的 `glUseProgram` 会改变全局状态，影响后续所有绘制；D3D12 的 `SetPipelineState` 只影响当前命令列表。如果上层代码按 OpenGL 的"状态残留"思维写逻辑，在 D3D12 下会莫名其妙地状态泄漏或失效。
- **多线程能力差异**：Vulkan 的命令缓冲可以跨线程并行录制；OpenGL 的上下文只能绑定到一个线程。`#ifdef` 分支会让"哪些操作可以并行"变成平台相关的隐性知识，极容易出错。
- **资源生命周期差异**：OpenGL 的纹理删除 (`glDeleteTextures`) 由驱动异步处理；D3D12 的资源释放必须等待 GPU 完成引用。直接裸调用时，很难写出统一的"安全释放"逻辑。

**结论**：必须有一层统一抽象，把"做什么"（Draw a mesh with this material to that render target）和"怎么做"（OpenGL state change vs D3D12 command list recording）彻底分开。这就是 RHI（Render Hardware Interface）存在的根本原因。

---

## 问题 2：RHI 抽象的"薄"与"厚"边界在哪里？

既然需要一层抽象，下一个问题立刻浮现：**这层抽象应该多厚？**

### 分支 A：极薄抽象——直接 1:1 映射 API 调用

**核心思路**：RHI 的每个函数几乎直接对应底层 API 的一个函数，只做参数类型转换。

```cpp
// 薄抽象示例：直接映射
class ICommandList {
public:
    virtual void SetViewport(float x, float y, float w, float h) = 0;
    virtual void DrawIndexed(uint32_t index_count, uint32_t start_index, int32_t base_vertex) = 0;
};
```

**适用场景**：你的引擎只需要支持两种非常相似的 API（如 D3D12 和 Vulkan），且团队对两者都很熟悉。

**隐藏代价**：
- 上层代码仍然需要理解"命令列表"、"Barrier"、"Descriptor Set"等概念，只是换了一套命名。
- 不同 API 的行为差异（如 Vulkan 的 `vkQueueSubmit` 与 D3D12 的 `ExecuteCommandLists` 在同步语义上的细微差别）仍然会泄漏到上层。
- 无法为多线程录制提供统一策略——薄抽象只是把线程安全问题推迟给了上层。

**失效条件**：当需要支持 OpenGL（没有显式命令列表）或 WebGPU（安全验证层很重）时，1:1 映射会崩解，因为不存在对应的底层概念。

### 分支 B：中等厚度——命令模式 + 统一资源句柄

**核心思路**：RHI 定义一套引擎自己的"命令语言"，上层通过录制引擎级命令来间接驱动 GPU。资源全部用句柄（Handle）引用，生命周期由 RHI 内部管理。

```cpp
// 中等厚度：引擎自己的命令枚举
enum class RenderCmdType {
    SetViewport,
    SetScissor,
    BindPipeline,
    BindVertexBuffer,
    BindIndexBuffer,
    DrawIndexed,
    ResourceBarrier,
};

struct RenderCmdHeader {
    RenderCmdType type;
    uint32_t payload_size;
};

// 上层只写命令到缓冲区，不直接碰 API
class RenderCommandBuffer {
public:
    void SetViewport(float x, float y, float w, float h);
    void DrawIndexed(PipelineHandle pso, BufferHandle vb, BufferHandle ib, uint32_t count);
private:
    // 线性内存缓冲区，存储 (Header + Payload) 序列
    LinearAllocator cmd_mem;
};
```

**适用场景**：需要支持差异较大的多个后端（OpenGL + D3D12 + Vulkan），且希望上层完全不感知底层 API 的差异。

**隐藏代价**：
- 引入了一层"引擎命令 -> 底层 API 命令"的翻译开销。虽然单次翻译很小（通常只是解包参数），但在极端高频场景（每帧数万次 Draw Call）下，指令分发的开销不可忽视。
- 需要引擎自己管理命令内存的分配/回收，增加了实现复杂度。
- 如果底层 API 支持某个优化特性（如 D3D12 的 Bundle），而引擎命令层没有对应的表达，这个特性就无法被利用，除非破坏抽象的纯洁性。

**失效条件**：当底层 API 的新特性（如 Mesh Shader、Ray Tracing）层出不穷时，中等厚度抽象需要不断扩展命令枚举，否则会变成性能瓶颈。

### 分支 C：借用第三方成熟抽象——wgpu / bgfx / NVRHI

**核心思路**：不自己写 RHI，而是基于已有的跨平台图形库。这些库本身就是"别人写好的 RHI"。

| 库                 | 后端覆盖                             | 特点                                      |
| ----------------- | -------------------------------- | --------------------------------------- |
| **wgpu** (Rust/C) | Vulkan, Metal, D3D12, GL, WebGPU | 基于 WebGPU 标准，安全验证严格，API 现代              |
| **bgfx** (C++)    | Vulkan, Metal, D3D12, D3D11, GL  | 游戏引擎领域广泛使用，成熟稳定                         |
| **NVRHI** (C++)   | Vulkan, D3D12, D3D11             | NVIDIA 出品，自动管理 Barrier 和 Descriptor，可绕过 |
| **Dawn** (C++)    | 同 wgpu                           | Google 的 WebGPU 实现，C++ 接口               |

**适用场景**：个人开发者或小团队，想快速获得多后端能力，不重复造轮子。

**隐藏代价**：
- **API 形状锁定**：你的引擎设计被第三方库的 API 形状约束。比如 wgpu 的 `BindGroup` 概念会渗透到你的材质系统设计里。
- **版本依赖**：第三方库升级大版本时（如 wgpu 0.19 -> 22.x），你的引擎可能需要大量适配。
- **调试深度受限**：出现 GPU 崩溃或渲染错误时，你调试的是"引擎 -> 第三方库 -> 底层 API"三层，定位问题的链路变长。
- **ECS 映射不自然**：这些库通常不是为 ECS 设计的，它们的 Device/Context/Queue 是传统的面向对象句柄，需要额外包装才能融入 ECS。

**失效条件**：当引擎需要某个底层 API 的尖端特性（如 Vulkan 的某个扩展）而第三方库尚未支持时；或者当引擎的渲染架构与第三方库的设计理念冲突时（如 wgpu 的严格验证与高频动态资源修改的矛盾）。

### 引擎对照：三端在这个问题上的选择

> 我们在解决的是「RHI 应该有多厚」这个问题。
>
> - **chaos** 选择了**双层架构**：底层 `graphics_interface` 是薄抽象（接近 1:1 映射各平台 API），高层 `render/RHI` 是中等厚度抽象（`DynamicRHI`、`RHICommandContext` 提供面向渲染管线的易用接口）。上层通常用高层，"忍者开发者"可以直接下探到底层。
> - **UE** 选择了**非常厚的命令抽象**：`FRHICommandList` 不直接映射任何底层 API，而是定义了一套 UE 自己的命令体系（`FRHICommandDrawPrimitive`、`FRHICommandSetViewport` 等），用内存栈分配器批量录制，再由 RHI 线程翻译执行。这让它能统一管理跨线程录制、延迟删除、PSO 缓存等复杂机制。
> - **Bevy** 选择了**第三方抽象 + 包装层**：底层直接依赖 `wgpu`，Bevy 只写了一个薄薄的 `RenderDevice` 和 `WgpuWrapper`，把 wgpu 的类型变成 ECS `Resource`。RHI 的厚度主要来自 wgpu 本身，而不是 Bevy 自己。

### 决策分析与推荐

**默认推荐：以分支 B（中等厚度自研命令抽象）为骨架，但接口设计应吸收 UE 的厚命令抽象设计意图，预留向 UE 式命令体系演进的架构空间。**

理由如下：
1. **UE 的 `FRHICommandList` 证明了厚命令抽象是工业最优解**：它不是过度设计，而是解决"跨线程录制 + 跨后端一致性 + 延迟删除 + PSO 缓存"这一系列问题的**系统性答案**。如果我们一开始的接口设计就按薄抽象做，未来想补这些能力时，接口形状会被锁死，重构成本远高于初期多写的一些代码。
2. **但这不等于初期要实现 UE 的全部复杂度**。正确的做法是**接口朝 UE 对齐，实现分阶段填充**：
   - 阶段 5 初期：命令缓冲区的实现可以极其简单——一个 `LinearAllocator` 一帧一清，`switch-case` 翻译执行，单线程录制。
   - 阶段 5 中期：当需要多线程并行时，你已经有了命令缓冲的架构基础，只需把"单线程翻译"扩展为"多线程并行翻译"（吸收 UE 的 Parallel Translation Threads）。
   - 阶段 5 后期：引入 RHI 线程、延迟删除队列、PSO 异步编译时，它们都自然落在"命令缓冲 + 翻译执行"的框架内，无需推翻重来。
3. **第三方库（分支 C）可以作为后端实现**，但引擎与第三方库之间仍必须保留自己的命令抽象层。这保证了未来可以更换后端（如从 wgpu 切换到原生 D3D12）而不重写上层渲染代码。
4. **薄抽象（分支 A）不值得考虑**：它既不能解决跨后端一致性，又不能为多线程预留空间。UE、chaos 都没有采用纯薄抽象作为上层接口。

**具体策略**：
- 定义引擎自己的 `IRenderDevice` / `IRHICommandList` / `IRHIPipeline` / `IRHIBuffer` / `IRHITexture` 抽象接口。
- 接口语义对齐 UE 的 `FRHICommandList`：上层只"发出命令"（`DrawPrimitive`、`SetViewport`、`SetPipeline`），不感知命令是立即执行还是缓冲延迟。
- 初期只实现 **D3D12** 后端。Vulkan 作为明确计划的第二后端，在 D3D12 跑通后再启动。
- 命令录制**初始实现为即时执行风格**（`IRHICommandList` 内部直接调 D3D12 API），但接口设计必须是"命令式"的——预留 `Begin/End` 生命周期、`Submit` 批量提交点。这是"接口对齐 UE，实现简化"的关键。

**在 X 条件下可切换为其他分支**：
- 如果你需要**旧设备兼容**或**macOS/Linux 的快速兜底**，可以保留一个最小化的 **OpenGL** 后端，但接口层绝不允许 OpenGL 的状态机思维渗透。GL 后端内部必须模拟命令缓冲（延迟执行状态变更）。
- 如果你有明确的 **Web 部署需求**，再考虑引入 **WebGPU/wgpu-native** 作为额外后端。

**遗留问题**：即使确定了厚度，命令的录制模型仍有分歧——是"上层直接调用 RHI 接口即时执行"，还是"上层录制到命令缓冲区再批量提交"？这是问题 3 的核心矛盾。

---

## 问题 3：命令应该"即时执行"还是"缓冲录制"？

### 场景与根因

假设你正在实现一个前向渲染 Pass，一帧要画 1000 个物体。如果每个 Draw Call 都直接调用底层 API（如 `glDrawElements` 或 `vkCmdDrawIndexed`），会发生什么？

对于 **OpenGL**，这基本就是标准做法——`glDrawElements` 会立即进入驱动，驱动可能批量优化，也可能直接发进 GPU。CPU 和 GPU 之间没有显式解耦。

对于 **D3D12/Vulkan**，底层 API 本身就设计为"录制到命令缓冲，再批量提交"。如果你每个 Draw Call 都 `begin command buffer -> record one draw -> end -> submit`，`vkQueueSubmit` 的每次调用都有显著的 CPU 开销（驱动需要验证、刷新缓存、与 GPU 同步）。NVIDIA 的官方建议明确说："尽量减少 Queue 提交的次数。"

更深层的矛盾是：**CPU 需要准备下一帧的数据，GPU 还在执行上一帧的指令**。如果 CPU 每发一条命令都等待 GPU 完成，帧率直接崩溃。

### 分支 A：即时执行（Immediate Mode）

**核心思路**：上层调用 `DrawIndexed()` 时，RHI 内部**立即**转换为底层 API 调用。对上层来说，没有"命令缓冲"的概念。

```cpp
// 即时执行：OpenGL 后端示例
class GLCommandContext : public ICommandContext {
public:
    void DrawIndexed(uint32_t count, uint32_t start, int32_t base_vertex) override {
        glDrawElementsBaseVertex(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                                  reinterpret_cast<void*>(start * sizeof(uint32_t)), base_vertex);
    }
};
```

**适用场景**：OpenGL/D3D11 后端、Draw Call 数量较低（< 1000/帧）、实现简单优先。

**隐藏代价**：
- 多线程无法并行录制——即时执行意味着线程安全必须由底层 API 保证，而 OpenGL 上下文是单线程的。
- CPU-GPU 完全耦合——CPU 在 `DrawIndexed` 返回前，可能已被驱动阻塞（驱动内部缓冲满时）。
- 难以做跨帧优化——因为你没有一帧的完整命令视图，无法重排、合并或预取。

**失效条件**：当你需要支持 D3D12/Vulkan 并发挥其多线程录制优势时，即时执行模型与底层 API 的设计理念直接冲突。

### 分支 B：延迟命令缓冲（Deferred Command Buffer）

**核心思路**：上层调用 `DrawIndexed()` 时，RHI 把命令序列化到一块 CPU 内存缓冲区中。整帧录制完成后，一次性把缓冲区翻译成底层 API 的命令列表并提交。

```cpp
// 延迟录制：引擎命令缓冲区
struct CmdDrawIndexed {
    static constexpr RenderCmdType kType = RenderCmdType::DrawIndexed;
    PipelineHandle pipeline;
    BufferHandle vertex_buffer;
    BufferHandle index_buffer;
    uint32_t index_count;
    uint32_t start_index;
    int32_t base_vertex;
};

class RenderCommandBuffer {
    LinearAllocator mem;  // 线性分配器，一帧一清
    
public:
    template<typename TCmd, typename... Args>
    void Emplace(Args&&... args) {
        auto* cmd = mem.Alloc<RenderCmdHeader>();
        cmd->type = TCmd::kType;
        cmd->payload_size = sizeof(TCmd);
        new (mem.Alloc<TCmd>()) TCmd(std::forward<Args>(args)...);
    }
    
    void DrawIndexed(PipelineHandle pso, BufferHandle vb, BufferHandle ib,
                     uint32_t count, uint32_t start, int32_t base) {
        Emplace<CmdDrawIndexed>(pso, vb, ib, count, start, base);
    }
};
```

执行阶段（通常在帧末，由 RHI 线程或主线程完成）：

```cpp
void ExecuteCommandBuffer(RenderCommandBuffer* cb, NativeCommandList* native) {
    uint8_t* ptr = cb->mem.GetBase();
    while (ptr < cb->mem.GetCurrent()) {
        auto* header = reinterpret_cast<RenderCmdHeader*>(ptr);
        ptr += sizeof(RenderCmdHeader);
        switch (header->type) {
            case RenderCmdType::DrawIndexed: {
                auto* cmd = reinterpret_cast<CmdDrawIndexed*>(ptr);
                native->BindPipeline(cmd->pipeline);
                native->BindVertexBuffer(cmd->vertex_buffer);
                native->BindIndexBuffer(cmd->index_buffer);
                native->DrawIndexed(cmd->index_count, cmd->start_index, cmd->base_vertex);
                break;
            }
            // ... 其他命令
        }
        ptr += header->payload_size;
    }
}
```

**适用场景**：需要跨平台一致性、希望上层完全不感知底层 API 差异、需要为未来多线程录制预留架构空间。

**隐藏代价**：
- **两重间接开销**：上层调用 -> 写入引擎命令缓冲区 -> 执行时翻译为底层 API 调用。对于简单场景，这个开销可能比直接裸调用高 5%~15%。
- **内存带宽**：一帧的命令数据全部存在 CPU 内存中，极端场景下（数万 Draw Call + 大量状态切换）命令缓冲区本身可能占用数 MB。
- **Barrier 语义复杂**：如果你完全隐藏底层 API 的 Barrier，RHI 需要自己推导资源状态转换——这非常容易出错（漏 Barrier 导致画面闪烁或 GPU 崩溃，多 Barrier 导致性能下降）。

**失效条件**：当 CPU 命令缓冲区的录制和翻译本身成为瓶颈时。极端优化场景下（如 DOOM Eternal 的渲染器），引擎会直接使用原生 API 命令列表，避免任何中间层。

### 分支 C：混合模型——即时接口 + 内部自动缓冲

**核心思路**：对外暴露的 RHI 接口看起来是"即时"的（`DrawIndexed` 立即返回），但内部实现自动维护一个底层命令列表，在合适的时机批量提交。

```cpp
// 混合模型：对外即时，对内缓冲
class D3D12CommandContext : public ICommandContext {
    ID3D12GraphicsCommandList* d3d12_list;  // 内部维护
    bool inside_render_pass = false;
    
public:
    void DrawIndexed(uint32_t count, uint32_t start, int32_t base) override {
        // 直接写入 D3D12 命令列表，没有引擎中间层
        d3d12_list->DrawIndexedInstanced(count, 1, start, base, 0);
    }
    
    void Flush() {
        // 内部自动关闭列表、提交到队列、获取新列表
        d3d12_list->Close();
        queue->ExecuteCommandLists(1, &list);
        // 重置并继续...
    }
};
```

**适用场景**：D3D12/Vulkan 后端为主，想获得原生 API 性能，同时保持上层接口简洁。

**隐藏代价**：
- 不同后端的"缓冲策略"不同：D3D12 可以一个命令列表录到底；OpenGL 根本没有列表。上层代码如果依赖"Flush 时机"，会引入跨后端行为差异。
- 调试困难：上层只调了一个 `DrawIndexed`，但画面直到 `Flush` 才更新，增加了时序问题的排查难度。

### 引擎对照

> 我们在解决的是「命令录制模型」这个问题。
>
> - **chaos** 的 `RHICommandContext` 提供了即时风格的接口（`drawIndexedPrimitive()` 直接调底层），但内部通过 `MeshDrawCommand` 结构支持多线程收集后统一提交——属于**分支 C 的变体**。
> - **UE** 是**分支 B 的极致**：`FRHICommandList` 完全使用延迟命令缓冲，命令分配在内存栈上形成侵入式链表，由 RHI 线程统一翻译执行。渲染线程和 RHI 线程通过这套机制完全解耦。
> - **Bevy/wgpu** 是**分支 C**：对外暴露 `CommandEncoder`（即时写入风格），但 `CommandEncoder` 内部构建 `CommandBuffer`，最后由 `Queue::submit()` 批量提交。开发者感知不到中间过程，但 wgpu 自动处理了验证和缓冲。

### 决策分析

**默认推荐：分支 B（延迟命令缓冲），但阶段 5 初期采用"简化版延迟缓冲"实现。**

理由：
1. **UE 选择了分支 B 的极致形态，这不是偶然**。UE 的 `FRHICommandList` 之所以采用完全延迟的引擎级命令缓冲，是因为只有这种模型才能统一解决以下问题：
   - 跨线程并行录制（多线程写入同一块命令内存，或各自写入后合并）
   - 渲染线程与 RHI 线程解耦（渲染线程只生成引擎命令，RHI 线程翻译为底层 API）
   - 延迟删除资源（命令缓冲中记录了资源引用，RHI 线程知道何时 GPU 不再使用）
   - PSO 缓存与异步编译（命令翻译阶段可以拦截缓存未命中，启动后台编译而不阻塞）
   如果采用分支 C（混合模型），这些问题在需要解决时会发现"混合模型的内部状态与上层接口纠缠在一起"，重构成本极高。
2. **分支 B 的"实现负担"被高估了**。一帧的命令缓冲本质上就是 `LinearAllocator` + 类型标签 + `switch-case` 翻译。在阶段 5 初期，这个翻译器可以只有 10 个 `case`，单线程执行，代码量不超过 200 行。但它给你的是**正确的架构方向**。
3. **分支 C 的"简洁"是假象**。它看似省了一层"引擎命令"，但 D3D12 后端内部要维护 `ID3D12CommandList`，OpenGL 后端要模拟状态机缓冲，WebGPU 后端要攒 `CommandEncoder`——三种后端的"内部缓冲策略"完全不同。上层代码如果依赖任何时序语义（如"BindTexture 之后 Draw 才生效"），跨后端行为差异就会泄漏。分支 B 用一层统一的引擎命令消除了这种泄漏。
4. **分支 A（纯即时）在 D3D12/Vulkan 下完全无法工作**，因为显式 API 本身就必须构建命令列表。它连选项都不构成。

**"简化版延迟缓冲"的实现策略**：
- 定义引擎级命令枚举（如 `CmdDrawIndexed`、`CmdSetViewport`、`CmdBindPipeline`），结构与 UE 的 `FRHICommand*` 类似但大幅简化。
- 每帧分配一块命令内存（`LinearAllocator`，4~16MB 预分配，一帧一清）。
- 单线程录制：当前 System 调用 `DrawIndexed()` 时，直接 `new (allocator) CmdDrawIndexed(...)` 到内存中。
	- 单线程翻译：帧末由 `RHISubmitSystem` 遍历命令内存，`switch-case` 翻译成 D3D12 API 调用。
- **接口上完全不暴露"即时"语义**：`IRHICommandList::DrawIndexed()` 的文档明确写"将绘制命令追加到当前命令缓冲，不会立即执行"。

**关键设计细节**：
- 命令内存的分配器必须是**栈式/线性的**，不允许随机释放。这对应 UE 的 `FMemStack` 思想。
- 预留命令内存的**多线程分配能力**：初期用原子偏移量即可，`alloc_offset.fetch_add(size)`。未来多线程录制时不需要改分配策略。
- 预留 `Submit()` 和 `Present()` 的显式边界：`Submit(command_list)` 表示"翻译并提交这一批命令"，`Present()` 表示"交换链呈现"。这与 UE 的 RHI 线程提交模型一致。

**遗留问题**：选择了延迟命令缓冲，下一个问题立刻浮现——这些命令存在哪里？一帧数千条变长命令的内存分配策略直接决定录制的 CPU 开销和缓存友好性。命令内存该如何布局与管理？这是问题 4 的核心。

---
## 问题 4：命令缓冲区的内存布局与分配策略如何设计？

### 场景与根因

我们已经选择了**延迟命令缓冲**（问题 3），但一个关键细节被有意推迟了：这些命令在 CPU 内存中到底长什么样？一帧要录 5000 条命令——`SetViewport` 只有 16 字节，`DrawIndexed` 需要 48 字节，`ResourceBarrier` 可能是 24 字节。如果每条命令都用 `new`/`malloc` 分配，16 ms 帧时间内光是堆分配的开销就能吃掉 2~4 ms，帧率直接从 60 fps 掉到 40 fps。

更深层的根因是：**延迟命令缓冲把"GPU 做什么"序列化成了 CPU 内存中的字节流**，但这块内存的管理策略本身就是一个独立的工程问题，涉及三个互相牵制的约束：

1. **分配速度**：录制阶段每微秒都在竞争，分配必须是 O(1)，最好无锁。
2. **内存连续性**：命令在执行阶段被顺序遍历，如果内存不连续，CPU 缓存预取会失效。
3. **变长支持**：不同命令的 Payload 大小不一，固定尺寸会浪费内存，变长又会引入对齐和碎片问题。

### 分支 A：`std::vector<std::unique_ptr<CommandBase>>`（多态容器）

**核心思路**：每条命令是一个派生类实例，用虚接口统一执行。

```cpp
class RenderCommand {
public:
    virtual void Execute(NativeContext* ctx) = 0;
    virtual ~RenderCommand() = default;
};

class CmdDrawIndexed : public RenderCommand {
    PipelineHandle pso; BufferHandle vb; BufferHandle ib;
    uint32_t count, start; int32_t base;
public:
    void Execute(NativeContext* ctx) override {
        ctx->BindPipeline(pso); ctx->BindVertexBuffer(vb); ctx->DrawIndexed(count, start, base);
    }
};

// 录制时
std::vector<std::unique_ptr<RenderCommand>> cmd_list;
cmd_list.push_back(std::make_unique<CmdDrawIndexed>(pso, vb, ib, count, start, base));
```

**适用场景**：快速原型、命令数量极少（< 100/帧）、需要命令级调试（虚函数可以挂断点）。

**隐藏代价**：
- **new/delete 开销**：每条命令一次堆分配，5000 条命令就是 5000 次 `malloc`/`free`。`malloc` 在 Windows 上的平均耗时约 50~100 ns，5000 次累计 0.25~0.5 ms——这还没算析构时的虚函数调用。
- **内存不连续**：`std::unique_ptr` 是 8 字节指针，实际命令对象散落在堆各处。执行阶段遍历 `cmd_list` 时，每次解引用都是缓存未命中。
- **虚函数表膨胀**：每个命令对象带一个 8 字节 vptr，5000 条命令额外 40 KB 内存，且虚调用阻止了编译器内联。

**失效条件**：Draw Call > 500/帧，或需要多线程并行录制时，`new` 的锁竞争会让多核扩展性归零。

### 分支 B：`LinearAllocator` + 类型标签 + 变长 Payload

**核心思路**：从一块预分配的连续内存中**线性分配**命令，每条命令前带一个类型头，执行时按顺序解析。

```cpp
// 命令头：固定 8 字节
struct RenderCmdHeader {
    RenderCmdType type;       // 4 字节：enum class
    uint32_t payload_size;    // 4 字节：Payload 长度（含对齐填充）
};

// 变长 Payload 示例
struct alignas(8) CmdDrawIndexed {
    static constexpr RenderCmdType kType = RenderCmdType::DrawIndexed;
    PipelineHandle pso;
    BufferHandle vb, ib;
    uint32_t index_count;
    uint32_t start_index;
    int32_t base_vertex;
    // 48 字节，已对齐到 8 字节边界
};

class LinearAllocator {
    uint8_t* base;
    std::atomic<size_t> offset;  // 原子偏移，预留多线程能力
    size_t capacity;
public:
    template<typename T, typename... Args>
    T* Alloc(Args&&... args) {
        size_t aligned_size = (sizeof(T) + 7) & ~7;  // 向上对齐到 8
        size_t old_offset = offset.fetch_add(aligned_size + sizeof(RenderCmdHeader));
        // ... 边界检查 ...
        auto* header = reinterpret_cast<RenderCmdHeader*>(base + old_offset);
        header->type = T::kType;
        header->payload_size = aligned_size;
        auto* payload = reinterpret_cast<T*>(base + old_offset + sizeof(RenderCmdHeader));
        return new (payload) T(std::forward<Args>(args)...);
    }
    void Reset() { offset.store(0); }  // 一帧一清
};
```

**适用场景**：工业级引擎的首选，Draw Call 数量任意，需要极致的录制性能。

**隐藏代价**：
- **内存预分配压力**：需要预估每帧命令的最大内存消耗。如果预估不足（如突发的大量粒子渲染），需要 fallback 到次级分配器或断言。UE 的做法是预分配 `FMemStack` 并允许动态扩容（触发一次大的重新分配）。
- **无随机释放**：只能整段重置，不能单独删除某条命令。这意味着命令一旦写入就不能撤销——如果某条 Pass 被裁剪，它在内存中只是"跳过执行"，不会被回收。
- **对齐复杂性**：不同后端的命令对齐要求不同（D3D12 的命令列表对 8 字节对齐不敏感，但某些 GPU 对 16 字节或 32 字节对齐有特殊优化）。引擎命令层的对齐策略必须与底层翻译器协商一致。

**失效条件**：需要**编辑或撤销已录制命令**的场景（如编辑器中撤销某次绘制）。此时线性分配器的"追加-only"语义不适用。

### 分支 C：固定大小 Command Union

**核心思路**：所有命令被填充到同一个固定大小（取最大命令的尺寸），用 `union` 或 `std::variant` 存储。

```cpp
constexpr size_t kMaxCmdSize = 64;

struct FixedCommand {
    RenderCmdType type;
    uint8_t payload[kMaxCmdSize - sizeof(RenderCmdType)];
};

std::vector<FixedCommand> cmd_list;  // 连续，无动态分配
```

**适用场景**：命令种类少、最大命令尺寸可控、极度追求分配简单性的嵌入式场景。

**隐藏代价**：
- **内存浪费**：如果 90% 的命令是 `SetViewport`（16 字节），却被填充到 64 字节，内存利用率只有 25%。一帧 5000 条命令浪费约 240 KB——对桌面平台可忽略，但对移动端不可忽视。
- **扩展困难**：新增一种命令若超过 `kMaxCmdSize`，必须改全局常量并重新编译所有模块。

**失效条件**：命令种类频繁扩展，或命令尺寸差异巨大（如含大型矩阵的 `PushConstants` 命令 vs 简单的 `SetViewport`）。

### 分支 D：Ring Buffer / 多帧复用

**核心思路**：不每帧释放内存，而是维护一个循环缓冲区，复用前几帧已完成的内存。

```cpp
class RingCommandBuffer {
    static constexpr uint32_t kFramesInFlight = 3;
    uint8_t* buffer[kFramesInFlight];
    uint32_t write_frame = 0;
    Fence frame_fences[kFramesInFlight];
    
public:
    void BeginFrame() {
        // 等待 3 帧前的 Fence，确保 GPU 已完成使用该段内存
        WaitFence(frame_fences[write_frame]);
        // 复用这段内存，无需新分配
        allocator.Reset(buffer[write_frame]);
    }
    void EndFrame() {
        SignalFence(frame_fences[write_frame]);
        write_frame = (write_frame + 1) % kFramesInFlight;
    }
};
```

**适用场景**：命令量波动剧烈（如空旷场景 1 MB/帧，团战场景 8 MB/帧），希望平滑内存使用峰值。

**隐藏代价**：
- **Fence 管理复杂度**：必须精确追踪 GPU 完成时间，否则复用未完成帧的内存会导致 GPU 崩溃。
- **内存峰值固定**：Ring Buffer 的大小必须按最坏情况（8 MB）分配，即使大部分时间只用 1 MB。
- **不适合单帧多 Submit**：如果一帧内有多次 `Submit`（如先提交 Shadow Pass，再提交 Main Pass），Ring Buffer 的帧边界语义需要扩展为"Submit 边界"语义。

**失效条件**：内存极其紧张、或命令缓冲区需要频繁 Resize（如编辑器中动态增删 Viewport）。

### 引擎对照

> 我们在解决的是「命令缓冲区的内存管理」这个具体子问题。
>
> - **chaos** 的 `RHICommandList` 使用**内存栈式分配**（类似分支 B），但增加了**侵入式链表**——每条命令自带 `next` 指针，翻译时无需重新计算偏移，直接遍历链表。这牺牲了 8 字节/命令的内存，换取了翻译阶段的缓存预取友好性。
> - **UE** 的 `FMemStack` 是分支 B 的工业级形态：支持**嵌套作用域分配**（`FMemMark`）和**动态扩容**（当当前页满时分配新页并链接）。UE 的渲染线程每帧从 `FMemStack` 分配数 MB 的命令内存，帧末一次性释放。
> - **Bevy / wgpu** 的命令缓冲由 wgpu 内部管理（对外不可见），但 Bevy 在 ECS 层使用了**提取模式**（Extract）——先收集渲染资源到 `RenderWorld`，再统一生成 wgpu 命令。Bevy 不直接面对"命令内存布局"问题，但它的 `RenderPhase` 排序后批量提交，本质上享受了分支 B 的"连续内存遍历"优势。

### 决策分析与推荐

**默认推荐：分支 B（`LinearAllocator` + 类型标签 + 变长 Payload），阶段 5 初期按简化版实现。**

理由：
1. **UE 和 chaos 都选择了基于线性分配的命令内存模型**，这不是偶然。5000+ Draw Call 场景下，`new` 的堆分配（分支 A）会让录制线程的 CPU 时间翻倍。
2. **固定大小 Union（分支 C）的内存浪费在桌面端可忽略，但扩展性差**。如果阶段 8 引入 Compute Shader Dispatch 命令（Payload 可能含 64 字节工作群组参数），固定大小会被迫膨胀到 128 字节，所有旧命令跟着一起浪费。
3. **Ring Buffer（分支 D）是优化项而非必选项**。在内存压力不大的桌面开发阶段，一帧一清（`Reset()`）的实现简单且足够。当移动平台优化或内存 profiling 暴露峰值问题时，再引入 Ring Buffer 作为可插拔策略。
4. **对齐到 8 字节是阶段 5 的安全默认**：x64 平台的所有基础类型都满足 8 字节对齐，D3D12/Vulkan 的命令列表也不对 16 字节以下的对齐做强制要求。未来如果针对特定 GPU 做优化，可以提升到 16 或 32 字节对齐，但这不改变架构。

**简化版实现策略**：
- 预分配 4 MB 命令内存（`std::vector<uint8_t>` 或 `std::unique_ptr<uint8_t[]>`）。
- 每帧开始时 `offset = 0`。
- `Alloc<T>()` 用原子 `fetch_add`（即使单线程也保留原子操作，零成本为未来多线程预留）。
- 执行阶段顺序遍历 `(Header + Payload)` 序列，`switch-case` 翻译。
- **暂不实现动态扩容**——4 MB 可以容纳约 8 万条 48 字节的 `DrawIndexed` 命令，远超阶段 5 的需求。

**遗留问题**：命令缓冲区里存储的是 `PipelineHandle`、`BufferHandle`、`TextureHandle` 等**句柄**，而不是底层 API 的裸指针。Handle 的设计决定了 RHI 的类型安全、生命周期管理和跨后端一致性。句柄应该怎么设计？这是问题 5 的核心。

---
## 问题 5：资源句柄（Handle）应该如何设计才能跨后端一致且安全？

### 场景与根因

假设你的 `MeshRenderSystem` 创建了一个顶点缓冲区。在 D3D12 后端中，这个缓冲区对应 `ID3D12Resource*`；在 Vulkan 后端中对应 `VkBuffer`；在 OpenGL 中对应一个 `GLuint`。如果上层代码直接用裸指针（或整数）存储这些引用，切换后端时所有数据结构——`MeshComponent`、`MaterialComponent`、甚至 `RenderWorld` 的存储布局——都要重写。

更隐蔽的问题是**生命周期安全**：当缓冲区被释放后，某个旧的引用如果仍然被使用，在 D3D12 中会变成悬空指针（导致 GPU 崩溃或非法内存访问），在 OpenGL 中可能复用到了新分配的同名纹理（导致画面错乱）。裸引用不提供任何"已释放"的检测机制。

根因在于：RHI 作为跨后端抽象，必须定义一套**后端无关的资源引用机制**。这套机制要同时满足三个看似矛盾的约束：

1. **类型安全**：不能把 `BufferHandle` 误传给需要 `TextureHandle` 的接口（编译期捕获）。
2. **生命周期安全**：资源被释放后，旧 Handle 不会变成悬空引用——至少能检测出 use-after-free。
3. **低开销**：从 Handle 到实际 GPU 资源的映射必须是 O(1)，且内存占用要小（ECS Component 中存 Handle 而不是完整对象）。

### 分支 A：裸虚接口指针（`IRHIBuffer*`）

**核心思路**：所有 RHI 资源继承自 `IRHIResource` 基类，上层直接存基类指针或接口指针。

```cpp
class IRHIResource {
public:
    virtual void AddRef() = 0;
    virtual void Release() = 0;
    virtual uint32_t GetRefCount() const = 0;
    virtual ~IRHIResource() = default;
};

class IRHIBuffer : public IRHIResource {
public:
    virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual uint64_t GetSize() const = 0;
};

// 上层 Component
struct MeshComponent {
    IRHIBuffer* vertex_buffer = nullptr;  // 裸指针，需要外部管理生命周期
    IRHIBuffer* index_buffer = nullptr;
};
```

**适用场景**：快速原型、命令数量少、不切换后端。

**隐藏代价**：
- **生命周期管理混乱**：`IRHIBuffer*` 是裸指针，谁负责 `Release()`？如果 `MeshComponent` 析构时 `Release()` 一次，但另一个 System 也持有了这个指针，就会 double-free 或提前释放。
- **虚函数开销**：每次通过接口调用都需要一次虚表跳转。虽然单次开销只有几纳秒，但在高频路径（如命令翻译阶段批量解析 Handle）中，虚函数会阻止编译器内联和向量化。
- **ECS 不友好**：在 ECS 的 SoA/Archetype 存储中，Component 应该存紧凑的 POD 数据。存指针意味着每个 Component 带一个 8 字节指针，且指针指向的堆对象散落在内存各处——cache locality 被彻底破坏。
- **跨后端仍然泄漏**：`IRHIBuffer` 的接口形状（如 `Map()`/`Unmap()`）仍然隐含了特定 API 的假设。某些后端（如 WebGPU）的缓冲区映射模型完全不同。

**失效条件**：需要多线程并发访问资源、或要求严格的 cache locality 时，虚接口指针的线程安全（需要锁或原子引用计数）和内存布局劣势会被放大。

### 分支 B：Typed Handle（Index + Generation）

**核心思路**：用轻量的整型结构体作为 Handle，内部包含**数组索引**和**世代号**。所有实际资源对象存储在一个后端内部的全局 Table（密集数组）中。

```cpp
// 类型标签，用于编译期类型区分
template<typename Tag>
struct Handle {
    uint16_t index;      // 在资源 Table 中的数组索引
    uint16_t generation; // 世代号，防止悬空引用
    
    bool IsValid() const { return index != 0xFFFF; }
    bool operator==(Handle other) const {
        return index == other.index && generation == other.generation;
    }
};

// 类型安全的资源句柄
struct BufferTag {};
struct TextureTag {};
struct PipelineStateTag {};
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using PipelineStateHandle = Handle<PipelineStateTag>;

// 后端内部的全局 Table（以 Buffer 为例）
template<typename T, typename Tag>
class ResourceTable {
    struct Slot {
        T resource;
        uint16_t generation = 1;
        bool occupied = false;
    };
    std::vector<Slot> slots;
    std::vector<uint16_t> free_list;
public:
    Handle<Tag> Alloc(T&& resource) {
        if (!free_list.empty()) {
            uint16_t idx = free_list.back(); free_list.pop_back();
            slots[idx].resource = std::move(resource);
            slots[idx].occupied = true;
            return {idx, slots[idx].generation};
        }
        uint16_t idx = static_cast<uint16_t>(slots.size());
        slots.push_back({std::move(resource), 1, true});
        return {idx, 1};
    }
    void Free(Handle<Tag> handle) {
        if (handle.index >= slots.size()) return;
        auto& slot = slots[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) return;  // 双 free 或已失效
        slot.occupied = false;
        slot.generation++;  // 世代递增，旧 Handle 永久失效
        free_list.push_back(handle.index);
    }
    T* Resolve(Handle<Tag> handle) {
        if (handle.index >= slots.size()) return nullptr;
        auto& slot = slots[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) return nullptr;
        return &slot.resource;
    }
};
```

**适用场景**：ECS 原生引擎、需要极致类型安全、要求 Component 内存紧凑。

**隐藏代价**：
- **Table 查找开销**：从 Handle 到实际资源需要一次数组索引。虽然数组访问是 O(1) 且缓存友好，但相比直接指针解引用仍多了一层间接。
- **索引位数限制**：`uint16_t index` 最多支持 65535 个同类型资源。对于纹理，这在大型开放世界场景中可能不够。可升级为 `uint32_t`，但 Handle 大小从 4 字节变为 8 字节。
- **跨 DLL/模块边界**：如果 Handle 定义在引擎核心模块，而 Table 实现位于后端插件（DLL），Resolve 操作需要跨 DLL 调用。不过通常 Table 以内联方式放在头文件中，或 Handle 的 Resolve 通过 RHI 的虚接口完成。

**失效条件**：需要频繁传递资源到外部库（如物理引擎或 AI 库）且外部库不接受自定义 Handle 类型时，需要额外的转换层。

### 分支 C：强引用智能指针（`std::shared_ptr<IRHIResource>`）

**核心思路**：使用 `std::shared_ptr` 管理资源生命周期，上层无需手动 `Release()`。

```cpp
struct MeshComponent {
    std::shared_ptr<IRHIBuffer> vertex_buffer;
    std::shared_ptr<IRHIBuffer> index_buffer;
};
```

**适用场景**：OOP 风格的引擎、资源所有权复杂（多个 System 共享同一份资源）、开发者不想手动管理引用计数。

**隐藏代价**：
- **原子引用计数开销**：每次拷贝 `shared_ptr` 都需要原子操作（`fetch_add`）。在 ECS 的 System 遍历中，如果每个实体都拷贝一次 Handle，原子操作的数量与实体数成正比。
- **控制块内存**：`shared_ptr` 内部有一个控制块（含强引用计数、弱引用计数、删除器），额外分配一次堆内存。
- **ECS 架构冲突**：ECS 的核心理念是"Component 存数据，System 存逻辑"。`shared_ptr` 把"逻辑"（引用计数管理）嵌入了"数据"（Component），破坏了 Component 的 POD 属性，也阻止了批量 memcpy 和缓存预取。

**失效条件**：高性能 ECS 场景。`shared_ptr` 在 Entity 数量 > 10,000 时，引用计数的原子竞争会成为明显瓶颈。

### 引擎对照

> 我们在解决的是「跨后端资源引用」这个具体子问题。
>
> - **chaos** 选择了**分支 B 的变体**：底层 `graphics_interface` 使用裸指针和引用计数，但高层的 `render/RHI` 层使用 `Handle` + `ResourceTable`。`Handle` 是 32 位整型（含 type tag 和 index），`ResourceTable` 提供 `GetPtr(handle)` 转换。这种双层设计让上层代码完全与底层 API 的指针类型解耦。
> - **UE** 选择了**分支 A + 自定义引用计数**：`FRHIResource` 是所有 GPU 资源的基类，带虚函数 `AddRef()` / `Release()`。上层使用 `TRefCountPtr<FRHIBuffer>`（类似 `intrusive_ptr`），不是 `shared_ptr`。UE 没有选择 Handle 模型，因为它的 RHI 层是传统的 OOP 设计，资源对象本身已经携带了足够的元数据。但在 ECS 下，这种设计需要重新表达。
> - **Bevy / wgpu** 的选择是**分支 B 的 WebGPU 变体**：wgpu 的 `Id<Buffer>` 本质上就是 Index + Generation 的 Handle。Bevy 在 ECS 中直接存储 wgpu 的 `Id`，并通过 `RenderDevice` Resource 调用 `resolve`。这与我们推荐的 Typed Handle 模型完全一致。

### 决策分析与推荐

**默认推荐：分支 B（Typed Handle + Generational Index + Resource Table）。**

理由：
1. **与 ECS 架构天然同构**：ECS 的 `Entity` 本身就是 Index + Generation 的 Handle，`ResourceTable` 就是资源的 `Archetype`。Component 中存 4 字节 Handle 比存 8 字节指针更紧凑，且 Table 的密集数组存储保证了 cache locality。
2. **类型安全不可妥协**：`BufferHandle` 和 `TextureHandle` 是不同的 C++ 类型，编译器会阻止混用。这比运行时检查（如 UE 的 `GetType()`）更可靠、零开销。
3. **生命周期安全是工业底线**：Generational Index 让 use-after-free 可被检测（`Resolve()` 返回 `nullptr`），而不是像裸指针那样直接崩溃。虽然不能完全防止逻辑错误，但至少不会 corrupt GPU 状态。
4. **UE 的 `TRefCountPtr` 在 ECS 下没有直接映射**。UE 的设计基于 OOP 的"资源对象即引用计数载体"，而 ECS 中 Component 应该是无行为的纯数据。把引用计数从 Component 中剥离到全局 Table，是 ECS 兼容且吸收 UE 生命周期管理意图的正确方式。

**具体设计**：
- Handle 用 `uint32_t`（16-bit index + 16-bit generation）或 `uint64_t`（32-bit index + 32-bit generation）。阶段 5 初期用 `uint32_t` 足够。
- `ResourceTable` 对每个资源类型（Buffer、Texture、PipelineState）各一个实例。
- `Free()` 不立即销毁底层 API 资源，而是推入**延迟删除队列**（详见 [[GPU资源生命周期与PSO管理]]）。`ResourceTable` 只负责 Handle 的分配回收，底层资源的实际释放由后端自己决定。
- 命令缓冲区中只存 Handle（4 字节），不存裸指针。翻译阶段用 `Resolve(handle)` 转成底层 API 资源。

**遗留问题**：Handle 和 Table 解决了"如何引用资源"，但"创建失败怎么办"、"运行时 GPU 错误怎么捕获"、"RenderDoc 怎么识别我的 Pass"——这些问题指向 RHI 的**错误处理与调试抽象层**。不同 API 的错误模型差异巨大，如何统一？这是问题 6 的核心。

---
## 问题 6：错误处理与调试抽象层——RHI 如何统一不同 API 的诊断能力？

### 场景与根因

你在 D3D12 上创建 PSO，传入了一个错误的 `BlendStateDesc`，D3D12 Debug Layer 立刻输出："D3D12 ERROR: CreateGraphicsPipelineState: BlendDesc.RenderTarget[0].BlendEnable is TRUE but BlendOp is D3D12_BLEND_OP_ADD..." 函数返回 `E_INVALIDARG`。同样的错误在 Vulkan 上，Validation Layer 输出一长串回调消息，函数返回 `VK_ERROR_INITIALIZATION_FAILED`。而在 OpenGL 上，`glCompileShader` 不会返回任何错误码，你必须主动调用 `glGetShaderiv` 查编译状态，再用 `glGetShaderInfoLog` 读错误字符串。

如果上层代码直接面对这些差异，调试逻辑会彻底碎片化：D3D12 分支检查 HRESULT，Vulkan 分支检查 VkResult 并注册回调，OpenGL 分支轮询全局错误状态。更糟的是**GPU 崩溃**：D3D12 的 TDR（Timeout Detection and Recovery）会让驱动重置 GPU，Vulkan 的 Device Lost 会让整个逻辑设备不可用，OpenGL 可能只是黑屏但没有错误码。上层渲染代码无法以统一方式响应这些灾难性故障。

根因在于：GPU API 的错误模型在设计哲学上完全不同：

| API | 错误模型 | 调试机制 | 灾难性故障 |
|-----|---------|---------|-----------|
| D3D12 | HRESULT 返回值 | PIX + Debug Layer（独立 DLL） | TDR（驱动级 GPU 重置） |
| Vulkan | VkResult 返回值 | Validation Layer（可注入的消息回调） | `VK_ERROR_DEVICE_LOST` |
| OpenGL | 全局错误码堆栈 `glGetError` | RenderDoc + GL Debug Output | 驱动黑屏/崩溃 |
| Metal | NSError（Objective-C） | Xcode GPU Frame Capture | GPU 进程重启 |

RHI 必须提供两层统一抽象：
1. **统一错误传播**：创建/操作失败时，上层不需要知道后端是 HRESULT 还是 VkResult。
2. **统一调试标注**：让 RenderDoc、PIX、Xcode 等工具能识别引擎级语义（如"Shadow Pass"、"Player Material"），而不是只看到一堆底层 API 调用。

### 分支 A：C++ 异常

**核心思路**：所有 RHI 错误统一抛出 `RHIException`，上层用 `try/catch` 捕获。

```cpp
class RHIException : public std::exception {
    RHIBackend backend;
    RHIBackendErrorCode unified_code;
    std::string message;
public:
    const char* what() const noexcept override { return message.c_str(); }
};

// 后端内部转换
BufferHandle CreateBufferD3D12(const BufferDesc& desc) {
    ID3D12Resource* res = nullptr;
    HRESULT hr = device->CreateCommittedResource(..., &res);
    if (FAILED(hr)) {
        throw RHIException(RenderBackendType::D3D12, 
                           MapHRESULTToUnified(hr), 
                           "CreateBuffer failed");
    }
    return buffer_table.Alloc(res);
}
```

**适用场景**：错误处理逻辑复杂、需要跨多层调用栈传播错误、团队习惯使用异常。

**隐藏代价**：
- **热路径开销**：即使在未抛出时，异常机制也会增加二进制体积（ unwind table）和一定的运行时开销。对于高频创建/释放（如每帧创建临时 Uniform Buffer），异常安全代码可能增加 5~10% 的调用开销。
- **游戏行业惯例**：大量游戏引擎（包括 UE 的部分模块）禁用 C++ 异常，因为异常在主机平台（如某些旧版游戏机 SDK）上的行为不一致，且堆 unwinding 在实时系统中是不可预测的延迟来源。
- **GPU 崩溃不可捕获**：TDR 或 `VK_ERROR_DEVICE_LOST` 发生时，异常无法帮你恢复——GPU 已经挂了，异常只能让程序体面退出。

**失效条件**：项目禁用异常、或目标平台对异常支持不完善时。

### 分支 B：`Result<T, Error>` 返回值

**核心思路**：每个可能失败的 RHI 操作返回 `Result<T, RHIError>`，调用者必须显式检查。

```cpp
template<typename T>
struct Result {
    bool ok;
    T value;
    RHIError error;
    
    bool IsOk() const { return ok; }
    T Unwrap() { assert(ok); return std::move(value); }
};

Result<BufferHandle> CreateBuffer(const BufferDesc& desc) {
    // ... 后端实现 ...
    if (FAILED(hr)) return Result<BufferHandle>::Err(MapHRESULTToUnified(hr));
    return Result<BufferHandle>::Ok(handle);
}

// 上层调用
auto result = rhi->CreateBuffer(desc);
if (!result.IsOk()) {
    LOG_ERROR("Buffer creation failed: {}", result.error.Message());
    return fallback_buffer;
}
```

**适用场景**：要求严格的错误检查、Rust/C 风格错误处理、无异常项目。

**隐藏代价**：
- **调用点冗余**：每个 `CreateBuffer`、`CreateTexture`、`CreatePipelineState` 调用后都要写 `if (!result.IsOk())`，代码膨胀。
- **容易遗漏检查**：如果上层开发者忘记检查返回值，错误被静默忽略，后续使用无效 Handle 会导致更难调试的 GPU 崩溃。
- **流水线污染**：在渲染管线的链式调用中（创建 Buffer → 上传数据 → 绑定到 PSO），每个步骤都返回 `Result` 会让代码变成深度嵌套的 `if` 金字塔。

**失效条件**：渲染代码路径极长、或团队对错误检查的纪律性不足时。

### 分支 C：断言 + 日志回调（DEBUG 终止，Release 记录）

**核心思路**：DEBUG 模式下遇到错误立即 `assert(false)` 终止程序，方便定位第一现场。Release 模式下只记录日志并调用可注册的回调函数。

```cpp
#ifdef DEBUG
    #define RHI_CHECK(expr) \
        do { if (!(expr)) { \
            LogRHIError(#expr, __FILE__, __LINE__); \
            DebugBreak(); \
        } } while(0)
#else
    #define RHI_CHECK(expr) \
        do { if (!(expr)) { \
            LogRHIError(#expr, __FILE__, __LINE__); \
            if (g_error_callback) g_error_callback(...); \
        } } while(0)
#endif

void CreateBufferInternal(...) {
    HRESULT hr = device->CreateCommittedResource(...);
    RHI_CHECK(SUCCEEDED(hr));
}
```

**适用场景**：开发阶段追求快速定位错误、发布阶段追求不崩溃。

**隐藏代价**：
- **Release 模式静默失败**：如果 Release 模式下只是记录日志而不终止，后续使用无效资源会导致 GPU 崩溃——而 Release 模式下没有 Debug Layer 帮你定位根因。
- **无法优雅恢复**：`assert` 意味着程序终止，没有给上层"重试"或"fallback"的机会。例如纹理加载失败时，上层想回退到默认白色纹理，但 `assert` 直接杀死了进程。
- **回调注册是全局状态**：`g_error_callback` 是全局变量，在多线程场景下需要锁保护，且不同模块可能互相覆盖回调。

**失效条件**：需要运行时优雅降级（如纹理加载失败回退默认资源）、或需要按模块/按资源类型设置不同错误策略时。

### 分支 D：统一错误码 + 可配置策略 + 调试标注封装

**核心思路**：RHI 内部把所有 API 错误转换为一个**统一错误码枚举**，对外提供**可配置的错误策略**（DEBUG 断言、Release 回调、或 Result 返回值），同时封装**调试标注接口**让 GPU 调试工具能识别引擎语义。

```cpp
// 1. 统一错误码
enum class RHIErrorCode {
    Success,
    InvalidArgument,      // 参数错误（映射 D3D12 E_INVALIDARG、Vulkan VK_ERROR_VALIDATION_FAILED）
    OutOfMemory,          // 显存不足
    DeviceLost,           // GPU 崩溃/TDR
    ShaderCompilationFailed,
    UnsupportedFeature,   // 当前 GPU 不支持该特性
};

// 2. 错误信息结构
struct RHIErrorInfo {
    RHIErrorCode code;
    std::string message;
    const char* file;
    int line;
};

// 3. 可配置的错误处理策略
enum class ErrorPolicy {
    Assert,     // DEBUG 断言终止
    Log,        // 记录日志继续执行
    Callback,   // 调用用户注册的回调
    Result,     // 返回 Result<T, Error>（仅对创建类 API）
};

class IRenderDevice {
public:
    virtual void SetErrorPolicy(ErrorPolicy policy) = 0;
    virtual void SetErrorCallback(std::function<void(const RHIErrorInfo&)> callback) = 0;
};

// 4. 调试标注统一接口
class ICommandContext {
public:
    virtual void PushDebugGroup(const char* name) = 0;   // 开始一个标注区域
    virtual void PopDebugGroup() = 0;                     // 结束标注区域
    virtual void SetResourceName(BufferHandle h, const char* name) = 0;
    virtual void SetResourceName(TextureHandle h, const char* name) = 0;
};
```

**错误处理的内部分支**：
- **创建类 API**（`CreateBuffer`、`CreatePipelineState`）：默认返回 `Result<T, RHIError>`，让上层必须处理失败。
- **录制类 API**（`DrawIndexed`、`SetViewport`）：默认使用 `Assert` 策略——这些操作在正确编写的渲染管线中不应该失败，如果失败说明前置状态有误，DEBUG 阶段应该立即暴露。
- **设备丢失类错误**（`DeviceLost`）：无论策略如何，一律触发 `Callback` 并进入设备恢复流程（如果引擎支持）。

**调试标注的后端映射**：

| RHI 统一接口 | D3D12 映射 | Vulkan 映射 | OpenGL 映射 |
|-------------|-----------|------------|------------|
| `PushDebugGroup(name)` | `PIXBeginEvent` / `ID3D12GraphicsCommandList::BeginEvent` | `vkCmdBeginDebugUtilsLabelEXT` | `glPushDebugGroupKHR` |
| `PopDebugGroup()` | `PIXEndEvent` / `EndEvent` | `vkCmdEndDebugUtilsLabelEXT` | `glPopDebugGroupKHR` |
| `SetResourceName(h, name)` | `ID3D12Object::SetName` | `vkSetDebugUtilsObjectNameEXT` | `glObjectLabel` |

**适用场景**：工业级引擎、需要同时支持开发调试和发布稳定、需要集成多种 GPU 调试工具。

**隐藏代价**：
- **实现复杂度**：每个后端都要把原生错误码映射到统一枚举，调试标注接口也要逐一封装。D3D12 的 `SetName` 需要 BSTR 转换，Vulkan 的 DebugUtils 需要注入扩展函数指针。
- **策略配置增加认知负担**：上层开发者需要理解不同 API 的默认策略差异（创建类 vs 录制类）。

**失效条件**：极简原型阶段，这些抽象会拖慢开发速度。阶段 1~2 可以直接用 `assert`，阶段 5 再引入统一错误层。

### 引擎对照

> 我们在解决的是「RHI 错误处理与调试标注」这个具体子问题。
>
> - **chaos** 的错误处理相对**轻量**：底层 `graphics_interface` 使用 `assert` + 日志，没有统一的错误码枚举。高层 `render/RHI` 层封装了少量的 HRESULT/VkResult 转换，但调试标注（Debug Marker）支持不完整。
> - **UE** 选择了**分支 D 的工业级完整形态**：`FRHICommandList` 提供 `PushEvent` / `PopEvent`（映射到各平台的 GPU Profiler 和调试标注），错误处理通过 `checkf`（断言）和 `UE_LOG` 组合。UE 的 D3D12 后端内置了 PIX 集成，Vulkan 后端自动注入 Validation Layer 回调。UE 没有使用 `Result<T,E>`，而是依赖断言 + 日志——这是因为 UE 的编辑器环境下，错误需要被可视化面板捕获，而不是返回给调用者。
> - **Bevy / wgpu** 的错误模型被 wgpu 锁定：wgpu 使用 Rust 的 `Result` 类型返回错误（C API 中对应 `WGPUErrorType` 回调）。Bevy 上层代码通常 `unwrap()`（Rust 的 panic），因为 wgpu 的验证层在 DEBUG 模式下已经非常严格。Bevy 的调试标注通过 `RenderPass::push_debug_group` 暴露，底层由 wgpu 映射到各平台。

### 决策分析与推荐

**默认推荐：分支 D（统一错误码 + 可配置策略 + 调试标注封装），但阶段 5 初期按简化版实现。**

理由：
1. **不同 API 的错误模型差异太大，不能留给上层处理**。如果每个调用 `CreateBuffer` 的地方都要写 `#ifdef D3D12 check HRESULT #elif VULKAN check VkResult`，跨后端移植时上层代码会碎片化。
2. **UE 的 `PushEvent` / `PopEvent` 和 PIX 深度集成证明了调试标注不是"锦上添花"，而是"生产力必需品"**。没有 Debug Group，在 PIX/RenderDoc 中你只能看到数百个匿名 Draw Call，无法区分"Shadow Pass"和"Main Pass"。
3. **分支 A（异常）在游戏行业有兼容性风险，分支 B（Result）对录制类 API 过于冗余**。分支 D 的"可配置策略"允许对不同 API 采用不同策略：创建类返回 `Result`，录制类用 `assert`，设备丢失用回调——这种灵活性是工业级引擎的必要能力。
4. **Bevy/wgpu 的 Rust `Result` 模型在 C++ 中可以用 `std::expected`（C++23）或轻量 `Result<T,E>` 模拟**，但阶段 5 初期更简单的方式是：创建类 API 返回 `Handle`，失败时返回 `InvalidHandle`，并触发日志/断言。这本质上是分支 D 的退化版本。

**简化版实现策略**：
- 定义 `RHIErrorCode` 枚举（至少包含 `Success`、`InvalidArgument`、`OutOfMemory`、`DeviceLost`、`ShaderCompilationFailed`）。
- 创建类 API（`CreateBuffer`、`CreateTexture`、`CreatePipelineState`）返回 `Handle`，失败时返回 `Handle::Invalid()` 并输出日志。
- DEBUG 模式下，所有后端启用原生 Debug Layer（D3D12 Debug Layer、Vulkan Validation Layer）。
- 实现 `PushDebugGroup` / `PopDebugGroup` / `SetResourceName` 三个统一接口，D3D12 后端用 `PIXBeginEvent` / `SetName` 实现，Vulkan 后端先留空（阶段 5 中期补全）。
- **暂不实现用户可配置的 `ErrorPolicy`**——DEBUG 统一断言，Release 统一日志。

**遗留问题**：RHI 的抽象层厚度、命令模型、内存策略、资源句柄、错误处理都已确定。现在必须回答最后一个工程问题：**第一个后端该选谁？** D3D12、Vulkan、OpenGL、WebGPU 各有优劣，选错了会让阶段 5 的验收标准（屏幕中央出现带纹理的旋转立方体）延迟数月。这是问题 7 的核心。

---
## 问题 7：第一步后端该选谁？

### 场景绑定

你已经设计好了 RHI 的抽象接口：`IRenderDevice`、`ICommandContext`、`IBuffer`、`ITexture`、`IPipelineState`。现在必须选一个后端作为**第一个实现**，让阶段 5 的验收标准（屏幕中央出现带纹理的旋转立方体）能够达成。

这个选择不是技术信仰问题，而是**工程路径问题**。选错了，你会在底层 API 的细节中陷数月，迟迟看不到画面；选对了，你能快速验证上层设计，再逐步扩展。

### 分支 A：OpenGL / OpenGL ES

**核心思路**：用 OpenGL 作为首个后端。阶段 1 的最简图形后端已经建立了 GL 上下文和清屏逻辑，可以在此基础上扩展。

**适用场景**：
- 你已经熟悉 OpenGL，阶段 1 有现成代码。
- 目标平台是桌面（Windows/Linux/macOS 通过兼容性模式）。
- 想先专注于上层渲染架构（材质系统、RenderGraph），把底层 API 复杂度降到最低。

**隐藏代价**：
- OpenGL 是**全局状态机**，与 RHI 的"命令上下文"模型天然冲突。你需要在 GL 后端内部模拟命令缓冲（如延迟执行状态变更），这增加了 GL 后端的复杂度。
- OpenGL 的**多线程支持极弱**：上下文只能绑定到一个线程，RHI 的多线程录制能力在 GL 后端上完全无法发挥。
- OpenGL 在**macOS 上已被标记为废弃**，在移动设备上性能显著低于 Metal/Vulkan。
- 未来迁移到 D3D12/Vulkan 时，OpenGL 的"驱动隐式管理"思维（如自动 Barrier、隐式资源状态跟踪）会让你养成错误的假设，迁移成本比从零开始还高。

**失效条件**：目标平台包含 macOS 或需要现代 GPU 特性（Compute Shader、Mesh Shader）。

### 分支 B：WebGPU / wgpu-native

**核心思路**：基于 WebGPU C API（通过 `wgpu-native` 或 Dawn）实现 RHI 后端。WebGPU 是一个现代、跨平台的显式 API，设计吸取了 Vulkan、D3D12、Metal 的优点，同时更安全、更易用。

**适用场景**：
- 想要**一次实现，多端运行**（Windows、macOS、Linux，甚至未来可能通过 WASM 到浏览器）。
- 希望后端本身帮你处理许多繁琐事务（Barrier 推导、描述符管理、命令缓冲验证）。
- 个人开发者精力有限，不想自己处理 D3D12 的 Fence 同步或 Vulkan 的 Validation Layer 配置。

**隐藏代价**：
- 引入外部依赖：`wgpu-native` 是 Rust 项目，C API 绑定需要链接一个不小的库（~10MB+）。
- API 形状锁定：WebGPU 的 `BindGroup`、`CommandEncoder` 概念会渗透到你的材质和命令系统设计中。
- 调试深度受限：出现渲染错误时，你面对的是"你的代码 -> wgpu -> 底层 API"三层，有时难以定位是 wgpu 的 bug、你的误用，还是底层驱动的行为。
- 性能天花板：WebGPU 为了安全和可移植性，有一些额外的验证和间接层。根据 2026 年的学术论文测量，wgpu-native 在 Vulkan 后端上的单次 dispatch 开销约为 24~36 us，比原生 Vulkan 略高，但对游戏渲染来说完全可接受。

**失效条件**：当你需要某个 WebGPU 尚未支持的尖端 Vulkan 扩展时。

### 分支 C：Vulkan

**核心思路**：直接用 Vulkan 作为首个后端。Vulkan 是最"裸露"的现代 API，能最大程度发挥硬件性能。

**适用场景**：
- 你的核心学习目标就是**深入理解 GPU 底层工作原理**。
- 目标平台主要是 Windows/Linux/Android（Vulkan 覆盖最全）。
- 不急于看到画面，愿意花数周时间搭建 Instance、Device、SwapChain、CommandPool、Fence 的初始代码。

**隐藏代价**：
- **极高的 boilerplate**：画一个三角形需要 ~1000 行 Vulkan 代码（Instance、Surface、Device、Queue、SwapChain、ImageView、RenderPass、Framebuffer、CommandPool、CommandBuffer、Fence、Semaphore、PipelineLayout、GraphicsPipeline、ShaderModule、VertexInput……）。
- **调试地狱**：Vulkan 的错误通常是"黑屏"或"驱动崩溃"，Validation Layer 虽然有帮助，但报错信息仍需要大量经验才能解读。
- **平台覆盖缺口**：macOS 需要 MoltenVK（Vulkan -> Metal 转换层），iOS 支持有限。

**失效条件**：当项目优先级是"快速迭代、验证渲染架构"而非"学习底层 API"时。

### 引擎对照

> 我们在解决的是「第一步后端选择」这个问题。
>
> - **chaos** 在 Windows 上默认使用 D3D12（因为团队有 Xbox/PS 经验），但也支持 Vulkan 和 Metal。其 RHI 设计从一开始就是多后端的，但每个后端的实现都由专门的图形程序员维护。
> - **UE** 在 Windows 上的默认搜索顺序是 D3D12 -> D3D11 -> Vulkan -> OpenGL。UE 的 D3D12 后端是最完善、性能最优的，但实现代码量超过 10 万行。
> - **Bevy** 选择 **wgpu** 作为唯一后端。Bevy 没有自己实现 D3D12/Vulkan 后端，而是完全依赖 wgpu 的跨平台能力。这个选择让 Bevy 团队能把精力集中在 ECS 和渲染管线上，而不是图形驱动的细节。

### 决策分析

**默认推荐：分支 A 的现代子集——优先 D3D12，Vulkan 作为第二后端。**

这是吸收 UE 后端选择策略的结果（UE 在 Windows 上的默认搜索顺序是 D3D12 -> D3D11 -> Vulkan -> OpenGL），也是基于现代 API 的工程现实。以下是这两个 API 的直接对比：

| 维度 | D3D12 | Vulkan |
|------|-------|--------|
| **Boilerplate 代码量** | 画三角形 ~600 行 | 画三角形 ~1000+ 行 |
| **调试体验** | PIX 业界顶尖，调试层信息清晰 | Validation Layer 信息量大但难读，黑屏/崩溃排查困难 |
| **资源状态模型** | 显式 Barrier，状态枚举直观 | Image Layout + Pipeline Barrier，概念更细碎 |
| **描述符管理** | Descriptor Heap，线性分配相对直接 | Descriptor Set / Pool / Layout 三层组合，灵活但复杂 |
| **跨平台覆盖** | Windows、Xbox | Windows、Linux、Android、macOS(MoltenVK) |
| **Shader 编译** | HLSL + DXC，工具链成熟 | GLSL/SPIR-V 或 Slang，需要额外编译器 |
| **社区/教程** | 官方文档完善，中文资料多 | Spec 全面但难啃，高质量教程相对分散 |

**为什么优先 D3D12？**

1. **复杂度更低**：D3D12 是 Microsoft 设计的"有主见的"API，它在 Vulkan 的灵活性之上做了简化。例如 D3D12 的 Root Signature 比 Vulkan 的 Pipeline Layout + Descriptor Set 组合更直观；D3D12 的资源状态转换模型比 Vulkan 的 Image Layout 少一半概念。UE 选择 D3D12 作为 Windows 默认后端，很大程度上就是因为它的"可预测性"——同样的代码在 D3D12 上行为更一致。
2. **调试是生命线**：PIX 可以让你单步跟踪每个 Draw Call 的像素历史、查看资源状态、分析 Barrier。Vulkan 的 RenderDoc 也很好，但 PIX 对 D3D12 的原生支持更深，报错信息更直接。对于独立开发者，调试工具的质量直接决定你能走多深。
3. **学会 D3D12 等于学会 80% 的 Vulkan**：这两个 API 在架构上是**高度同构**的——都有显式 Command List、PSO、Barrier、Descriptor/Root Signature、Fence 同步。如果你理解了 D3D12 的这些概念，迁移到 Vulkan 时只是"换了一套命名 + 处理更多 corner case"，而不是学习新范式。UE 的 RHI 层同时支持 D3D12 和 Vulkan，其抽象接口设计就是以 D3D12 的概念为基准映射到 Vulkan 的。

**为什么 Vulkan 必须作为第二后端？**

1. **平台覆盖**：如果你或你的朋友想在 Linux 上运行引擎，D3D12 不行（除非用 VKD3D 转换层，但那又多一层复杂度）。Vulkan 是 Windows 之外几乎所有平台的唯一现代选择。
2. **移动/Android**：D3D12 完全不覆盖移动设备。Vulkan 是 Android 的现代图形标准。
3. **验证理解**：在 D3D12 上跑通的渲染管线，移植到 Vulkan 时会强迫你处理那些 D3D12 驱动"帮你隐式处理了"的边缘情况（如精确的 Image Layout 转换、Queue Family 所有权转移）。这会让你对 GPU 编程的理解更完整。

**具体实施路径**：
- **阶段 5 初期**：只实现 **D3D12 后端**。用 D3D12 验证 RHI 接口设计，完成"带纹理旋转立方体"的验收标准。
- **阶段 5 中期**（材质系统和 RenderGraph 跑通后）：启动 **Vulkan 后端**。此时你的 RHI 抽象已经稳定，只需要把 `IRenderDevice` 的每个函数翻译为 Vulkan 等价物。
- **OpenGL/WebGPU**：优先级放到最低。除非你有明确的 Web 部署需求（选 WebGPU）或旧设备兼容需求（选 OpenGL），否则不要让它们分散你对现代 API 的注意力。

**不推荐的误区**：
- **"Vulkan 更底层，学了 Vulkan 就不用学 D3D12"**——恰恰相反，Vulkan 的复杂度有很大一部分来自"为了灵活性而暴露的琐碎细节"（如 Queue Family、Memory Type、Image Layout 的精确转换），这些细节对理解 GPU 有帮助，但对"先让画面跑出来"是阻碍。UE 的图形程序员通常同时精通两者，而不是只学一个。
- **"两个同时写，哪个先通算哪个"**——同时实现两个现代 API 后端会让你的调试复杂度翻倍（问题出在 RHI 抽象层、D3D12 实现、还是 Vulkan 实现？）。先钉死一个，跑通上层全部管线，再移植第二个。

## 设计清单：RHI 骨架

综合以上问题链的分析，以下是一份可直接落地的 RHI 设计清单。它吸收了 UE 的工业级设计思想，在 ECS 架构中重新表达。阶段 5 初期可以先实现其简化子集，但骨架方向必须与之一致。

### 接口层（引擎公共头）

```cpp
// 后端类型枚举
enum class RenderBackendType {
    D3D12,   // 优先实现：复杂度较低，调试工具顶尖
    Vulkan,  // 第二后端：跨平台覆盖最广
    // 未来可选：OpenGL（兼容性兜底）、WebGPU（Web 部署）
};

// 纯虚接口：渲染设备（GPU 资源工厂）
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    
    virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* initial_data) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* initial_data) = 0;
    virtual PipelineStateHandle CreatePipelineState(const PipelineStateDesc& desc) = 0;
    virtual ShaderHandle CreateShader(ShaderStage stage, const void* bytecode, size_t size) = 0;
    
    virtual ICommandContext* CreateCommandContext() = 0;
    virtual void Submit(ICommandContext* ctx) = 0;
    virtual void Present() = 0;
    
    virtual RenderBackendType GetBackendType() const = 0;
    virtual const GPUCapabilities& GetCapabilities() const = 0;
};

// 纯虚接口：命令上下文（录制绘制命令）
class ICommandContext {
public:
    virtual ~ICommandContext() = default;
    
    virtual void BeginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void EndRenderPass() = 0;
    
    virtual void SetViewport(float x, float y, float w, float h) = 0;
    virtual void SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    
    virtual void BindPipeline(PipelineStateHandle pso) = 0;
    virtual void BindVertexBuffer(BufferHandle buf, uint32_t slot, uint32_t offset) = 0;
    virtual void BindIndexBuffer(BufferHandle buf, uint32_t offset) = 0;
    virtual void BindUniforms(uint32_t slot, const void* data, uint32_t size) = 0;
    virtual void BindTexture(uint32_t slot, TextureHandle tex) = 0;
    
    virtual void DrawIndexed(uint32_t index_count, uint32_t start_index, int32_t base_vertex) = 0;
    virtual void Draw(uint32_t vertex_count, uint32_t start_vertex) = 0;
    
    virtual void CopyBufferToTexture(BufferHandle src, TextureHandle dst) = 0;
    virtual void ResourceBarrier(const BarrierDesc* barriers, uint32_t count) = 0;
};
```

### 命令内存管理

- 每帧预分配 4~16 MB 连续命令内存（`std::vector<uint8_t>` 或 `std::unique_ptr<uint8_t[]>`）。
- 使用 `LinearAllocator`（原子偏移量 `fetch_add`）分配命令，O(1) 且无锁。
- 命令格式：`(RenderCmdHeader | 8 字节) + (对齐后的 Payload | 变长)`，顺序存储。
- 帧末 `Reset()` 一次性回收，不逐条释放。暂不使用 Ring Buffer，按最坏情况预分配即可。

### 资源管理

- 所有 GPU 资源通过 **Typed Handle + Generational Index** 引用（`Handle<BufferTag>`、`Handle<TextureTag>` 等）。
- 后端内部维护每资源类型的 `ResourceTable<T, Tag>`（密集数组 + 空闲列表），`Resolve(handle)` 提供 O(1) 映射。
- `Release(handle)` 时世代号递增，旧 Handle 永久失效（可检测 use-after-free）。
- 引用计数归零后资源进入**延迟删除队列**，每帧末检查 GPU Fence 释放已完成帧的资源。
- PSO 使用 `HashMap<PipelineStateDesc, PipelineStateHandle>` 全局缓存，`GetOrCreate()` 接口。

### 错误处理与调试（简化版）

- 定义统一错误码枚举 `RHIErrorCode`（`Success`、`InvalidArgument`、`OutOfMemory`、`DeviceLost`、`ShaderCompilationFailed`）。
- 创建类 API（`CreateBuffer`、`CreatePipelineState`）失败时返回 `Handle::Invalid()` 并输出日志；DEBUG 模式断言终止。
- 录制类 API（`DrawIndexed`、`SetViewport`）失败时 DEBUG 模式断言——正确编写的管线中不应失败。
- DEBUG 模式下启用原生 Debug Layer（D3D12 Debug Layer、Vulkan Validation Layer）。
- 封装 `PushDebugGroup(name)` / `PopDebugGroup()` / `SetResourceName(handle, name)`，D3D12 后端用 PIX 事件实现，Vulkan 后端预留扩展注入点。

### ECS 映射

- `RenderDevice`、`RenderQueue`、`SwapChain` 作为 ECS `Resource` 存入 World。
- `MeshRenderSystem` 通过 System 签名声明对 `RenderDevice` 和 `FrameContext` 的依赖。
- 渲染命令的录制在 System 执行期间完成，`Present` 在帧末由专门的 `PresentSystem` 完成。
- **暂不实现双 World**。所有渲染相关组件（`Mesh`、`Material`、`Camera`）直接存在于主 World，`MeshRenderSystem` 遍历它们并生成命令。

### 多线程策略（预留）

- **阶段 5 初期**：单线程录制。`ICommandContext` 的调用直接翻译为底层 API。
- **未来扩展点**：引入 `DrawPacket` 数组，多线程并行填充，单线程排序后批量提交。预留 `RenderCommandBuffer` 接口但不实现。

### AI 友好检查

| 检查项 | 实现状态 |
|--------|---------|
| 状态平铺 | `RenderDevice`/`FrameContext` 是 Render World Resource；`RenderMesh`/`RenderMaterial` 是 Render World Component；主 World 不持有 GPU 状态 |
| 自描述 | 所有 Resource/Component 类型通过阶段 4.4 的反射系统注册，AI 可读 Schema |
| 确定性 | Extract 阶段给定相同主 World 状态生成相同 Render World 状态；命令序列由 Render World 的 System 调度图确定 |
| 工具边界 | AI 通过 MCP 修改主 World 的 `Material` 组件（Extract 后影响渲染）或 Render World 的 `RenderSettings` Resource，不直接操作 `CommandContext` |
| Agent 安全 | 渲染系统的组件操作受 ECS 事务保护；Extract 的单向只读语义防止主 World 被渲染副作用污染 |
| 快照/回放 | Render World 是主 World 的确定性投影，快照 Render World 即可完整复现一帧渲染输入 |

> **下一步**：[[多线程命令录制与并行渲染]]，因为命令模型确定后，单线程录制在 Draw Call > 2000 时必然成为 CPU 瓶颈。必须引入多线程并行才能释放现代多核 CPU 的潜力。
