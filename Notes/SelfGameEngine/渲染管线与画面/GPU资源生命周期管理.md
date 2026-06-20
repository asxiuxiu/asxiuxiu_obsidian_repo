---
order: 31
title: GPU 资源生命周期管理
date: 2026-05-28
tags:
  - self-game-engine
  - rendering
  - gpu-resources
  - lifecycle
  - memory-management
aliases:
  - GPU 资源生命周期管理
---

> **前置依赖**：[[RHI抽象层与命令模型]]
> **本模块增量**：掌握 GPU 资源的安全释放策略（避免 CPU-GPU race condition），理解显存预算与内存压力管理，建立瞬态资源的高效分配模型。你将能够设计一个无资源泄漏、无运行时卡顿、且可观测的 GPU 资源管理层。
>
> 本笔记探讨的核心问题是：**CPU 和 GPU 是异步执行的，CPU 怎么知道 GPU 已经用完了某个资源？显存不足时该驱逐谁？瞬态资源如何避免每帧分配开销？**


## 问题 0：为什么 GPU 资源不能直接用 C++ RAII 管理？

### 场景与根因

假设你有一扇石门，玩家打开后，门上的动态法线纹理不再需要了。你在 C++ 侧写了一个智能指针 `std::shared_ptr<GPUTexture> door_normal;`当玩家离开房间时，智能指针析构，`delete` 了纹理对象，底层调用 `vkDestroyImageView` 和 `vkFreeMemory`。

但 GPU 可能还在处理上一帧的渲染命令——门上的高光 Pass 可能有一两个 Draw Call 还在 GPU 管线深处排队。如果 CPU 立即释放这块显存，轻则画面闪烁（读到脏数据），重则驱动崩溃（访问已释放的资源）。这不是 bug，而是**CPU-GPU 异步执行的固有特性**。

CPU 发出绘制命令后不会等待 GPU 完成，而是继续准备下一帧。两者之间的时间差通常是 **1~3 帧**（取决于交换链的缓冲策略和 GPU 负载）。在 D3D12/Vulkan 的显式 API 中，这个异步性被完全暴露给应用层——驱动不再帮你做隐式同步。

**根因分析**：C++ 的 RAII 模型假设"析构时对象不再被任何人使用"，但在 GPU 渲染管线中，**引用者不是 CPU 线程，而是 GPU 硬件本身**。GPU 的执行进度对 CPU 来说是一个黑箱，除非通过显式同步原语（Fence）查询。

```
CPU Timeline:  帧N-2      帧N-1      帧N        帧N+1
               │         │         │         │
               ▼         ▼         ▼         ▼
               提交命令   提交命令   提交命令   提交命令
                          \
GPU Timeline:              \
                            ▼
                           执行帧N-2    执行帧N-1    执行帧N

在帧N+1删除纹理 ────────────────────────────────► 危险！GPU可能还在帧N使用它
```

**结论**：GPU 资源的生命周期管理必须引入**跨时间维度的同步机制**，让 CPU 的释放操作等待 GPU 的完成信号。这是现代图形 API 与 OpenGL 时代最根本的区别之一。

> **遗留问题**：释放需要等待，但创建呢？如果创建一个 4K HDR 纹理，驱动分配 GPU 内存时会不会阻塞 CPU？这是问题 1 的起点。


## 问题 1：资源创建的同步问题

### 场景与根因

你在加载一个新关卡时，需要一次性创建数十张纹理和顶点缓冲。如果所有创建操作都是同步阻塞的，主线程会在 `CreateTexture` 调用上卡住数百毫秒——玩家看到一个明显的加载冻结。

**根因**：GPU 资源创建涉及驱动与硬件的协商：验证格式支持、分配显存页、建立 GPU 虚拟地址映射。某些操作（如从 CPU 内存上传初始数据到 GPU）还需要等待 DMA 引擎完成。这些都不是"立即可返回"的操作。

### 分支 A：同步创建 + 立即使用

**核心思路**：调用 `CreateBuffer` 时，底层 API 立即分配资源并返回 Handle。如果资源需要初始数据（如纹理像素、网格顶点），在同一线程内完成上传。

```cpp
// 同步创建示例：CPU 阻塞直到驱动分配完成
BufferHandle CreateBufferSync(const BufferDesc& desc, const void* initial_data) {
    // D3D12: CreateCommittedResource 可能阻塞数微秒到数毫秒
    auto handle = backend->CreateBuffer(desc);
    if (initial_data) {
        void* mapped = backend->Map(handle);
        memcpy(mapped, initial_data, desc.size);
        backend->Unmap(handle);
    }
    return handle;
}
```

**适用场景**：资源数量少、加载阶段有进度条可接受阻塞、实现简单优先。

**隐藏代价**：
- 大量同步创建累计耗时可达数百毫秒，造成加载画面冻结。
- 从 CPU 到 GPU 的上传如果走 `Map/Unmap`（Upload Heap），在集成显卡上可能与 CPU 内存竞争带宽。

**失效条件**：运行时动态加载（如玩家走进房间时流式加载新纹理）。

### 分支 B：异步创建 + Staging 上传

**核心思路**：资源创建走快速路径（只分配，不上传），初始数据通过 Staging Buffer 异步上传。创建调用立即返回一个"未就绪"的 Handle，后台线程完成上传后标记为就绪。

```cpp
BufferHandle CreateBufferAsync(const BufferDesc& desc, const void* initial_data) {
    // 1. 立即分配 GPU 资源（通常很快，<0.1ms）
    auto handle = backend->CreateBuffer(desc);
    
    // 2. 把初始数据拷入 Staging Buffer（CPU 内存，立即完成）
    if (initial_data) {
        StagingUploadJob job;
        job.target_handle = handle;
        job.staging_data = CopyToStagingBuffer(initial_data, desc.size);
        upload_queue->Enqueue(job);  // 后台线程处理
        MarkResourcePending(handle); // 标记为"未就绪"
    }
    
    return handle;
}
```

**适用场景**：大型开放世界流式加载、运行时纹理流送、需要隐藏加载延迟的场景。

**隐藏代价**：
- 需要管理"未就绪"状态：渲染代码在资源就绪前不能绑定它，否则会导致 GPU 读取未初始化的内存。
- 增加了架构复杂度：需要上传队列、Fence 跟踪、就绪回调或轮询机制。
- Staging Buffer 本身也是有限资源，大量并发上传可能导致 Staging 内存耗尽。

**失效条件**：资源必须立即可用（如 UI 加载按钮的纹理，不能等几帧）。

### 分支 C：预分配池 + 运行时复用

**核心思路**：在引擎启动或关卡加载时，预先分配一大块显存（Texture Pool / Buffer Pool），运行时从池中分配子区域，无需再调用驱动创建 API。

```cpp
class TexturePool {
    TextureHandle backing_texture;  // 4096x4096 大纹理
    FreeListAllocator sub_allocator; // 子区域分配器
    
public:
    TextureHandle AllocSubTexture(uint32_t w, uint32_t h, Format format) {
        auto offset = sub_allocator.Alloc(w, h);
        return MakeSubTextureView(backing_texture, offset, w, h, format);
    }
};
```

**适用场景**：同尺寸纹理频繁创建销毁（如粒子图集、阴影贴图级联、GBuffer）。

**隐藏代价**：
- 需要手动处理子区域的碎片化和合并。
- 不支持所有纹理类型（如 3D 纹理、CubeMap、MSAA 纹理通常不能简单作为子视图）。
- 如果池子大小预估不足，仍然需要 fallback 到动态创建。

**失效条件**：纹理尺寸差异巨大、或需要独立的 GPU 虚拟地址时。

### 引擎对照

> 我们在解决的是「资源创建是否阻塞」这个问题。
>
> - **chaos** 的 `RenderDeviceBase` 创建资源基本是同步的，但上层通过异步加载管线（`AssetLoadingSystem`）把创建操作分散到后台线程。即**分支 B 的上层实现**：不在 RHI 层做异步，而在资源系统层异步。
> - **UE** 的 `RHIAsyncCreateTexture2D` 提供了显式的异步创建接口，返回一个尚未就绪的纹理引用，由 RHI 内部完成上传后通知上层。同时 UE 大量使用**分支 C**：`TexturePool`、`VertexBufferPool` 在渲染线程预分配常用尺寸。
> - **Bevy/wgpu** 的资源创建是同步的（`device.create_texture` 立即返回），但 wgpu 内部会延迟实际的 GPU 分配和上传。Bevy 的 `RenderAssetPlugin` 把"CPU Asset -> GPU Resource"的转换放到 `PrepareAssets` 阶段，本质上是**分支 B 的 ECS 化表达**。

### 决策分析

**默认推荐：分支 B（异步创建 + Staging 上传）作为运行时加载主路径，分支 C（预分配池）作为高频同尺寸资源的优化层。**

理由：
1. **UE 的异步纹理创建和 Bevy 的 PrepareAssets 都证明了异步路径的必要性**。运行时动态加载不可能接受同步阻塞，这是开放世界和流式加载的底线要求。
2. **预分配池（分支 C）是性能优化而非架构替代**。阶段 5 初期可以没有池化，但架构上必须预留"从池中分配"的接口。当 Profiling 发现某类纹理频繁创建时，再引入 Pool。
3. **同步创建（分支 A）只在编辑器工具链和初始化阶段可用**。运行时代码中应禁止同步创建大资源。

**遗留问题**：资源创建好了、用完了，但 GPU 显存不是无限的。当显存不足时，系统该如何决策驱逐哪些资源？这是问题 2 的核心。


## 问题 2：显存预算与内存压力管理

### 场景与根因

你的游戏在高端 PC 上运行流畅，显存占用 4 GB。但在一款 4 GB 显存的笔记本上，进入某个复杂场景后突然崩溃——`DXGI_ERROR_DEVICE_REMOVED`，或者 Vulkan 的 `VK_ERROR_DEVICE_LOST`。驱动日志显示"Out of Video Memory"。

**根因**：现代引擎中，纹理资产很容易膨胀到数 GB（4K 法线 + 4K 漫反射 + 4K 粗糙度 + Lightmap + Shadowmap + GBuffer + 后处理纹理）。如果引擎对显存使用没有预算概念，它会无限制地加载资产，直到驱动或操作系统强制终止。

更深层的矛盾是：**你想要尽可能多的细节（高分辨率纹理、大的阴影图），但硬件容量是硬上限**。没有预算系统，这个问题不会被暴露，而是直接以神秘崩溃的形式呈现。

### 分支 A：无预算——放任自流

**核心思路**：引擎不跟踪显存使用，创建资源时直接调底层 API，失败时让驱动报错。

**适用场景**：原型阶段、资产极少的简单场景、开发机显存 > 16 GB。

**隐藏代价**：
- 崩溃不可预测：可能在任何时刻、任何资源创建时爆炸。
- 无法做质量降级：当显存紧张时，引擎不知道"该缩哪张纹理"。
- 多平台灾难：开发机 24 GB 显存，测试机 4 GB，两者的行为完全不同。

**失效条件**：任何需要发布到不同硬件配置的项目。

### 分支 B：软预算——跟踪 + 警告 + 策略降级

**核心思路**：引擎维护一个显存使用计数器，每次创建/释放资源时更新。当使用量接近预算阈值时，触发警告并执行策略化降级（如把远距纹理降为低分辨率 Mip）。

```cpp
struct GPUMemoryBudget {
    uint64_t total_budget_bytes;      // 由平台能力或用户设置决定
    uint64_t current_usage_bytes;
    uint64_t warning_threshold;       // 如 80%
    
    bool TryAllocate(uint64_t size) {
        if (current_usage_bytes + size > total_budget_bytes) {
            // 触发压力回调，请求上层释放或降级
            if (!OnMemoryPressure(size)) return false;
        }
        current_usage_bytes += size;
        return true;
    }
};
```

**适用场景**：几乎所有商业引擎。

**隐藏代价**：
- 需要精确跟踪每种资源的显存占用。纹理的显存不是简单的 `width * height * bpp`——驱动可能有对齐填充、Mipmap 链、压缩格式（BC/ASTC）的块尺寸。预算系统必须与后端协商真实占用。
- 压力回调的执行时机敏感：如果在渲染中途触发纹理降级，可能导致该帧的画面质量跳变。
- 预算值的设定本身是难题：不同 GPU 的可用显存不同，系统共享内存（集成显卡）和专用显存的性能差异巨大。

**失效条件**：当需要严格保证不 OOM 时（如主机平台 TRC 要求），软预算的"尝试分配"可能在回调失败后仍然无法拒绝。

### 分支 C：硬预算——强制驱逐 + 虚拟内存

**核心思路**：显存被视为一个受预算约束的缓存。所有资源从磁盘或系统内存按需调入显存，当预算超支时，强制将最近未使用的资源驱逐回系统内存（或完全卸载）。

**适用场景**：大型开放世界、主机平台（PS5/Xbox 的显存管理就是硬预算模型）、纹理流送系统。

**隐藏代价**：
- 实现复杂度极高：需要虚拟纹理（Virtual Texture）或纹理流送（Texture Streaming）系统，维护"哪些 Mip 在显存中"的复杂状态机。
- 驱逐操作本身有开销：把纹理数据从显存迁出到系统内存，或重新加载时从磁盘读取，都需要时间和带宽。
- 需要内容管线配合：美术资产必须按 Mip 链分块存储，否则无法部分加载/驱逐。

**失效条件**：中小型项目、没有专职图形程序员的团队。

### 引擎对照

> 我们在解决的是「显存不足时该怎么办」这个问题。
>
> - **chaos** 使用了**分支 B**：RHI 层有显存统计，超过阈值时输出日志警告，上层资源系统（如 `TextureStreamingManager`）据此调整流送策略。没有硬预算强制驱逐。
> - **UE** 拥有工业级的**分支 C 实现**：`Texture Streaming` 系统基于视角和距离计算每张贴图需要的 Mip 级别，通过 `IStreamingManager` 强制加载/驱逐 Mip 数据。UE 的预算系统与平台 SDK 深度集成（如 Xbox 的 `D3D12X` 内存预算 API）。
> - **Bevy** 目前没有内置的显存预算管理，依赖 wgpu 的内部启发式策略。wgpu 在内存不足时会返回 `OutOfMemory` 错误，由应用层处理。这是**分支 A 向分支 B 演进**的阶段。

### 决策分析

**默认推荐：分支 B（软预算）作为阶段 5 的必实现目标，架构上预留向分支 C（硬预算/纹理流送）演进的数据接口。**

理由：
1. **UE 的 Texture Streaming 是分支 C 的工业巅峰，但硬预算系统的实现依赖完整的 Mip 链流送、虚拟纹理、内容管线配合**。阶段 5 的里程碑只是"屏幕中央出现带纹理的旋转立方体"，不需要硬预算。但如果不预留预算接口，阶段 7-8 引入开放世界时将面临重构。
2. **软预算的核心价值是"可观测性"**。即使不强制驱逐，仅"跟踪显存使用并在接近上限时警告"就能在开发期发现 90% 的 OOM 风险。这是性价比最高的第一步。
3. **分支 B 与 ECS 天然兼容**：`GPUMemoryBudget` 可以是一个 ECS `Resource`，每次创建资源的 System 查询它，压力事件通过 ECS `Event` 广播给降级 System。

**具体实现建议**：
- 每个 `IRHIBuffer` / `IRHITexture` 的实现必须能报告其真实的 GPU 内存占用（通过后端 API 查询，如 D3D12 的 `GetCopyableFootprints` 估算对齐后的尺寸）。
- 定义 `GPUMemoryBudget` ECS Resource，含 `budget_bytes`、`usage_bytes`、`pressure_level`。
- 创建资源的 System 在 `CreateTexture` 前查询预算，超支时触发 `MemoryPressureEvent`。
- 预留 `TextureStreamingPolicy` 组件接口（当前可为空），未来阶段 8 实现虚拟纹理时填充。

**遗留问题**：预算系统告诉我们"显存快满了"，但具体的资源释放还面临一个更基础的问题：CPU 怎么知道 GPU 已经用完了一个资源，可以安全释放？这是问题 3 的核心。


## 问题 3：GPU 资源如何安全释放？

### 场景与根因

这是 GPU 编程中最经典的生命周期问题。假设你在帧 N 提交了一个使用纹理 T 的 Draw Call，然后在帧 N+1 的 CPU 逻辑中决定销毁 T（比如对应的游戏对象被卸载）。如果 CPU 立即调用 `vkDestroyImage`，GPU 可能在帧 N 或 N-1 还在读取 T——因为 GPU 通常滞后 CPU 1~3 帧。

**根因分析**：这不是"多线程竞争"（CPU 线程之间），而是**跨处理器竞争**（CPU vs GPU）。CPU 和 GPU 有自己的时间线，通过命令队列松耦合。CPU 侧的资源释放必须等待 GPU 时间线推进到"安全点"——即所有引用该资源的命令都已执行完毕。

### 分支 A：引用计数 + 延迟删除队列

**核心思路**：每个 GPU 资源维护一个引用计数。当引用计数归零时，不立即释放，而是放入一个**延迟删除队列**。队列中的资源在确认 GPU 已完成所有引用它的帧后，才被真正释放。

```cpp
class GPUResource {
    std::atomic<uint32_t> ref_count{1};
    uint64_t deletion_frame = 0;  // 标记为待删除时的帧号
    
public:
    void AddRef() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    
    void Release() {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            deletion_frame = FrameCounter::Current();
            DelayedDeleteQueue::Get().Enqueue(this);
        }
    }
};

class DelayedDeleteQueue {
    std::vector<GPUResource*> pending;
    
public:
    void Enqueue(GPUResource* res) { pending.push_back(res); }
    
    void Process(uint64_t safe_frame) {
        // 只有 GPU 完全处理完 safe_frame 之前的所有命令后，
        // 才释放标记为删除且帧号 <= safe_frame 的资源
        auto new_end = std::remove_if(pending.begin(), pending.end(),
            [safe_frame](GPUResource* res) {
                if (res->deletion_frame <= safe_frame) {
                    ActuallyDelete(res);
                    return true;  // 从 pending 移除
                }
                return false;
            });
        pending.erase(new_end, pending.end());
    }
};
```

如何知道"GPU 完成了哪一帧"？通过 **Fence**（或 SwapChain 的帧索引）。每提交一帧命令时，向 GPU 队列插入一个 Fence 信号。CPU 端定期查询 Fence 值，得知"GPU 已执行到第 N 帧"。所有在第 N 帧之前标记删除的资源，现在可以安全释放了。

```
CPU Timeline:
  帧N-1: 录制命令 ──► Submit(cmd_list_N-1) + Signal(Fence, N-1)
  帧N:   录制命令 ──► Submit(cmd_list_N)   + Signal(Fence, N)
  帧N+1: 释放纹理T ──► Enqueue(T, frame=N+1)  [不立即delete]
  帧N+2: 查询Fence= N ──► T的deletion_frame=N+1 > N，不释放
  帧N+3: 查询Fence= N+1 ──► T的deletion_frame=N+1 <= N+1，安全释放！
```

**适用场景**：所有需要跨帧存在的 GPU 资源，尤其是纹理、缓冲、渲染目标。

**隐藏代价**：
- **内存膨胀**：如果资源删除很频繁（如每帧创建和销毁大量临时缓冲），延迟队列可能积累大量待删除资源，导致显存峰值比实际需要高 20%~50%。
- **释放延迟不可控**：最坏情况下，资源被删除后 2~3 帧才真正释放。对于显存紧张的平台（移动设备），这可能引发 OOM。
- **Fence 查询开销**：每帧查询 GPU Fence 有轻微 CPU 开销。

**失效条件**：当资源生命周期极短（只存在于一帧内）时，延迟删除的 2~3 帧延迟太浪费了。

### 分支 B：帧环缓冲区（Frame Ring Buffer / Frame Context）

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
        // 记录当前帧的 Fence，下轮回收到本段时就知道 GPU 完成了
        segments[current_frame % kFrameCount].fence = GetCurrentFrameFence();
        current_frame++;
    }
};
```

**Granite 引擎的 Frame Context 变体**（行业参考）：Granite 使用一个"Frame Context"对象持有整帧的所有待释放资源。每帧结束后，Frame Context 带着一个 Fence 进入等待队列。当 Fence 被 GPU  signaled 时，整个 Frame Context 一次性清理所有资源。这比逐个资源跟踪 Fence 更高效。

```cpp
// Granite 风格的 Frame Context
struct FrameContext {
    FenceValue completion_fence;
    std::vector<GPUResource*> deferred_deletions;
    std::vector<CommandPool*> command_pool_recycles;
    
    void PurgeIfComplete() {
        if (IsFenceSignaled(completion_fence)) {
            for (auto* res : deferred_deletions) ActuallyDelete(res);
            for (auto* pool : command_pool_recycles) pool->Reset();
            // 整个 FrameContext 可被复用
        }
    }
};
```

**适用场景**：每帧更新的 uniform buffer、动态顶点/索引数据、临时 staging buffer（CPU -> GPU 上传用）。

**隐藏代价**：
- 需要预估每帧这类资源的总用量，并预分配足够大的环缓冲。预估不准会导致回退到普通堆分配，失去性能优势。
- 不适合生命周期跨帧的资源（如纹理资产、静态网格缓冲）。
- 对于 triple-buffering，最坏情况下同时存在 3 份帧数据，内存占用是单帧的 3 倍。

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

**默认推荐：分支 A 的 UE 式精细化实现——引用计数 + 延迟删除队列，吸收 UE 的"引用计数打包"和"RHI 线程帧边界清理"设计。分支 B 作为瞬态资源的互补优化层。**

理由：
1. **UE 的 `FRHIResource` 是分支 A 的工业级巅峰**。它把引用计数打包在 32 位原子整数中（30 位计数 + 2 位状态标志），`Release()` 归零后标记为延迟删除，由 `FRHICommandListExecutor` 在帧边界批量清理。这不是"过度设计"，而是经过无数项目验证的**最小正确实现**——它用 32 位原子操作就解决了"多线程 AddRef/Release 竞争 + 延迟删除 + 帧安全"三个问题。
2. **分支 A 与 ECS 天然兼容**。GPU 资源可以包装为 ECS `Resource`，引用计数是 Resource 的内部状态，延迟删除队列是另一个 `Resource`。System 不直接操作裸指针，通过 `Handle<T>` 访问，这与 ECS 的"组件/资源是平铺数据"原则一致。
3. **分支 B（帧环缓冲）是性能优化，不是替代方案**。UE 同样大量使用帧环缓冲（`FMemStackBase`、Uniform Buffer 环分配），但它与分支 A 是**互补关系**：环缓冲管"每帧临时数据"，延迟删除管"跨帧持久资源"。阶段 5 初期可以只用分支 A，但架构上要预留环缓冲的接口。
4. **分支 C（显式同步）在运行时路径中是错误选择**。`WaitForIdle()` 会摧毁 CPU-GPU 流水线并行性。UE 只有在编辑器操作（如用户点击删除资产）或关卡切换时才会使用类似机制。

**具体实现建议（吸收 UE 设计）**：
- **引用计数打包**：每个 GPU 资源内部用一个 `std::atomic<uint32_t>` 存储引用计数。借鉴 UE 的做法，用低 30 位存计数，高 2 位存状态标志（如 `kPendingDelete`、`kDeferredDelete`）。`AddRef()` 和 `Release()` 都是无锁原子操作。
- **延迟删除队列**：不是"全局单例队列"，而是**RHI 层内部的帧边界清理机制**。每帧的 `RHISubmitSystem` 在提交命令后，把当帧的 Fence 值记录到队列中。下一帧（或几帧后）的 `RHISubmitSystem` 检查 Fence，确认 GPU 已完成后批量释放。
- **Handle 安全层**：`Handle<Texture>` 对上层暴露，内部持有 `GPUResource*` + 代际标记（generation）。如果资源已被延迟删除，Handle 的代际标记不匹配，访问时返回安全错误（而非悬空指针）。这是 UE 的 `FRHIResource` 句柄安全思想的简化版。
- **Frame Context 预留**：架构上支持 Granite 风格的 `FrameContext`，每帧的资源释放集中到 FrameContext 中，由 FrameContext 的 Fence 统一 gate。这比逐个资源查 Fence 更高效。

**遗留问题**：延迟删除和帧环分配解决了"释放时机"问题，但还有一个更深层的优化空间：很多资源（如后处理中间纹理）只存在于一帧内，而且尺寸固定。能不能让它们在物理显存上**复用同一块内存**？这是问题 4 的核心。


## 问题 4：瞬态资源与帧环分配

### 场景与根因

你的延迟渲染管线需要多张全屏中间纹理：GBuffer 的 Albedo、Normal、Roughness，然后 Lighting Pass 输出到 HDR 缓冲，再经过 ToneMapping 输出到 SwapChain。这些纹理每一帧都被创建、使用、丢弃。如果每帧都 `CreateTexture` / `DestroyTexture`，驱动分配器的压力极大，且延迟删除队列会积累大量待释放纹理。

**根因**：这些资源的共同特征是**生命周期恰好一帧**，且**尺寸和格式在管线设计时就已确定**。它们不需要长期存在于显存中，但每帧都需要物理内存 backing。延迟删除（分支 A）对这种场景是过度设计——它引入了 2~3 帧的释放延迟，而资源本应在帧末就可立即回收。

### 分支 A：每帧 new/delete（延迟删除兜底）

**核心思路**：即使是临时纹理，也走标准的创建/释放流程，依赖问题 3 的延迟删除队列在几帧后回收。

**适用场景**：实现简单、临时纹理数量极少（< 10/帧）。

**隐藏代价**：
- 每帧创建纹理可能触发驱动的 GPU 内存分配，耗时 0.1~1 ms。
- 延迟删除队列在显存紧张时会膨胀，因为临时纹理占据的空间要 2~3 帧后才释放。

**失效条件**：后处理链复杂（> 5 张中间纹理）、或目标平台显存紧张。

### 分支 B：帧环缓冲区（Frame Ring Buffer for Textures）

**核心思路**：预分配一组固定尺寸的大纹理作为"帧缓冲池"。每帧从池中取一个作为临时渲染目标，帧末归还。通过 Fence 保证 GPU 完成后才复用。

```cpp
class FrameTransientTexturePool {
    static constexpr uint32_t kFramesInFlight = 3;
    
    struct PoolSlot {
        TextureHandle texture;
        FenceValue last_use_fence;
        bool available;
    };
    
    // 按 (width, height, format) 分组的池子
    std::map<TextureDesc, std::vector<PoolSlot>> pools;
    
public:
    TextureHandle Acquire(const TextureDesc& desc) {
        auto& pool = pools[desc];
        uint64_t current_fence = GetCompletedFenceValue();
        
        // 查找已完成 GPU 使用的空闲槽
        for (auto& slot : pool) {
            if (slot.available && slot.last_use_fence <= current_fence) {
                slot.available = false;
                return slot.texture;
            }
        }
        
        // 无空闲槽，创建新纹理
        PoolSlot new_slot;
        new_slot.texture = backend->CreateTexture(desc);
        new_slot.available = false;
        pool.push_back(new_slot);
        return new_slot.texture;
    }
    
    void Release(TextureHandle handle, const TextureDesc& desc) {
        auto& pool = pools[desc];
        for (auto& slot : pool) {
            if (slot.texture == handle) {
                slot.available = true;
                slot.last_use_fence = GetCurrentFrameFence();
                break;
            }
        }
    }
};
```

**适用场景**：GBuffer、后处理中间纹理、阴影图级联等尺寸/格式固定的临时资源。

**隐藏代价**：
- 池子按 `(w, h, format)` 分组，分组数量多时会增加查找开销。
- 最坏情况下池子无限增长（如果每帧都需要新的尺寸组合）。需要设置池子上限或 LRU 驱逐。
- 3 帧的缓冲意味着同一时刻最多 3 份同尺寸纹理在显存中。

### 分支 C：显存别名（Memory Aliasing / Transient Resource Aliasing）

**核心思路**：利用 RenderGraph 的依赖分析，让生命周期不重叠的临时资源**共享同一块物理 GPU 内存**。这是 UE 的 RDG 和 Bevy 的 `TextureCache` 都在使用的技术。

```
物理显存块 X (1920x1080xRGBA16F = 16.5MB)
├── 时间片 A (GBuffer Pass): 被 GBuffer_Normal 使用
├── 时间片 B (Lighting Pass): 被 HDR_Color 使用
└── 时间片 C (Bloom Pass): 被 Bloom_Mip0 使用

这三个纹理在时间上互不重叠，因此可以绑定到同一个 ID3D12Resource/VkImage
```

UE 的 `FRDGBuilder` 在 Compile 阶段分析所有 Pass 的资源依赖，对标记为 `Transient` 的纹理进行内存别名分配。Bevy 的 `TextureCache` 则以更简单的"描述符匹配 + 3 帧回收"实现类似效果。

**适用场景**：有 RenderGraph 或明确 Pass 依赖管线的引擎。

**隐藏代价**：
- 需要精确的 Pass 依赖分析。如果错误地让两个同时活跃的纹理共享内存，会导致画面 corruption。
- 别名资源的调试极其困难：在 GPU 调试器中，同一个物理资源在不同时间有不同的"逻辑身份"。
- 平台限制：某些移动 GPU 对别名内存的压缩/解压有特殊要求。

**失效条件**：没有 RenderGraph、或无法精确推导资源生命周期时。

### 引擎对照

> 我们在解决的是「临时资源如何高效分配」这个问题。
>
> - **chaos** 的 `RenderGraph` 承担了临时资源管理职责。从源码看，`RenderGraphRHIResourceManager` 会复用 RHI 资源，但具体是分支 B（池化）还是分支 C（别名）需要进一步确认。
> - **UE** 的 RDG 是**分支 C 的工业级实现**：`FRDGBuilder::Compile()` 阶段进行显存别名计算，`FRDGAllocator` 管理物理堆的分配和复用。对于 Uniform Buffer 等每帧数据，UE 使用**分支 B** 的环分配。
> - **Bevy** 的 `TextureCache` 是**分支 B 的简化版**：以 `TextureDescriptor` 为 key 管理池子，每帧回收超过 3 帧未使用的纹理。Bevy 没有显式的显存别名系统，因为 wgpu 的验证层对内存别名有严格限制。

### 决策分析

**默认推荐：分支 B（帧环缓冲池）作为阶段 5 的必实现目标，架构上预留分支 C（显存别名）的接口。**

理由：
1. **分支 C（显存别名）的收益巨大但依赖 RenderGraph**。我们的 RenderGraph 笔记（5.7）尚未产出，在 RenderGraph 成熟之前，无法安全地实施显存别名。
2. **分支 B（帧环缓冲池）是独立可实现的**。即使没有 RenderGraph，一个简单的 `TransientTexturePool` 就能消除 90% 的临时纹理分配开销。Bevy 的 `TextureCache` 证明了这一点。
3. **分支 B 与 ECS 兼容**：Pool 可以是一个 ECS `Resource`，`Acquire` / `Release` 在 System 中调用。帧末的 `CleanupSystem` 批量更新 Fence 和回收标记。

**具体实现建议**：
- 定义 `TransientResourcePool` ECS Resource，支持 `AcquireTexture(desc)` 和 `ReleaseTexture(handle, desc)`。
- 按 `(width, height, format, usage)` 四维 key 分组管理池子。
- 每帧初检查 `safe_frame = CompletedFence()`，回收所有 `last_use_fence <= safe_frame` 的槽位。
- 设置池子软上限（如每组最多 8 个槽），超限时释放最早未使用的纹理。

**遗留问题**：现在我们已经有了资源创建、预算、安全释放、瞬态分配的全链路。但在 ECS 架构下，这些"全局状态"（Device、Queue、资源池、延迟删除队列）应该如何表达？传统的单例模式与 ECS 的"无全局状态"原则是冲突的。这是问题 5 的核心。


## 问题 5：ECS 架构下的资源生命周期表达与 AI 可观测性

### 场景与根因

在传统 OOP 引擎中，GPU 资源管理通常是全局单例：`g_render_device`、`g_texture_manager`、`g_delayed_delete_queue`。这些单例在任何地方都可以被访问，逻辑代码直接 `g_texture_manager->Create(...)`。

但在 ECS 架构中，**全局可变的单例是反模式的**。ECS 的原则是：所有状态要么是 Component（附着在 Entity 上），要么是 Resource（附着在 World 上），System 通过查询（Query）来访问状态。如果允许全局单例，System 的输入输出就不透明了——AI 无法知道某个 System 会修改哪些资源，也无法做确定性回放。

**根因**：OOP 的"全局管理器"模式与 ECS 的"状态平铺 + 显式依赖"模式存在根本性冲突。我们需要把 GPU 资源管理的全局状态"ECS 化"。

### 分支 A：保留全局单例，ECS 只管理逻辑层

**核心思路**：RHI 层仍然用全局单例管理 GPU 资源（`RenderDevice`、`TextureManager`），ECS World 只管理游戏逻辑对象（Entity、Transform、MeshRenderer）。渲染 System 从 ECS 读取组件数据，然后调用全局 RHI 单例创建/使用/释放资源。

```cpp
// 全局单例（非 ECS）
RenderDevice* g_device;
TextureManager* g_texture_mgr;

// ECS System
void MeshRenderSystem(World& world) {
    for (auto [transform, mesh] : world.Query<Transform, MeshRenderer>()) {
        // 直接访问全局 RHI
        auto tex = g_texture_mgr->Get(mesh.texture_id);
        g_device->BindTexture(tex);
        g_device->DrawIndexed(mesh.index_count);
    }
}
```

**适用场景**：从 OOP 引擎向 ECS 迁移的过渡期、RHI 层是第三方库（如 wgpu）无法修改。

**隐藏代价**：
- System 的依赖不透明：AI 无法静态分析 `MeshRenderSystem` 会访问哪些 GPU 资源。
- 多线程不安全：全局单例的并发访问需要额外的锁，与 ECS 的 System 并行调度冲突。
- 无法做确定性回放：全局单例的状态变化不在 ECS 的快照范围内。

**失效条件**：当需要多 Agent 协作修改渲染状态、或需要 GPU 资源的确定性回放时。

### 分支 B：完全 ECS 化——RHI 资源作为 World Resource

**核心思路**：所有 GPU 资源管理状态都表达为 ECS `Resource`。`RenderDevice`、`TextureTable`、`DelayedDeleteQueue`、`FrameRingBuffer`、`GPUMemoryBudget` 都是 World 上的 Resource。System 显式声明它们作为参数。

```cpp
// ECS Resource（纯数据，无行为）
struct RenderDevice { NativeDevice* device; };
struct TextureTable { DenseArray<TextureSlot> slots; FreeList free_list; };
struct DelayedDeleteQueue { std::vector<PendingDelete> pending; uint64_t safe_frame; };
struct GPUMemoryBudget { uint64_t usage; uint64_t budget; };

// ECS System：显式依赖
void TextureCleanupSystem(
    Resource<DelayedDeleteQueue> delete_queue,
    Resource<RenderDevice> device,
    Resource<GPUMemoryBudget> budget
) {
    delete_queue->safe_frame = device->QueryCompletedFrame();
    for (auto it = delete_queue->pending.begin(); ...) {
        if (it->frame <= delete_queue->safe_frame) {
            device->DestroyTexture(it->handle);
            budget->usage -= it->size;
            it = delete_queue->pending.erase(it);
        }
    }
}
```

**适用场景**：纯 ECS 引擎、需要 AI 完全可观测、需要确定性回放。

**隐藏代价**：
- RHI 层的实现必须适配 ECS 的数据导向风格，不能再有"对象即方法载体"的 OOP 设计。
- `RenderDevice` Resource 中通常包含原生 API 的指针（如 `ID3D12Device*`），这些指针不是 POD，序列化/快照时需要特殊处理。
- 某些第三方库（如 wgpu）的接口是 OOP 风格，需要额外包装层才能融入 ECS。

### 分支 C：混合模型——RHI 核心状态为 Resource，底层后端为单例

**核心思路**：ECS World 持有 RHI 的"配置和调度状态"（预算、队列、表），但底层 API 对象（`ID3D12Device`、`VkDevice`）作为不可变单例存在。System 操作的是 ECS 层面的 Resource，底层调用通过这些 Resource 转发到单例后端。

```cpp
// 不可变单例（启动时创建，永不改变）
class D3D12Backend { ID3D12Device* device; ... };
D3D12Backend* g_d3d12_backend;  // 只读，启动后不变

// ECS Resource（可变状态）
struct TextureTable { ... };
struct DelayedDeleteQueue { ... };

// System：修改 ECS Resource，Resource 内部调用不可变后端
void CreateTextureSystem(
    Command<CreateTextureRequest> requests,
    Resource<TextureTable> table,
    Resource<GPUMemoryBudget> budget
) {
    for (auto& req : requests) {
        if (budget->TryAllocate(req.desc.size)) {
            // 通过 Resource 内部方法调用全局后端（只读访问）
            auto handle = table->Alloc(g_d3d12_backend, req.desc);
            req.out_handle = handle;
        }
    }
}
```

**适用场景**：自研 RHI + 需要 ECS 可观测性，但不想把底层 API 指针强行塞进 ECS 的序列化路径。

**隐藏代价**：
- 架构上需要明确区分"可变 ECS 状态"和"不可变后端单例"，增加了设计复杂度。
- 如果未来需要热切换后端（如 D3D12 <-> Vulkan），"不可变"假设会失效。

### 引擎对照

> 我们在解决的是「RHI 全局状态在 ECS 下如何表达」这个问题。
>
> - **chaos** 基本是**分支 A**：`DynamicRHI` 和 `PSOManager` 是全局单例，上层 ECS（如果 chaos 有 ECS 的话）调用它们。chaos 的源码显示 OOP 全局管理器是主流模式。
> - **UE** 也是**分支 A 的工业级版本**：`GDynamicRHI`、`FRHICommandListExecutor` 是全局状态，UE 的 ECS（Mass）与 RHI 层没有直接集成。UE 的 RHI 设计早于其 ECS 化，所以这种分离是历史原因。
> - **Bevy** 是**分支 B 的标杆**：`RenderDevice`、`RenderQueue`、`PipelineCache`、`TextureCache` 全部是 ECS `Resource`。Bevy 的渲染 System 显式查询这些 Resource，没有全局单例。wgpu 的底层指针被包装在 `WgpuWrapper` 中，作为 Resource 的内部字段。

### 决策分析

**默认推荐：分支 C（混合模型）作为阶段 5 的实现目标，但接口设计向分支 B（完全 ECS 化）演进。**

理由：
1. **Bevy 的分支 B 是 ECS 引擎的终极形态**，但实现它需要 RHI 层完全按数据导向重写。阶段 5 初期，我们可能还在复用一些底层库的 OOP 接口（如 D3D12 的 COM 对象），完全消除"对象方法"不现实。
2. **分支 C 的"不可变后端 + 可变 ECS Resource"是最务实的折中**。底层 API 对象（`ID3D12Device*`）在运行时确实不会改变，把它们视为不可变单例不影响 ECS 的确定性。而所有可变状态（资源表、删除队列、预算）都放入 ECS Resource，保证了可观测性和可快照。
3. **分支 A（全局单例）在 ECS 引擎中是技术债务**。如果阶段 5 就采用全局单例，阶段 8 的 AI 桥接和多 Agent 编排会发现"渲染状态不可被 AI 安全操作"——那时重构成本极高。

**AI 友好设计清单**：

| 检查项 | 实现要求 |
|--------|---------|
| **状态平铺** | 所有可变 GPU 资源状态（资源表、删除队列、预算、环缓冲偏移）都是 ECS Resource 或 Component，不隐藏在全局单例中。 |
| **自描述** | `TextureTable`、`BufferTable` 等 Resource 提供 `Serialize()` 方法，输出 JSON/Schema 描述当前资源列表和占用。 |
| **确定性** | `TextureCleanupSystem` 的输入（Fence 值、pending 列表）完全由 ECS 状态决定。给定相同的输入序列，资源释放顺序可复现。 |
| **工具边界** | AI 通过 MCP 操作 GPU 资源时，接口输入输出是结构化的（如 `CreateTextureRequest { width, height, format }` → `Result<TextureHandle>`）。 |
| **Agent 安全** | 资源创建/释放操作通过 Command Buffer（ECS Event）排队，由专门的 `ResourceManagementSystem` 统一执行，防止 AI 的并发误操作。 |

**遗留问题**：GPU 资源生命周期的全链路（创建、预算、释放、瞬态分配、ECS 表达）已经建立。但资源创建本身也可能很慢——尤其是现代 API 的管线状态对象（PSO），在 D3D12/Vulkan 下编译一个 PSO 可能耗时数毫秒，首次使用时直接造成卡顿。PSO 的管理与缓存是下一篇笔记的核心。

> **下一步**：[[PSO缓存与异步编译]]，因为 GPU 资源的生命周期管理已经就绪，下一个工业级瓶颈是：管线状态对象的编译卡顿如何消除？
