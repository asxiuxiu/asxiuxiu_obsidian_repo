---
title: PSO 缓存与异步编译
date: 2026-05-28
tags:
  - self-game-engine
  - rendering
  - pso
  - pipeline-state-object
  - shader-compilation
  - async
aliases:
  - PSO 缓存与异步编译
---

> **前置依赖**：[[RHI抽象层与命令模型]]、[[GPU资源生命周期管理]]
> **本模块增量**：掌握管线状态对象的缓存策略、异步编译架构、磁盘持久化与工业级预编译方案。你将能够设计一个在运行时无卡顿、启动时快速预热、且可自动收集缺失 PSO 的工业级管线管理系统。
>
> 本笔记探讨的核心问题是：**D3D12/Vulkan 要求预编译 PSO，但编译耗时 1~10 毫秒。如何在首次遇到新材质时不卡顿？如何在加载阶段预先编译所有可能用到的 PSO？**


## 问题 0：为什么 PSO 编译会造成卡顿？

### 场景与根因

你在游戏中奔跑，穿过一片草地进入一座石质建筑。就在跨过门槛的瞬间，画面突然冻结了 50 毫秒——帧率从 60 fps 掉到 20 fps，玩家明显感到"卡了一下"。Profiler 显示这 50 毫秒全部消耗在 `CreateGraphicsPipelineState` 调用上。

**根因分析**：在传统 OpenGL 中，绘制前你需要分别设置各种状态：着色器程序、混合模式、深度测试、剔除面、顶点布局……这些状态是**细粒度、可独立变更**的。你可以在 Draw Call A 前开深度测试，Draw Call B 前关深度测试，驱动会在后台帮你排序和优化。

但 D3D12 和 Vulkan 要求你把"着色器 + 所有固定功能状态"预先打包成一个**不可变的管线状态对象（PSO）**。绘制时只能绑定整个 PSO，不能单独改其中某一项。PSO 的首次创建需要驱动**编译**管线——把着色器字节码与状态配置结合，生成 GPU 可直接执行的微码。这个过程可能耗时 **1~10 毫秒**，复杂管线甚至超过 50 毫秒。

**深层矛盾**：你想要渲染的每个材质组合（不同着色器 × 不同混合模式 × 不同深度模式 × 不同顶点布局 × 不同 RenderTarget 格式……）都可能需要一个独立的 PSO。如果一帧内有 100 个材质变体，首次运行时创建全部 PSO 会导致**数十次卡顿**。但如果不预先创建，运行时碰到未缓存的 PSO 就会 hitch。

```
CPU Timeline:
  帧N:   渲染草地 ──► 走入建筑 ──► 需要石质材质 PSO
                           │
                           ▼
                      cache miss!
                           │
                           ▼
                      CreatePipelineState() ────────► 阻塞 8ms
                           │
                           ▼
                      帧时间从 16ms 跳到 24ms ──► 玩家感到卡顿
```

**结论**：PSO 编译卡顿不是"优化问题"，而是现代显式 API 的**结构性问题**。任何严肃的商业引擎都必须有系统性的 PSO 管理策略，否则玩家的首次体验会被频繁的 hitch 摧毁。

> **遗留问题**：最简单的想法是"用到时才创建，但创建一次后缓存起来复用"。这是可行的第一步吗？这是问题 1 的起点。


## 问题 1：运行时按需创建 + 全局哈希缓存

### 场景与根因

你决定采用最直观的策略：第一次用到某个 PSO 配置时，调用底层 API 创建它，并用哈希表缓存。后续相同配置直接命中缓存。这个策略能工作吗？

**根因**：PSO 描述（PipelineStateDesc）是一个高维组合空间。它包含：顶点着色器、像素着色器、几何/网格着色器（可选）、顶点输入布局、混合状态、深度模板状态、光栅化状态、RenderTarget 格式数组、多重采样设置、管线布局（Root Signature / Pipeline Layout）……两个 PSO 只要有一个字段不同，通常就需要独立的编译结果。

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
    uint32_t rt_count;
    
    bool operator==(const PipelineStateDesc& other) const { /* 逐字段比较 */ }
};

namespace std {
    template<> struct hash<PipelineStateDesc> {
        size_t operator()(const PipelineStateDesc& d) const {
            size_t h = HashCombine(d.vertex_shader.id, d.fragment_shader.id);
            h = HashCombine(h, d.vertex_layout.id);
            h = HashCombine(h, std::hash<BlendState>{}(d.blend));
            // ... 逐字段组合哈希
            return h;
        }
    };
}

class PSOManager {
    HashMap<PipelineStateDesc, PipelineStateHandle> cache;
    std::shared_mutex cache_mutex;  // 读写锁
    
public:
    PipelineStateHandle GetOrCreate(const PipelineStateDesc& desc) {
        // 1. 读锁查缓存
        {
            std::shared_lock read_lock(cache_mutex);
            auto it = cache.Find(desc);
            if (it != cache.End()) return it->value;
        }
        
        // 2. 未命中：写锁创建（二次检查防止竞争）
        std::unique_lock write_lock(cache_mutex);
        auto it = cache.Find(desc);  // 二次检查
        if (it != cache.End()) return it->value;
        
        // 3. 调用后端创建 PSO（可能阻塞数毫秒！）
        auto handle = backend->CreatePipelineState(desc);
        cache.Insert(desc, handle);
        return handle;
    }
};
```

**适用场景**：所有现代 API 后端（D3D12、Vulkan、Metal）。

**隐藏代价**：
- **首次命中卡顿**：缓存未命中时的 `CreatePipelineState` 是同步阻塞的。玩家在场景中走动，突然看到新材质时卡一下——体验极差。这是问题 0 的核心矛盾，分支 A 单独使用时**没有解决**它，只是让"只卡一次"变成了"每次启动都卡一次"。
- **缓存膨胀**：如果材质关键字组合爆炸（如 UE 的材质系统有数千个 shader permutation），PSO 缓存可能占用数百 MB 内存，甚至拖慢哈希查找。
- **哈希冲突风险**：`PipelineStateDesc` 包含很多字段，哈希组合设计不好会导致意外冲突（两个不同的 desc 映射到同一个 PSO，渲染错误极难调试）。**必须在哈希桶内做逐字段精确比较确认**。

**失效条件**：当 PSO 数量极大（> 10000）时，哈希表的内存占用和查找开销变得不可忽视。

### 分支 B：排序键去重 + 命令缓冲期延迟创建

**核心思路**：不在绑定 PSO 的瞬间创建，而是在命令缓冲录制阶段收集所有需要的 PSO，在帧末统一创建。这样可以把一帧内的多次 cache miss 合并为一批创建，减少 hitch 次数。

```cpp
class DeferredPSOManager {
    HashMap<PipelineStateDesc, PipelineStateHandle> cache;
    std::vector<PipelineStateDesc> pending_creates;  // 本帧新发现的 PSO
    
public:
    PipelineStateHandle Request(const PipelineStateDesc& desc) {
        auto it = cache.Find(desc);
        if (it != cache.End()) return it->value;
        
        // 不立即创建，只记录需求
        pending_creates.push_back(desc);
        return kPlaceholderPSO;  // 返回占位 PSO
    }
    
    void EndFrame() {
        // 统一创建本帧所有缺失的 PSO
        for (auto& desc : pending_creates) {
            if (!cache.Contains(desc)) {
                auto handle = backend->CreatePipelineState(desc);
                cache.Insert(desc, handle);
            }
        }
        pending_creates.clear();
    }
};
```

**适用场景**：后处理栈、UI 渲染等 PSO 集合在帧内可预测的场景。

**隐藏代价**：
- 如果本帧有 20 个新 PSO，`EndFrame()` 会一次性阻塞 20~100 毫秒，比逐个 hitch 更严重。
- 返回 `kPlaceholderPSO` 意味着本帧可能用错误的管线渲染，画面会出现一闪而过的错误效果。

**失效条件**：运行时动态发现 PSO 的场景（如开放世界流式加载新材质）。

### 引擎对照

> 我们在解决的是「PSO 运行时缓存」这个问题。
>
> - **chaos** 使用 `PSOManager` + `ConcurrentHashMapWithPool` 做全局 PSO/SRB 缓存，`PSOSRBKey` 将完整描述哈希为 64/128 位键。从源码看是**分支 A** 的实现，源码注释未明确提及 LRU 驱逐（可能存在缓存膨胀风险）。
> - **UE** 同样使用全局哈希缓存（`PipelineStateCache`），但增加了**异步编译**支持。UE 的材质系统会在烘焙时预编译大量 PSO（`ShaderCompileWorker`），运行时命中缓存。对于未命中的情况，UE 有复杂的异步管线编译系统，避免阻塞渲染线程。这是**分支 A + 异步的工业级组合**。
> - **Bevy/wgpu** 的 PSO 管理主要由 wgpu 内部处理。wgpu 会自动缓存管线，并提供 `PipelineCache` 对象支持序列化/反序列化缓存数据。Bevy 在 `RenderDevice` 初始化时会尝试加载之前的管线缓存文件——相当于**分支 A + 磁盘缓存**。

### 决策分析

**默认推荐：分支 A 作为 PSO 管理的必要基础层，但绝不单独使用——必须与问题 2 的异步编译结合。**

理由：
1. **全局哈希缓存是任何 PSO 管理的最低要求**。没有缓存，相同 PSO 每次绑定都重新编译，性能直接崩溃。
2. **分支 A 单独使用在运行时必然导致 hitch**。即使 PSO 数量只有 50 个，如果玩家走进一个新区域时突然编译 3 个新 PSO，那就是 3~30 ms 的卡顿。帧时间从 16 ms 跳到 46 ms，玩家会明显感到卡顿。
3. **哈希冲突必须被严肃对待**。简单的 `std::hash` 组合在 `PipelineStateDesc` 这种高维结构上不够可靠。推荐采用稳定的 64 位或 128 位哈希（如 FNV-1a 或 CityHash），并在哈希桶内保留完整描述用于精确比较。

**遗留问题**：分支 A 的首次命中卡顿如何消除？答案很明确：不能让渲染线程阻塞等待 PSO 编译。这是问题 2 的核心。


## 问题 2：异步编译 + 占位回退

### 场景与根因

你已经有了全局 PSO 缓存，但运行时 cache miss 仍然会造成 hitch。核心矛盾是：**PSO 编译是驱动层面的阻塞操作，而渲染线程不能等**。

**根因**：D3D12 的 `CreateGraphicsPipelineState` 和 Vulkan 的 `vkCreateGraphicsPipelines` 在内部会调用驱动编译器，将 SPIR-V/DXIL 转换为 GPU 原生 ISA。这个编译过程是 CPU 密集型的，且驱动 API 是同步的——调用线程会被阻塞直到编译完成。

### 分支 A：纯异步线程编译 + 占位 PSO

**核心思路**：缓存未命中时，不立即编译 PSO，而是启动一个后台线程异步编译，同时用一个"近似 PSO"（如关闭某些特性的简化版本）或预创建的通用 PSO 继续渲染。后台编译完成后，下帧切换到正式 PSO。

```cpp
enum class PSOCompileState {
    Ready,       // 已编译完成，可直接使用
    Compiling,   // 后台线程正在编译
    Failed,      // 编译失败
};

struct AsyncPSOJob {
    PipelineStateDesc desc;
    std::future<PipelineStateHandle> result;
    PipelineStateHandle placeholder;
    PSOCompileState state = PSOCompileState::Compiling;
};

class AsyncPSOManager {
    HashMap<PipelineStateDesc, PipelineStateHandle> ready_cache;
    HashMap<PipelineStateDesc, AsyncPSOJob> pending_jobs;
    ThreadPool compile_pool{4};  // 4 个编译线程
    
public:
    PipelineStateHandle GetOrCreate(const PipelineStateDesc& desc) {
        // 1. 检查已就绪缓存
        auto it = ready_cache.Find(desc);
        if (it != ready_cache.End()) return it->value;
        
        // 2. 检查是否已在编译中
        auto pending = pending_jobs.Find(desc);
        if (pending != pending_jobs.End()) {
            // 检查后台任务是否完成
            if (IsReady(pending->result)) {
                auto handle = pending->result.get();
                ready_cache.Insert(desc, handle);
                pending_jobs.Remove(desc);
                return handle;
            }
            return pending->placeholder;  // 返回占位 PSO
        }
        
        // 3. 启动异步编译
        AsyncPSOJob job;
        job.desc = desc;
        job.placeholder = CreatePlaceholderPSO(desc);  // 最近似的已就绪 PSO
        job.result = compile_pool.Submit([desc, this]() {
            return backend->CreatePipelineState(desc);  // 后台编译
        });
        pending_jobs.Insert(desc, std::move(job));
        return job.placeholder;
    }
    
private:
    PipelineStateHandle CreatePlaceholderPSO(const PipelineStateDesc& desc) {
        // 策略 1：返回一个预创建的"错误粉色"PSO（调试用）
        // 策略 2：找到缓存中最近似的 PSO（相同 VS，但简化 PS）
        // 策略 3：返回一个"uber shader" PSO（功能完整但性能较低）
        return FindBestApproximation(desc);
    }
};
```

**适用场景**：大型引擎、材质系统复杂、玩家对偶尔的画质降级可容忍。

**隐藏代价**：
- **占位 PSO 的设计本身是个难题**：用什么回退？如果回退是"不渲染"，玩家会看到物体闪烁出现；如果回退是错误的光照模型，画面会看起来怪异。**Uber Shader**（一个功能完整但用动态分支替代静态变体的通用着色器）是业界常用的占位方案，但它在 GPU 上的执行效率通常比专用 PSO 低 20%~50%。
- 实现复杂：需要管理异步任务的生命周期、处理编译失败、在正确时机切换到正式 PSO。
- 对于小型项目或快速原型，异步线程池 + 任务同步的工程负担可能超过即时收益。

**失效条件**：当没有合理的占位方案时（如 PSO 差异极大，不存在"简化版"）。

### 分支 B：时间片同步编译（Time-sliced Synchronous）

**核心思路**：不在后台线程编译，而是在每帧的 CPU 空闲时间内"偷"一点时间，编译 1~2 个 PSO。渲染继续使用占位方案，直到所有 PSO 都编译完成。

```cpp
void PSOManager::Update(float max_time_ms) {
    auto start = GetTime();
    while (!compile_queue.empty() && (GetTime() - start) < max_time_ms) {
        auto& desc = compile_queue.front();
        auto handle = backend->CreatePipelineState(desc);
        ready_cache.Insert(desc, handle);
        compile_queue.pop_front();
    }
}
```

**适用场景**：加载画面、主菜单、过场动画——玩家对画面静止或简化可容忍，但不能接受随机 hitch。

**隐藏代价**：
- 需要游戏设计配合：在"安全"场景（加载画面）内集中编译 PSO，而不是在运行时边走边编译。
- 如果 PSO 数量很大（数千个），即使每帧编译 1 个也需要数分钟。Unity 6 的 PSO Precooking 系统允许开发者选择"同步预编译"或"后台异步"，就是针对这种权衡。

**失效条件**：没有加载画面等"安全时间窗口"的游戏类型（如无缝开放世界）。

### 分支 C：Uber Shader + 后台特化

**核心思路**：游戏启动时只编译一个通用的 Uber Shader PSO（功能完整，但用 uniform 分支替代静态变体）。运行时所有材质先用 Uber Shader 渲染，后台线程逐步编译专用 PSO，完成后切换。

**NVIDIA 的官方推荐**（2023 年 Advanced API Performance 博客）："Start with generic PSOs with generic shaders that compile quickly and generate specializations later. This gets you up and running faster even if you are not running the most optimal PSO or shader yet."

**适用场景**：材质变体极多、无法预先知道所有组合、需要保证零 hitch。

**隐藏代价**：
- Uber Shader 的 GPU 性能通常比专用 PSO 差，因为动态分支会浪费 SIMT 执行单元。
- Uber Shader 本身的编译也可能很慢，如果它包含太多功能分支。

### 引擎对照

> 我们在解决的是「消除 PSO 编译阻塞」这个问题。
>
> - **chaos** 的源码分析未明确显示异步编译系统。从 `PSOManager::getOrCreateGraphicsPipelineState` 的同步风格推断，它可能主要依赖**分支 A 的缓存 + 离线预编译**。
> - **UE** 拥有工业级的异步编译系统。`ShaderCompileWorker` 是独立进程，负责异步编译着色器和 PSO。UE5 的 **PSO Precaching** 系统在组件加载时自动收集所需 PSO，后台线程池编译，支持代理延迟创建（Proxy Creation Delay）——如果 PSO 未就绪，可以跳过渲染或回退到默认材质。这是**分支 A + B + C 的完整组合**。
> - **Bevy/wgpu** 的异步能力受限于 wgpu 的设计。wgpu 的管线创建是同步的，但可以通过异步加载模块来提前创建。Bevy 的 `PipelineCache` 在启动时加载磁盘缓存，运行时命中率高，减少了异步编译的需求。

### 决策分析

**默认推荐：分支 A（纯异步线程编译 + 占位 PSO）作为运行时路径，分支 B（时间片同步编译）作为加载阶段路径，Uber Shader（分支 C）作为可选的零 hitch 兜底。**

理由：
1. **UE 的 PSO 管理核心是"全局缓存 + 异步编译"**。UE5 的 Precaching 系统证明了：即使对于《堡垒之夜》这种材质变体极多的项目，异步编译 + 占位回退也能实现接近零 hitch 的体验。
2. **异步编译的复杂度被高估了**。最简单的实现只需要一个线程池 + 一个"pending PSO"队列 + 一个占位 PSO。这个"最小可行异步编译"不超过 300 行代码，但彻底消除了运行时 hitch。
3. **占位 PSO 策略必须有明确推荐**：默认使用"最近似的已缓存 PSO"作为占位（例如相同 Vertex Shader 但简化 Pixel Shader）。如果连近似 PSO 都找不到，回退到预创建的"错误粉色"PSO（调试用，确保问题可见），而不是静默跳过渲染。
4. **分支 B（时间片同步）是加载阶段的必要补充**。在关卡加载画面或主菜单中，可以集中时间片快速预热 PSO，减少运行时异步编译的压力。

**具体实现建议**：
- **异步任务结构**：`AsyncPSOJob { PipelineStateDesc desc; std::future<Handle> result; Handle placeholder; std::atomic<State> state; }`。
- **线程池优先级**：PSO 编译线程应使用低优先级（Idle Priority），防止与游戏逻辑线程竞争 CPU。NVIDIA 明确建议："Use Idle priority if there is no 'hurry' to prevent slowdowns for game threads."
- **状态机**：每个 pending PSO 有明确状态机 `Pending → Compiling → Ready | Failed`。
- **失败处理**：如果 PSO 编译失败（如着色器代码有平台相关 bug），必须记录 desc 并回退到占位 PSO，不能崩溃。

**遗留问题**：异步编译消除了运行时 hitch，但每次启动游戏都要重新编译 PSO 吗？能不能把编译结果保存到磁盘，下次启动直接加载？这是问题 3 的核心。


## 问题 3：PSO 缓存持久化与跨平台 Pipeline Library

### 场景与根因

你已经实现了异步编译，但玩家每次启动游戏、进入新存档时，都要重新经历一遍 PSO 编译——即使这些 PSO 在上一局已经完全编译过了。对于 PSO 数量上千的项目，这意味着每次启动都有数分钟的"预热期"。

**根因**：PSO 编译的结果是 GPU 特定的微码（ISA），它由驱动程序生成，存储在驱动的运行时内存中。当进程退出时，这些微码就丢失了。下次启动必须重新编译。

### 分支 A：驱动自动磁盘缓存

**核心思路**：不主动管理 PSO 缓存，依赖 D3D12/Vulkan 驱动自带的磁盘缓存机制。驱动会在后台自动保存编译过的 PSO 到系统临时目录，下次启动时自动加载。

**适用场景**：开发期、PSO 数量少、不想维护额外文件的项目。

**隐藏代价**：
- **不可控**：驱动缓存的位置、格式、生命周期完全由驱动决定。驱动升级后缓存可能失效，且你无法预热新驱动上的缓存。
- **首次启动无缓存**：新玩家第一次运行游戏时，驱动缓存为空，仍然要经历完整的编译卡顿。
- **跨平台不一致**：D3D12 的驱动缓存策略与 Vulkan 完全不同，无法统一。

**失效条件**：商业级项目、对首次启动体验有要求的项目。

### 分支 B：应用层 Pipeline Cache 文件

**核心思路**：引擎自己管理 PSO 缓存文件。创建 PSO 后，调用底层 API 导出编译后的二进制 blob，序列化到应用控制的文件中（如游戏安装目录或用户文档目录）。下次启动时先加载缓存文件，未命中时才编译。

**D3D12 PSO Library**：
```cpp
// D3D12 的 PSO Library API
ID3D12PipelineLibrary* pso_lib;
device->CreatePipelineLibrary(cache_data, cache_size, IID_PPV_ARGS(&pso_lib));

// 从 Library 加载已缓存的 PSO（极快，无需编译）
ID3D12PipelineState* pso;
pso_lib->LoadGraphicsPipeline(name, desc, IID_PPV_ARGS(&pso));

// 如果未命中，创建后存入 Library
device->CreateGraphicsPipelineState(desc, IID_PPV_ARGS(&pso));
pso_lib->StorePipeline(name, pso);

// 保存 Library 到磁盘
pso_lib->Serialize(&blob, &blob_size);
WriteFile("pso_cache.bin", blob, blob_size);
```

**Vulkan PipelineCache**：
```cpp
// Vulkan 的 PipelineCache
VkPipelineCacheCreateInfo cache_info = {};
cache_info.initialDataSize = file_size;
cache_info.pInitialData = file_data;
vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache);

// 创建 PSO 时传入 cache
vkCreateGraphicsPipelines(device, pipeline_cache, 1, &create_info, nullptr, &pipeline);

// 保存缓存数据
vkGetPipelineCacheData(device, pipeline_cache, &data_size, data);
WriteFile("vk_pipeline_cache.bin", data, data_size);
```

**适用场景**：所有商业级项目。

**隐藏代价**：
- **缓存与驱动/GPU 强相关**：D3D12 的 PSO Library 数据包含驱动版本信息，GPU 驱动升级后可能失效。Vulkan 的 `PipelineCache` 数据包含 `vendorID`、`deviceID`、`driverVersion`，跨设备不兼容。
- **缓存膨胀**：如果 PSO 数量极大，缓存文件可能达到数百 MB。
- **首次启动仍然无缓存**：新玩家第一次启动时，缓存文件不存在。

### 分支 C：Vulkan Graphics Pipeline Library（前沿扩展）

**核心思路**：Vulkan 的 `VK_EXT_graphics_pipeline_library` 扩展允许将 PSO 的编译拆分为多个独立阶段：顶点输入状态、预光栅化状态（Shader）、片段输出状态。每个阶段可以独立编译和缓存，最终链接成完整 PSO 时几乎无开销。

**Source 2 引擎的实践**（Valve，2022 年）：
> "Source 2 was fairly heavily designed around the Direct3D11 model where shaders are created independently and state objects are provided at draw time... VK_EXT_graphics_pipeline_library provides a way to avoid draw time hitching by compiling shaders earlier."

这个扩展解决了 D3D12/Vulkan 的一个核心痛点：在传统模型中，Shader 编译被延迟到 PSO 创建时（因为驱动需要看到所有状态才能优化）。而 Pipeline Library 允许"先编译 Shader，后链接状态"，大幅减少了 cache miss 时的编译延迟。

**适用场景**：Vulkan 后端、有大量 D3D11 风格遗留代码的引擎迁移。

**隐藏代价**：
- 仅支持 Vulkan（以及通过扩展的某些 D3D12 驱动行为），不是跨平台通用方案。
- 需要引擎在架构上支持"分阶段 PSO 构建"，增加了抽象复杂度。
- 最终链接步骤虽然快，但仍非零开销。

### 引擎对照

> 我们在解决的是「PSO 编译结果如何持久化」这个问题。
>
> - **chaos** 的源码未明确显示 PSO 磁盘缓存的实现。从 `PSOManager` 的设计看，它可能依赖驱动自动缓存（分支 A）或内部缓存未暴露。
> - **UE** 使用了**分支 B 的完整工业级实现**：D3D12 的 `PipelineStateCache` 支持从 `.upipelinecache` 文件加载缓存；Vulkan 后端支持 `PipelineCache` 序列化。UE5 的 PSO Precaching 系统还会自动把运行时收集的 PSO 追加到缓存文件。
> - **Bevy/wgpu** 支持 wgpu 的 `PipelineCache` Resource，Bevy 在启动时加载 `cache.bin` 文件，退出时保存。这是**分支 B 的简化版**。

### 决策分析

**默认推荐：分支 B（应用层 Pipeline Cache 文件）作为必实现功能，Vulkan 后端可选启用分支 C（Graphics Pipeline Library）作为优化。**

理由：
1. **应用层缓存是消除"每次启动都重新编译"的唯一可靠手段**。驱动自动缓存（分支 A）不可控、不可靠、跨平台不一致。
2. **D3D12 PSO Library 和 Vulkan PipelineCache 是原生 API 支持的标准机制**，实现成本低，收益极高。对于已有数百个 PSO 的项目，缓存文件可以把启动预热时间从数分钟降到数十秒。
3. **Pipeline Library（分支 C）是 Vulkan 的前沿优化**，但不是架构必需品。如果引擎的 Vulkan 后端已经成熟，可以引入该扩展进一步减少 cache miss 时的编译延迟。但 D3D12 没有直接等价物，不能作为跨平台核心策略。
4. **缓存文件必须包含版本校验**：在文件头写入引擎版本号、着色器字节码哈希、后端类型。加载时如果校验失败，丢弃旧缓存重新构建，避免版本不匹配导致的渲染错误。

**遗留问题**：应用层缓存解决了"启动后重复编译"的问题，但新玩家第一次启动时缓存仍然为空。如何在首次运行前就准备好 PSO 缓存？这是问题 4 的核心——工业级预编译。


## 问题 4：工业级预编译与变体爆炸控制

### 场景与根因

你希望新玩家第一次启动游戏时就能看到流畅画面，而不是在加载画面里等待 PSO 编译。这意味着你需要在**游戏打包阶段**就预先知道所有可能用到的 PSO，并将它们编译好随游戏分发。

**根因**：PSO 的数量由"材质变体"决定。一个基础材质在以下维度上可能产生变体：
- 着色器特性开关（如是否启用法线贴图、是否启用视差映射）
- 渲染路径（延迟 / 前向 / 阴影 / 深度预 pass）
- 平台特性（移动端简化版 / 桌面完整版）
- 顶点工厂类型（静态网格 / 骨骼动画 / 粒子）

如果这些维度有 10 个二值开关，理论变体数就是 2^10 = 1024。如果缺乏控制，这会导致**变体爆炸（Shader Permutation Explosion）**。

### 分支 A：手动枚举 + 离线烘焙

**核心思路**：开发者手动维护一个 PSO 列表，在打包时遍历列表并编译全部 PSO，将结果打包到游戏资源中。

**适用场景**：小型项目、PSO 数量可控（< 100）、团队有专人维护列表。

**隐藏代价**：
- 手动维护极易遗漏：新增一个材质或开关时，开发者可能忘记更新 PSO 列表。
- 工作流成本高：每次内容更新都需要重新烘焙 PSO 缓存。

**失效条件**：任何变体数量 > 100 的项目。

### 分支 B：运行时自动收集 + 打包期批量编译（UE5 PSO Precaching 模式）

**核心思路**：在开发期和测试期，引擎自动记录运行时用到的所有 PSO 配置，生成一个"PSO 集合文件"。打包时读取该集合，批量预编译所有 PSO，随游戏分发。

**UE5 PSO Precaching 的核心设计**（基于 Epic 官方文档和逆向分析）：

```
1. 开发期/测试期：
   - 游戏运行时，每当渲染一个 Primitive Component，PSOPrecache 系统收集该组件在所有可能 Pass 中需要的 PSO 描述
   - 收集的 PSO 包括：Base Pass、Custom Depth、Shadow、Velocity、Virtual Shadow Map 等
   - 记录到 .upipelinecache 文件

2. 打包期：
   - 构建工具读取 .upipelinecache，去重后批量编译
   - 输出平台特定的 PSO 缓存包

3. 运行时：
   - 游戏启动时加载 PSO 缓存包
   - 组件加载后，检查其所需 PSO 是否已缓存
   - 未命中（如用户生成内容）→ 启动异步编译（问题 2 的分支 A）
```

**UE5 的关键优化点**：
- **代理延迟创建（Proxy Creation Delay）**：如果组件渲染所需的 PSO 还在编译中，UE5 可以选择：
  - 跳过该组件的渲染（默认）
  - 回退到引擎默认材质
  - 阻塞等待（不推荐）
- **后台线程池**：`r.pso.PrecompileThreadPoolPercentOfHardwareThreads` 默认使用 75% 硬件线程进行后台编译。
- **内存管理**：预编译完成后可以删除内存中的 PSO 对象，只保留驱动磁盘缓存中的数据，节省运行时内存。

**Unity 6 的类似系统**：
Unity 6 引入了 "PSO Tracing + Precooking" 工作流，允许开发者：
- 在目标设备上运行"追踪模式"，记录所有遇到的 PSO
- 将记录导出为 `GraphicsStateCollection`
- 在加载阶段调用 `WarmUpProgressively` 时间片编译，或后台异步编译

**适用场景**：中大型商业项目、有 QA 测试流程的团队。

**隐藏代价**：
- 需要专门的工具和 CI 流程支持 PSO 追踪、收集、烘焙。
- 自动收集可能遗漏边缘情况（如只在特定 GPU 上触发的 PSO 变体）。
- 用户生成内容（UGC）无法预先收集，仍然需要运行时异步编译兜底。

### 分支 C：变体爆炸控制——限制开关组合

**核心思路**：与其收集所有变体，不如从设计上限制变体数量。通过材质模板、特化常量（Specialization Constants）、动态分支来减少静态 PSO 数量。

```cpp
// 策略 1：特化常量（Vulkan/Metal 支持）
// 用同一个 SPIR-V 模块，通过特化常量切换特性，不生成新 PSO
layout(constant_id = 0) const bool USE_NORMAL_MAP = false;

// 策略 2：动态分支（Uber Shader）
// 在 Shader 中用 uniform 分支，一个 PSO 覆盖多种配置
if (u_features.normal_map_enabled) {
    normal = sampleNormalMap(uv);
} else {
    normal = vertex_normal;
}

// 策略 3：材质模板限制
// 只允许 8 种预定义的材质模板，每种模板固定一套开关组合
enum class MaterialTemplate {
    OpaqueStandard,   // 支持 Albedo + Normal + Roughness
    OpaqueSimple,     // 仅 Albedo
    Transparent,      // 混合模式
    Unlit,            // 无光照
    // ... 共 8 种
};
```

**适用场景**：所有项目，尤其是移动端和独立游戏。

**隐藏代价**：
- 特化常量需要运行时重新创建 Pipeline，不是完全免费。
- 动态分支会降低 GPU 利用率（SIMT 发散）。
- 材质模板限制了美术自由度。

### 引擎对照

> 我们在解决的是「如何预先准备好所有 PSO」这个问题。
>
> - **chaos** 的材质系统有 `ShaderTemplate` 管理变体，但未明确显示自动 PSO 收集系统。可能依赖手动维护或运行时异步编译兜底。
> - **UE** 的 **PSO Precaching** 是分支 B 的工业级巅峰。从 UE5.1 开始，Epic 不再依赖手动收集，而是完全依靠自动运行时收集 + 后台编译。Fortnite 的加载画面因此增加了约 15 秒预热时间，但换来了运行时几乎零 hitch。
> - **Bevy** 目前没有工业级 PSO 预编译系统。Bevy 依赖 wgpu 的驱动缓存和 `PipelineCache` 文件，没有自动收集机制。对于 Bevy 项目，开发者需要手动确保所有材质在启动时被访问一次以预热缓存。

### 决策分析

**默认推荐：分支 B（运行时自动收集 + 打包期批量编译）作为阶段 8-9 的工业级目标，分支 C（变体爆炸控制）作为阶段 5 的必做设计约束。**

理由：
1. **UE5 的 PSO Precaching 证明了自动收集是可行且必要的**。手动维护 PSO 列表（分支 A）在大型项目中不可持续，自动收集是唯一的工业级答案。
2. **但自动收集系统不是阶段 5 初期必须实现的**。阶段 5 的里程碑只是"屏幕中央出现带纹理的旋转立方体"，此时 PSO 数量可能不到 10 个，手动预热即可。`PSOManager` 的接口应预留 `PrecacheCollection()` 方法，但初期可以留空。
3. **变体爆炸控制（分支 C）必须从阶段 5 就纳入设计**。如果阶段 5 允许美术随意添加材质开关，到阶段 7 时 PSO 数量可能爆炸到数千个，届时再限制的成本极高。推荐：
   - 材质系统只暴露 **8~16 种预定义模板**。
   - 优先使用**动态分支**替代静态变体（在移动端注意 SIMT 发散成本）。
   - 对必须使用静态变体的场景（如延迟渲染 GBuffer 组合），用**特化常量**减少 PSO 数量。

**遗留问题**：PSO 缓存、异步编译、预编译策略都已确定。在 ECS 架构下，PSO Manager 本身应该如何表达？AI 如何查询"某个材质对应的 PSO 是否已就绪"？这是问题 5 的核心。


## 问题 5：ECS 架构下的 PSO 管理与 AI 可观测性

### 场景与根因

在传统 OOP 引擎中，`PSOManager` 是一个全局单例，任何渲染代码都可以调用 `PSOManager::GetOrCreate(desc)`。这种设计在 ECS 引擎中面临两个问题：
1. **System 依赖不透明**：AI 无法静态知道哪个 System 会触发 PSO 编译。
2. **状态不可快照**：PSO 编译状态（Pending / Ready / Failed）隐藏在全局单例中，不支持 ECS 的序列化和回放。

**根因**：PSO 管理状态（缓存、待编译队列、占位映射）是可变全局状态，必须被纳入 ECS 的"状态平铺"原则。

### ECS 映射设计

```cpp
// ECS Resource：PSO 管理状态
struct PSOManagerResource {
    HashMap<PipelineStateDesc, PipelineStateHandle> ready_cache;
    HashMap<PipelineStateDesc, AsyncPSOJob> pending_jobs;
    Handle<Texture> error_pink_texture;  // 占位材质用的粉色纹理
};

// ECS Component：材质实例对 PSO 的需求
struct MaterialPSORequest {
    PipelineStateDesc desc;
    bool high_priority;  // 是否高优先级（如玩家眼前的物体）
};

// ECS System：异步编译更新
void PSOCompileSystem(
    Resource<PSOManagerResource> pso_mgr,
    Resource<ThreadPool> thread_pool,
    Query<MaterialPSORequest> requests
) {
    // 1. 检查已完成的编译任务
    for (auto& [desc, job] : pso_mgr->pending_jobs) {
        if (job.state.load() == CompileState::Ready) {
            pso_mgr->ready_cache.Insert(desc, job.result);
            pso_mgr->pending_jobs.Remove(desc);
        }
    }
    
    // 2. 为新的高优先级请求启动编译
    for (auto [entity, req] : requests) {
        if (pso_mgr->ready_cache.Contains(req.desc)) continue;
        if (pso_mgr->pending_jobs.Contains(req.desc)) continue;
        
        AsyncPSOJob job;
        job.desc = req.desc;
        job.state = CompileState::Pending;
        job.future = thread_pool->Submit([desc = req.desc]() {
            return backend->CreatePipelineState(desc);
        });
        pso_mgr->pending_jobs.Insert(req.desc, std::move(job));
    }
}

// ECS System：渲染时使用已就绪的 PSO，未就绪时使用占位
void MeshRenderSystem(
    World& world,
    Resource<PSOManagerResource> pso_mgr,
    Resource<RHICommandBuffer> cmd_buf
) {
    for (auto [transform, mesh, material] : world.Query<Transform, MeshRenderer, Material>()) {
        auto it = pso_mgr->ready_cache.Find(material.pso_desc);
        if (it != pso_mgr->ready_cache.End()) {
            cmd_buf->BindPipeline(it->value);
        } else {
            // PSO 未就绪：绑定占位 PSO（如错误粉色着色器）
            cmd_buf->BindPipeline(pso_mgr->error_placeholder_pso);
        }
        cmd_buf->DrawIndexed(mesh.index_count, ...);
    }
}
```

### AI 可观测性设计

| 检查项 | 实现要求 |
|--------|---------|
| **状态平铺** | `PSOManagerResource` 是 ECS Resource，所有 PSO 状态（缓存、待编译队列）对 AI 可见。 |
| **自描述** | `PSOManagerResource` 提供 `Serialize()` 输出当前缓存数量、待编译数量、各状态占比。 |
| **确定性** | PSO 编译顺序由 `MaterialPSORequest` 的遍历顺序决定。给定相同的场景加载序列，编译顺序可复现。 |
| **工具边界** | AI 通过 MCP 调用 `PrecachePSO(desc)` 时，输入是结构化的 `PipelineStateDesc` JSON，输出是 `Ready/Compiling/Failed` 状态。 |
| **Agent 安全** | PSO 创建操作通过 `PSOCompileSystem` 统一执行，禁止 AI 直接调用底层 `CreatePipelineState`（防止并发竞争和无效描述）。 |

**AI 预热接口示例**：
```json
{
  "tool": "precache_pso_collection",
  "input": {
    "psos": [
      { "vertex_shader": "standard_vs", "pixel_shader": "opaque_ps", "blend": "opaque" },
      { "vertex_shader": "standard_vs", "pixel_shader": "transparent_ps", "blend": "alpha_blend" }
    ],
    "priority": "high",
    "mode": "async"
  },
  "output": {
    "queued": 2,
    "already_ready": 0,
    "estimated_time_ms": 15
  }
}
```

### 引擎对照

> 我们在解决的是「PSO 管理在 ECS 下如何表达」这个问题。
>
> - **chaos** 的 `PSOManager` 是全局单例，与 ECS 无直接集成。
> - **UE** 的 `PipelineStateCache` 是 RHI 层的全局状态，UE 的 ECS（Mass）不直接管理 PSO。UE5 的 PSO Precaching 通过 `FPSOPrecacheProxy` 在组件层面触发，但底层仍然走全局缓存。
> - **Bevy** 的 `PipelineCache` 是 ECS `Resource`，渲染 System 直接查询它。Bevy 的 `SpecializedRenderPipeline` trait 让材质系统可以自定义 PSO 特化逻辑，完全在 ECS 框架内运行。这与我们推荐的 ECS 映射完全一致。

### 决策分析

**默认推荐：Bevy 式完全 ECS 化——`PSOManagerResource` 作为 ECS Resource，`PSOCompileSystem` 和 `MeshRenderSystem` 显式查询和更新它。**

理由：
1. **Bevy 的 `PipelineCache` 已经证明了 PSO 管理可以完全 ECS 化**。没有需要全局单例的强理由。
2. **AI 可观测性要求所有可变状态平铺**。如果 PSO 编译状态隐藏在全局单例中，AI Agent 无法知道"哪些 PSO 还未就绪"，也无法在加载阶段主动预热。
3. **占位 PSO 的绑定决策必须在 ECS System 中显式表达**。这确保了渲染代码的行为可预测、可调试——如果某个物体显示为粉色，开发者可以明确知道是因为 `PSOCompileSystem` 尚未完成其编译。

**遗留问题**：PSO 缓存与异步编译的全链路已经建立。现在，GPU 资源的生命周期管理和 PSO 管理都已经就绪，下一个架构层面的问题是：在 ECS 引擎中，渲染的"全局状态"（Device、Queue、SwapChain）应该如何被多个 System 安全共享？这是 [[ECS架构下的渲染世界设计]] 的核心。

> **下一步**：[[ECS架构下的渲染世界设计]]，因为 RHI 层的接口、资源生命周期、PSO 管理已经各自就绪，下一个问题是：在 ECS 架构下，这些子系统如何被组织成一个协调的"渲染世界"？逻辑线程和渲染线程如何安全并行？
