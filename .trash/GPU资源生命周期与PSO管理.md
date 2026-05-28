---
title: GPU 资源生命周期与 PSO 管理
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - gpu-resources
  - pso
  - lifecycle
aliases:
  - GPU 资源生命周期与 PSO 管理
---

> **前置依赖**：[[RHI抽象层与命令模型]]
> **本模块增量**：掌握 GPU 资源的安全释放策略（避免 CPU-GPU race condition），理解管线状态对象（PSO）的缓存与异步编译机制。你将能够设计一个无资源泄漏、无运行时卡顿的 GPU 资源管理层。
>
> 本笔记探讨的核心问题是：**CPU 和 GPU 是异步执行的，CPU 怎么知道 GPU 已经用完了某个资源？PSO 编译卡顿如何消除？**


## 问题 4：GPU 资源如何安全释放？

### 场景与根因

假设你有一扇门，玩家打开后，门上的动态光效纹理不再需要了。你在 CPU 侧删除了这个纹理对象。但 GPU 可能还在上一帧的渲染中读取它——光效 Pass 可能有一两个 Draw Call 还在 GPU 管线中排队。如果 CPU 立即 `free()` 这块显存，轻则画面闪烁（读到脏数据），重则驱动崩溃（访问已释放的资源）。

这不是 bug，而是**CPU-GPU 异步执行的固有特性**。CPU 发出绘制命令后不会等待 GPU 完成，而是继续准备下一帧。两者的时间差通常是 1~3 帧。

### 分支 A：引用计数 + 延迟删除队列

**核心思路**：每个 GPU 资源（纹理、缓冲、管线状态）维护一个引用计数。当引用计数归零时，不立即释放，而是放入一个**延迟删除队列**。队列中的资源在确认 GPU 已完成所有引用它的帧后，才被真正释放。

```cpp
class GPUResource {
    std::atomic<uint32_t> ref_count{1};
    bool marked_for_delete = false;
    
public:
    void AddRef() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    
    void Release() {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 引用归零，但先不 delete！
            DelayedDeleteQueue::Get().Enqueue(this);
        }
    }
    
    static void ProcessDeletes(uint64_t safe_frame) {
        // 只有 GPU 完全处理完 safe_frame 之前的所有命令后，
        // 才释放标记为删除且帧号 <= safe_frame 的资源
        for (auto* res : pending_deletes) {
            if (res->delete_frame <= safe_frame) {
                ActuallyDelete(res);
            }
        }
    }
};
```

如何知道"GPU 完成了哪一帧"？通过 **Fence**（或 SwapChain 的帧索引）。每提交一帧命令时，向 GPU 插入一个 Fence 信号。CPU 端定期查询 Fence 值，得知"GPU 已执行到第 N 帧"。所有在第 N 帧之前标记删除的资源，现在可以安全释放了。

**适用场景**：所有需要跨帧存在的 GPU 资源，尤其是纹理、缓冲、渲染目标。

**隐藏代价**：
- **内存膨胀**：如果资源删除很频繁（如每帧创建和销毁大量临时缓冲），延迟队列可能积累大量待删除资源，导致显存峰值比实际需要高 20%~50%。
- **释放延迟不可控**：最坏情况下，资源被删除后 2~3 帧才真正释放。对于显存紧张的平台（移动设备），这可能引发 OOM。
- **Fence 查询开销**：每帧查询 GPU Fence 有轻微 CPU 开销。

**失效条件**：当资源生命周期极短（只存在于一帧内）时，延迟删除的 2~3 帧延迟太浪费了。

### 分支 B：帧环缓冲区（Frame Ring Buffer / Ring Allocator）

**核心思路**：对于**只存在一帧**的资源（如每帧更新的 uniform 缓冲、动态顶点数据），不使用延迟删除队列，而是从一个环形的帧缓冲区中分配。每帧切换到一个新的缓冲段，GPU 完成一帧后，对应的段自动可回收，无需引用计数。

```cpp
class FrameRingBuffer {
    static constexpr uint32_t kFrameCount = 3;  // Triple buffering
    
    struct FrameSegment {
        uint8_t* cpu_base;
        uint64_t gpu_addr;
        uint32_t offset;
        uint32_t capacity;
        FenceValue fence;  // 标记本段被哪一帧使用
    };
    
    FrameSegment segments[kFrameCount];
    uint32_t current_frame = 0;
    
public:
    void* Alloc(uint32_t size, uint32_t alignment) {
        auto& seg = segments[current_frame % kFrameCount];
        // 等待本段对应的 GPU 帧完成（如果还没完成）
        WaitForFence(seg.fence);
        
        uint32_t aligned = AlignUp(seg.offset, alignment);
        if (aligned + size > seg.capacity) {
            // 段溢出——应调整容量或回退到堆分配
            return FallbackAlloc(size);
        }
        seg.offset = aligned + size;
        return seg.cpu_base + aligned;
    }
    
    void NextFrame() {
        current_frame++;
    }
};
```

**适用场景**：每帧更新的 uniform buffer、动态顶点/索引数据、临时 staging buffer（CPU -> GPU 上传用）。

**隐藏代价**：
- 需要预估每帧这类资源的总用量，并预分配足够大的环缓冲。预估不准会导致回退到普通堆分配，失去性能优势。
- 不适合生命周期跨帧的资源（如纹理资产、静态网格缓冲）。

**失效条件**：当资源生命周期不规则（有时一帧、有时十帧）时，环缓冲无法统一管理。

### 分支 C：显式所有权转移 + 手动同步

**核心思路**：完全由上层代码管理资源生命周期。删除资源前，上层必须显式调用 `WaitForGPU()` 或 `FenceSync()` 确保 GPU 空闲。

```cpp
void UnloadTexture(TextureHandle tex) {
    rhi->WaitForIdle();  // 阻塞直到 GPU 完成所有已提交命令
    rhi->DestroyTexture(tex);
}
```

**适用场景**：编辑器场景（用户删除一个资产时可以接受短暂卡顿）、关卡切换时批量卸载。

**隐藏代价**：
- `WaitForIdle()` 是 CPU-GPU 完全同步点，会摧毁流水线并行性，导致严重的帧时间尖刺（hitch）。绝不能在运行时频繁调用。

**失效条件**：运行时动态释放资源的场景。

### 引擎对照

> 我们在解决的是「GPU 资源安全释放」这个问题。
>
> - **chaos** 使用 `TRefCountPtr`（类似 `std::shared_ptr`）管理资源引用计数，资源对象继承自统一的基类，释放由后端内部处理。从源码推断，它采用了**分支 A（延迟删除）** 的策略，因为上层很少显式 `WaitForIdle`。
> - **UE** 的 `FRHIResource` 是**分支 A 的工业级实现**：引用计数打包在 32 位原子整数中（30 位计数 + 2 位状态标志），`Release()` 归零后标记为延迟删除，由 `FRHICommandListExecutor` 在帧边界批量清理。同时 UE 也大量使用**分支 B**：`FMemStackBase` 和 Uniform Buffer 的环分配是渲染线程的常见模式。
> - **Bevy/wgpu** 的资源释放依赖 Rust 的所有权系统 + wgpu 的内部引用计数。wgpu 会在内部处理 GPU 同步，开发者通常无需关心。但 wgpu 也提供了显式的 `Buffer::destroy()`，在调用时不会立即释放底层资源，而是排队等待 GPU 完成——本质仍是**分支 A**，由库内部封装。

### 决策分析

**默认推荐：分支 A 的 UE 式精细化实现——引用计数 + 延迟删除队列，吸收 UE 的"引用计数打包"和"RHI 线程帧边界清理"设计。**

理由：
1. **UE 的 `FRHIResource` 是分支 A 的工业级巅峰**。它把引用计数打包在 32 位原子整数中（30 位计数 + 2 位状态标志），`Release()` 归零后标记为延迟删除，由 `FRHICommandListExecutor` 在帧边界批量清理。这不是"过度设计"，而是经过无数项目验证的**最小正确实现**——它用 32 位原子操作就解决了"多线程 AddRef/Release 竞争 + 延迟删除 + 帧安全"三个问题。
2. **分支 A 与 ECS 天然兼容**。GPU 资源可以包装为 ECS `Resource`，引用计数是 Resource 的内部状态，延迟删除队列是另一个 `Resource`。System 不直接操作裸指针，通过 `Handle<T>` 访问，这与 ECS 的"组件/资源是平铺数据"原则一致。
3. **分支 B（帧环缓冲）是性能优化，不是替代方案**。UE 同样大量使用帧环缓冲（`FMemStackBase`、Uniform Buffer 环分配），但它与分支 A 是**互补关系**：环缓冲管"每帧临时数据"，延迟删除管"跨帧持久资源"。阶段 5 初期可以只用分支 A，但架构上要预留环缓冲的接口。
4. **分支 C（显式同步）在运行时路径中是错误选择**。`WaitForIdle()` 会摧毁 CPU-GPU 流水线并行性。UE 只有在编辑器操作（如用户点击删除资产）或关卡切换时才会使用类似机制。

**具体实现建议（吸收 UE 设计）**：
- **引用计数打包**：每个 GPU 资源内部用一个 `std::atomic<uint32_t>` 存储引用计数。借鉴 UE 的做法，用低 30 位存计数，高 2 位存状态标志（如 `kPendingDelete`、`kDeferredDelete`）。`AddRef()` 和 `Release()` 都是无锁原子操作。
- **延迟删除队列**：不是"全局单例队列"，而是**RHI 层内部的帧边界清理机制**。每帧的 `RHISubmitSystem` 在提交命令后，把当帧的 Fence 值记录到队列中。下一帧（或几帧后）的 `RHISubmitSystem` 检查 Fence，确认 GPU 已完成后批量释放。
- **Handle 安全层**：`Handle<Texture>` 对上层暴露，内部持有 `GPUResource*` + 代际标记（generation）。如果资源已被延迟删除，Handle 的代际标记不匹配，访问时返回安全错误（而非悬空指针）。这是 UE 的 `FRHIResource` 句柄安全思想的简化版。
- **Staging Ring Buffer 预留**：为 Uniform Buffer 和动态顶点数据预留 `FrameRingBuffer` 接口，但阶段 5 初期可以用简单的"每帧 new/delete"或分支 A 的延迟删除代替。等 Profiling 确认内存分配是瓶颈时，再替换为环缓冲实现。

**遗留问题**：资源创建本身也可能很慢——尤其是现代 API 的管线状态对象（PSO），在 D3D12/Vulkan 下编译一个 PSO 可能耗时数毫秒，首次使用时直接造成卡顿。如何管理 PSO 的创建与缓存？这是问题 5 的核心。



## 问题 5：管线状态对象（PSO）如何管理？

### 场景与根因

在传统 OpenGL 中，绘制前你需要分别设置各种状态：着色器程序、混合模式、深度测试、剔除面、顶点布局……这些状态是**细粒度、可独立变更**的。你可以在 Draw Call A 前开深度测试，Draw Call B 前关深度测试，驱动会在后台帮你排序和优化。

但 D3D12 和 Vulkan 要求你把"着色器 + 所有固定功能状态"预先打包成一个**不可变的管线状态对象（PSO）**。绘制时只能绑定整个 PSO，不能单独改其中某一项。PSO 的首次创建需要驱动**编译**管线——把着色器字节码与状态配置结合，生成 GPU 可直接执行的微码。这个过程可能耗时 **1~10 毫秒**。

**深层矛盾**：你想要渲染的每个材质组合（不同着色器 x 不同混合模式 x 不同深度模式……）都可能需要一个独立的 PSO。如果一帧内有 100 个材质变体，首次运行时创建全部 PSO 会导致**数十次卡顿**。但如果不预先创建，运行时碰到未缓存的 PSO 就会 hitch。

### 分支 A：运行时按需创建 + 全局哈希缓存

**核心思路**：第一次用到某个 PSO 配置时，调用底层 API 创建它，并用哈希表缓存。后续相同配置直接命中缓存。

```cpp
struct PipelineStateDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    VertexLayoutHandle vertex_layout;
    BlendState blend;
    DepthStencilState depth;
    RasterizerState raster;
    RenderTargetFormat rt_formats[8];
    
    bool operator==(const PipelineStateDesc& other) const { /* 逐字段比较 */ }
};

namespace std {
    template<> struct hash<PipelineStateDesc> {
        size_t operator()(const PipelineStateDesc& d) const {
            // 组合哈希所有字段
            size_t h = HashCombine(d.vertex_shader.id, d.fragment_shader.id);
            h = HashCombine(h, d.vertex_layout.id);
            // ...
            return h;
        }
    };
}

class PSOManager {
    HashMap<PipelineStateDesc, PipelineStateHandle> cache;
    
public:
    PipelineStateHandle GetOrCreate(const PipelineStateDesc& desc) {
        auto it = cache.Find(desc);
        if (it != cache.End()) return it->value;
        
        // 缓存未命中：调用后端创建 PSO
        auto handle = backend->CreatePipelineState(desc);
        cache.Insert(desc, handle);
        return handle;
    }
};
```

**适用场景**：所有现代 API 后端（D3D12、Vulkan、Metal）。

**隐藏代价**：
- **首次命中卡顿**：缓存未命中时的 `CreatePipelineState` 是同步阻塞的，会造成帧时间尖刺。玩家在场景中走动，突然看到新材质时卡一下——体验极差。
- **缓存膨胀**：如果材质关键字组合爆炸（如 UE 的材质系统有数千个 shader permutation），PSO 缓存可能占用数百 MB 内存，甚至拖慢哈希查找。
- **哈希冲突风险**：`PipelineStateDesc` 包含很多字段，哈希组合设计不好会导致意外冲突（两个不同的 desc 映射到同一个 PSO，渲染错误极难调试）。

**失效条件**：当 PSO 数量极大（> 10000）时，哈希表的内存占用和查找开销变得不可忽视。

### 分支 B：异步编译 + 占位回退

**核心思路**：缓存未命中时，不立即编译 PSO，而是启动一个后台线程异步编译，同时用一个"近似 PSO"（如关闭某些特性的简化版本）或上一帧的缓存结果继续渲染。后台编译完成后，下帧切换到正式 PSO。

```cpp
PipelineStateHandle PSOManager::GetOrCreateAsync(const PipelineStateDesc& desc) {
    auto it = cache.Find(desc);
    if (it != cache.End()) return it->value;
    
    auto pending = async_compile_jobs.Find(desc);
    if (pending != async_compile_jobs.End()) {
        // 正在后台编译，返回占位 PSO
        return pending->placeholder_pso;
    }
    
    // 启动异步编译
    AsyncCompileJob job;
    job.desc = desc;
    job.placeholder_pso = CreatePlaceholderPSO(desc);
    job.future = thread_pool->Submit([desc]() {
        return backend->CreatePipelineState(desc);
    });
    async_compile_jobs.Insert(desc, job);
    return job.placeholder_pso;
}
```

**适用场景**：大型引擎、材质系统复杂、玩家对偶尔的画质降级可容忍（如远处的物体先用简化管线）。

**隐藏代价**：
- 实现复杂：需要管理异步任务的生命周期、处理编译失败、在正确时机切换到正式 PSO。
- 占位 PSO 的设计本身是个难题：用什么回退？如果回退是"不渲染"，玩家会看到物体闪烁出现；如果回退是错误的光照模型，画面会看起来怪异。
- 对于小型项目或快速原型，异步线程池 + 任务同步的工程负担可能超过即时收益。

**失效条件**：当没有合理的占位方案时（如 PSO 差异极大，不存在"简化版"）。

### 分支 C：离线预编译 + 管线缓存文件

**核心思路**：在游戏打包阶段，预先遍历所有可能用到的材质组合，生成全部 PSO，并将它们序列化到缓存文件（D3D12 的 *.pSO、Vulkan 的 VkPipelineCache 数据）。运行时直接加载缓存，无需编译。

**适用场景**：商业级项目、材质组合在开发期已完全确定、打包时间长可接受。

**隐藏代价**：
- 需要专门的工具链遍历所有材质和场景，收集 PSO 描述。
- 缓存文件与驱动版本强相关：GPU 驱动升级后，离线缓存可能失效，需要重新生成。
- 对于快速迭代开发，维护预编译工具链的成本可能超过收益。

**失效条件**：开发期材质组合频繁变化、没有 CI/CD 自动化工具链时。

### 引擎对照

> 我们在解决的是「PSO 管理与缓存」这个问题。
>
> - **chaos** 使用 `PSOManager` + `ConcurrentHashMapWithPool` 做全局 PSO/SRB 缓存，`PSOSRBKey` 将完整描述哈希为 64/128 位键。从源码看是**分支 A** 的实现，源码注释未明确提及 LRU 驱逐（可能存在缓存膨胀风险）。
> - **UE** 同样使用全局哈希缓存（`PipelineStateCache`），但增加了**异步编译**支持。UE 的材质系统会在烘焙时预编译大量 PSO（`ShaderCompileWorker`），运行时命中缓存。对于未命中的情况，UE 有复杂的异步管线编译系统，避免阻塞渲染线程。这是**分支 A + B + C 的工业级组合**。
> - **Bevy/wgpu** 的 PSO 管理主要由 wgpu 内部处理。wgpu 会自动缓存管线，并提供 `PipelineCache` 对象支持序列化/反序列化缓存数据。Bevy 在 `RenderDevice` 初始化时会尝试加载之前的管线缓存文件——相当于**分支 A + C**，但开发者几乎不感知。

### 决策分析

**默认推荐：分支 A + B 的 ECS 化组合——运行时全局哈希缓存 + 异步编译架构预留，即使初期只有同步实现。**

理由：
1. **UE 的 PSO 管理是分支 A + B + C 的工业级组合，但核心骨架是"全局缓存 + 异步编译"**。UE 的 `PipelineStateCache` 用全局哈希缓存避免重复编译，`ShaderCompileWorker` 在后台异步编译未命中项，烘焙时预编译大量 PSO（分支 C）。这个组合不是"大型引擎才需要"，而是**任何需要流畅运行时体验的引擎都需要的正确架构**。
2. **分支 A 单独使用在运行时会导致卡顿**。即使你的 PSO 数量只有 50 个，如果玩家走进一个新区域时突然编译 3 个新 PSO，那就是 3~30ms 的 hitch。帧时间从 16ms 跳到 46ms，玩家会明显感到卡顿。
3. **分支 B（异步编译）的复杂度被高估了**。最简单的异步编译实现只需要一个线程池 + 一个"pending PSO"队列：`GetOrCreate()` 缓存未命中时，启动后台线程编译，同时返回一个**占位 PSO**（如最简单的纯色着色器 PSO，或最近似状态的已缓存 PSO）。后台完成后，下帧自动切换。这个"最小可行异步编译"不超过 300 行代码，但彻底消除了运行时 hitch。
4. **分支 C（离线预编译）可以后期补**。阶段 5 初期不需要预编译工具链，但 `PSOManager` 的接口应预留序列化/反序列化能力（如 `SaveCache()` / `LoadCache()`）。当材质系统（阶段 5.3）完善后，可以添加烘焙工具遍历所有材质组合生成缓存。

**具体实现建议（吸收 UE 设计）**：
- **PSO 描述哈希**：`PipelineStateDesc` 必须包含所有影响 PSO 的字段（着色器、顶点布局、混合/深度/光栅状态、RT 格式）。哈希函数用稳定的 `HashCombine` 逐字段组合。**必须处理哈希冲突**——在哈希桶内用线性探测或链式存储 `Desc -> Handle` 映射，查找时逐字段比较确认。
- **全局缓存线程安全**：`PSOManager` 用读写锁（`shared_mutex`）保护。`GetOrCreate()` 先读锁查缓存，未命中时释放读锁、获取写锁、二次检查（防止竞争）、创建 PSO。这与 UE 的 `PipelineStateCache` 锁策略一致。
- **异步编译骨架（阶段 5 初期必须预留）**：
  - 定义 `AsyncPSOJob { PipelineStateDesc desc; std::future<PipelineStateHandle> result; PipelineStateHandle placeholder; }`。
  - `GetOrCreate()` 未命中时，检查 `pending_jobs`，如果已在编译中返回 `placeholder`；否则启动后台线程编译，把 job 加入 `pending_jobs`。
  - 每帧初，`PSOManager::Update()` 检查完成的 job，把结果写入缓存，通知使用该 placeholder 的渲染对象切换。
  - **阶段 5 初期的简化**：如果工程负担重，`placeholder` 可以直接返回一个预创建的"错误粉色"PSO（用于调试可见），或返回最近似的已缓存 PSO。
- **调试可观测性**：记录"PSO 缓存命中率"、"编译耗时"、"异步 job 队列长度"到日志和性能分析面板。这是 UE 的 Shader Profiler 思想的简化版。

**遗留问题**：现在我们已经有了命令录制模型、资源生命周期管理、PSO 缓存。但还有一个架构层面的问题：ECS 引擎中，这些 RHI 的"全局状态"（Device、Context、Queue）应该如何表达？传统的单例模式与 ECS 的"无全局状态"原则是冲突的。这是问题 6 的核心。

> **下一步**：[[ECS架构下的渲染世界设计]]，因为 RHI 层的接口和资源管理已经就绪，下一个问题是：在 ECS 架构下，这些"全局状态"（Device、Queue、资源池）应该如何表达？逻辑线程和渲染线程如何安全并行？
