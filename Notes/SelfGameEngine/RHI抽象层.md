---
title: RHI抽象层
date: 2026-04-13
tags:
  - self-game-engine
  - rendering
  - rhi
  - architecture
aliases:
  - RHI Abstraction Layer
---

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])

# RHI 抽象层

> **前置依赖**：[[从零开始的引擎骨架]]、[[数学基础]]
> **本模块增量**：你的引擎获得了跨平台图形 API 抽象能力，能把一帧画面输出到窗口。上层渲染代码不再直接调用 D3D12/Vulkan。
> **下一步**：[[资源管理]] —— 有抽象层后，需要管理纹理、网格、着色器的上传和释放。

---

## Why：为什么游戏引擎需要 RHI？

### 场景一：平台移植噩梦
你花了三个月用 D3D12 写了一套延迟渲染管线，代码里到处是 `ID3D12Device*`、`D3D12_RESOURCE_BARRIER`、`CD3DX12_PIPELINE_STATE_STREAM`。老板突然说："Mac 版也要上。"

没有 RHI 时，你必须把上千处 D3D12 调用全部重写成 Metal。两个月后老板说："PS5 版也安排一下。" 你再次崩溃。

### 场景二：API 噪音淹没算法
你想实现一个 Bloom 后处理效果。核心思路很简单：降采样 → 高斯模糊 → 升采样叠加。但 D3D12 要求你先创建 `ID3D12RootSignature`，再配置 `D3D12_RENDER_TARGET_BLEND_DESC`，再处理 `D3D12_RESOURCE_STATE_TRANSITION_BARRIER`。

你的后处理代码 80% 是在和 GPU 驱动"寒暄"，只有 20% 在实现算法。Vulkan 更夸张，创建一个简单的 Framebuffer 可能需要 20 行样板代码。

### 场景三：团队协同的接口地狱
美术同学写材质编辑器时需要绑定纹理，TA 同学写后处理时需要创建 RT，引擎组同学写裁剪时需要读取深度缓冲。如果每个人都直接调用底层 API，代码风格、资源生命周期、错误处理方式将五花八门，调试和 review 成本指数级上升。

### 结论

RHI（Render Hardware Interface）不是"锦上添花"的优化，而是**现代游戏引擎的必需品**。它的核心使命只有一个：

> **让上层渲染代码"写一次，跑遍所有平台"，同时把底层 API 的噪音屏蔽在接口之后。**

---

## What：最简化版本的 RHI 长什么样？

下面的代码是一个**能编译、能运行**的最小 RHI 骨架。它只定义了跨平台所需的最少接口，并附带一个 `NullRHI` 后端（什么都不画，但流程完整）。

```cpp
// ============================================================
// 最小 RHI 接口（约 80 行核心代码）
// ============================================================

enum class ERHIApi { Null, D3D12, Vulkan, Metal };

// 资源句柄：后端不透明，上层只认句柄
using RHIBufferHandle    = uint32_t;
using RHITextureHandle   = uint32_t;
using RHIShaderHandle    = uint32_t;
using RHIPipelineHandle  = uint32_t;

// 顶点数据描述
struct Vertex { float x, y, z; float r, g, b; };

// 命令缓冲区：录制的命令序列
class IRHICommandBuffer {
public:
    virtual ~IRHICommandBuffer() = default;
    virtual void begin() = 0;
    virtual void end()   = 0;
    virtual void setViewport(uint32_t w, uint32_t h) = 0;
    virtual void bindPipeline(RHIPipelineHandle pso) = 0;
    virtual void bindVertexBuffer(RHIBufferHandle vb) = 0;
    virtual void draw(uint32_t vertex_count) = 0;
};

// 设备：资源的工厂 + 命令的执行者
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // 窗口/SwapChain
    virtual bool init(void* native_window, uint32_t w, uint32_t h) = 0;
    virtual void present() = 0;

    // 资源工厂
    virtual RHIBufferHandle   createVertexBuffer(const void* data, size_t size) = 0;
    virtual RHIShaderHandle   createShader(const char* code, const char* entry) = 0;
    virtual RHIPipelineHandle createPipeline(RHIShaderHandle vs, RHIShaderHandle ps) = 0;

    // 命令
    virtual IRHICommandBuffer* createCommandBuffer() = 0;
    virtual void submit(IRHICommandBuffer* cmd) = 0;
};

// ============================================================
// NullRHI 后端：用于测试和无头环境
// ============================================================
class NullCommandBuffer : public IRHICommandBuffer {
public:
    void begin() override {}
    void end()   override {}
    void setViewport(uint32_t, uint32_t) override {}
    void bindPipeline(RHIPipelineHandle) override {}
    void bindVertexBuffer(RHIBufferHandle) override {}
    void draw(uint32_t count) override {
        printf("[NullRHI] draw %u vertices\n", count);
    }
};

class NullDevice : public IRHIDevice {
    uint32_t m_next_handle = 1;
public:
    bool init(void*, uint32_t, uint32_t) override { return true; }
    void present() override { printf("[NullRHI] present\n"); }

    RHIBufferHandle createVertexBuffer(const void*, size_t) override {
        return m_next_handle++;
    }
    RHIShaderHandle createShader(const char*, const char*) override {
        return m_next_handle++;
    }
    RHIPipelineHandle createPipeline(RHIShaderHandle, RHIShaderHandle) override {
        return m_next_handle++;
    }

    IRHICommandBuffer* createCommandBuffer() override {
        return new NullCommandBuffer();
    }
    void submit(IRHICommandBuffer* cmd) override {
        // 在真实后端中，这里会把命令提交给 GPU 队列
        delete cmd;
    }
};

// ============================================================
// 使用示例：画一个三角形（流程完整，NullRHI 可跑）
// ============================================================
int main() {
    IRHIDevice* device = new NullDevice();
    device->init(nullptr, 800, 600);

    Vertex triangle[] = {
        { 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
        { 0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f},
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f},
    };
    auto vb = device->createVertexBuffer(triangle, sizeof(triangle));
    auto pso = device->createPipeline(1, 2); // 简化：直接用句柄占位

    auto* cmd = device->createCommandBuffer();
    cmd->begin();
    cmd->setViewport(800, 600);
    cmd->bindPipeline(pso);
    cmd->bindVertexBuffer(vb);
    cmd->draw(3);
    cmd->end();

    device->submit(cmd);
    device->present();

    delete device;
    return 0;
}
```

### 这个最小实现已经解决了什么？

1. **跨平台入口统一**：无论后端是 D3D12 还是 Vulkan，`main()` 里的代码不变。
2. **资源工厂模式**：上层不需要知道 `ID3D12Resource` 和 `VkBuffer` 的区别，只拿到一个 `uint32_t` 句柄。
3. **命令录制与提交分离**：这是现代 GPU API（D3D12/Vulkan/Metal）的核心范式，RHI 从一开始就暴露了这个模型。

### 还缺什么？

- 没有 **SwapChain/BackBuffer** 管理（还没真正把像素写到屏幕上）。
- 没有 **资源状态追踪/Barrier**（D3D12/Vulkan 下会立刻崩溃）。
- 没有 **多线程命令录制**（只有一个 `createCommandBuffer`）。
- 没有 **Shader 编译和 PSO 缓存**（每次 `createPipeline` 都可能触发数毫秒的驱动编译）。

这些正是我们在 **How** 阶段要逐步补上的。

---

## How：真实引擎的 RHI 是如何一步一步复杂起来的？

### 阶段 1：最小实现 → 能用（解决窗口呈现 + 基本资源）

**触发原因**：最小骨架虽然流程完整，但 `NullDevice` 不会真的在窗口上画任何东西。我们需要接入真实的 GPU API，把三角形渲染到屏幕上。

#### 新增设计元素

| 元素 | 解决的问题 |
|------|-----------|
| `SwapChain` 抽象 | 窗口句柄 → BackBuffer → Present 的跨平台统一 |
| `Texture` / `Buffer` 描述符 | 用 `TextureDesc` / `BufferDesc` 替代后端特定的创建参数 |
| `RenderPass` 描述符 | 统一 D3D12 的 RTV/DSV 绑定与 Vulkan 的 `VkRenderPass` |
| `ShaderModule` | 将编译后的 SPIR-V / DXBC / MetalIL 封装为统一句柄 |

#### 代码层面的变化

```cpp
// 阶段 1 新增：SwapChain 与资源描述符
struct SwapChainDesc {
    void* native_window;
    uint32_t width, height;
    uint32_t buffer_count = 2; // 双缓冲
};

struct TextureDesc {
    uint32_t width, height;
    ETextureFormat format;
    ETextureUsage  usage; // RenderTarget | DepthStencil | ShaderResource
};

class IRHIDevice {
    // ... 阶段 0 接口 ...
    virtual ISwapChain* createSwapChain(const SwapChainDesc& desc) = 0;
    virtual ITexture*   createTexture(const TextureDesc& desc) = 0;
    virtual void        beginRenderPass(IRHICommandBuffer* cmd, ITexture* rt, ITexture* ds) = 0;
    virtual void        endRenderPass(IRHICommandBuffer* cmd) = 0;
};
```

**关键决策**：
- `SwapChain` 是否属于 RHI？**是的**。虽然 SwapChain 和窗口系统强相关，但它直接管理 BackBuffer 的格式、翻转模式和 VSync，必须由 RHI 后端根据平台 API 实现。
- `native_window` 用 `void*`：Win32 下是 `HWND`，macOS 下是 `NSView`/`CALayer`，Linux 下可能是 `xcb_window_t`。用 `void*` 是最小侵入的跨平台方案。

---

### 阶段 2：能用 → 好用（解决迭代效率和调试问题）

**触发原因**：阶段 1 的 RHI 已经可以画三角形了，但每增加一个 Pass 或材质变体，你都需要手动处理大量重复和易错的工作：
- PSO 编译卡顿（D3D12/Vulkan 下创建 PipelineState 可能要几毫秒）。
- 多线程渲染时每个线程都自己创建/销毁命令分配器，开销巨大。
- 资源状态写错（比如忘了把 Texture 从 `ShaderResource` 转成 `RenderTarget`）导致驱动崩溃，调试困难。

#### 新增设计元素

| 元素 | 解决的问题 |
|------|-----------|
| **PSO 缓存** | 避免运行时重复编译着色器管线状态 |
| **命令缓冲区池** | 多线程并行录制命令，减少分配器分配开销 |
| **资源状态追踪 + Barrier 推导** | 自动插入 `ResourceBarrier`，降低手写错误 |
| `RHIUtils` 工具层 | 高频操作（创建全屏quad、上传 staging buffer）的快捷封装 |

#### 2.1 PSO 缓存：从"每次编译"到"查表复用"

D3D12/Vulkan 的 `PipelineState` 编译代价极高。真实引擎的解决方案是：**用描述符的 Hash 做键，全局缓存 PSO 对象**。

```cpp
struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle pixel_shader;
    VertexLayout vertex_layout;
    BlendState   blend;
    DepthState   depth;
    RasterState  raster;
    RenderTargetFormat rt_formats[8];
};

class PSOManager {
    HashMap<uint64_t, RHIPipelineHandle> m_cache;
public:
    RHIPipelineHandle getOrCreate(const PipelineDesc& desc) {
        uint64_t key = hashPipelineDesc(desc);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;

        // 缓存未命中：调用后端真正创建（可能耗时数毫秒）
        auto pso = m_device->createPipelineInternal(desc);
        m_cache[key] = pso;
        return pso;
    }
};
```

**这是从 chaos 源码中可以直接"偷走"的一招**：
- `PSOSRBKey` 将 `GraphicsPipelineStateCreateInfo` 的各字段混合计算为 64/128 位 Hash。
- 使用并发 HashMap（如 `ConcurrentHashMapWithPool`）保证多线程安全。
- 如果材质关键字组合爆炸（permutation 过多），工业级实现还会加入 LRU 驱逐策略。

#### 2.2 命令缓冲区池：多线程录制的基石

现代 GPU 驱动鼓励**多线程并行录制命令列表**。RHI 需要提供一个可复用的命令缓冲区池：

```cpp
class RHICommandBufferPool {
    DynamicArray<IRHICommandBuffer*> m_free_buffers;
    Mutex m_mutex;
public:
    IRHICommandBuffer* allocate() {
        LockGuard lock(m_mutex);
        if (!m_free_buffers.empty()) {
            auto* cmd = m_free_buffers.back();
            m_free_buffers.pop_back();
            cmd->reset(); // 重置内部状态，准备重新录制
            return cmd;
        }
        return m_device->createCommandBuffer();
    }
    void release(IRHICommandBuffer* cmd) {
        LockGuard lock(m_mutex);
        m_free_buffers.push_back(cmd);
    }
};
```

**使用模式**：
- Shadow Pass、GBuffer Pass、Lighting Pass 可以分别由不同线程从池中获取 `CommandBuffer`，并行录制。
- 录制完成后，主线程按顺序 `submit(cmdA)` → `submit(cmdB)` → `submit(cmdC)` → `present()`。

#### 2.3 资源 Barrier 自动推导（可选但强烈推荐）

D3D12 和 Vulkan 要求显式声明资源状态转换（如 `PRESENT` → `RENDER_TARGET` → `SHADER_RESOURCE`）。手动管理非常容易出错。

**阶段 2 的简化策略**：在 `beginRenderPass` / `endRenderPass` / `bindTexture` 时，RHI 内部追踪每个 `ITexture*` 的当前状态，自动插入必要的 Barrier。

```cpp
// 简化示例：RHI 内部维护资源状态表
void D3D12Device::bindTexture(IRHICommandBuffer* cmd, uint32_t slot, ITexture* tex) {
    auto* d3d_tex = static_cast<D3D12Texture*>(tex);
    if (d3d_tex->current_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        insertBarrier(cmd, d3d_tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        d3d_tex->current_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    // ... 真正绑定 SRV
}
```

> 在 chaos 引擎中，更复杂的 Barrier 推导被交给了 **RenderGraph**。RHI 层只负责执行 RenderGraph 推导出的 Barrier 列表。这是阶段 3 的工业级做法。

---

### 阶段 3：好用 → 工业级（解决性能、规模和平台覆盖）

**触发原因**：当你的引擎需要跑在 PS5/Xbox、支持 RayTracing、实现异步计算、处理数千个 Draw Call 时，阶段 2 的 RHI 会遇到瓶颈：
- D3D11 legacy 路径和新 API（D3D12/Vulkan）的行为差异越来越大。
- 主机平台（AGC/GNM）有独特的内存管理、显存分配、异步提交模型。
- 描述符（Descriptor/Sampler/RTV）数量爆炸，需要专门的堆管理。

#### 新增设计元素

| 元素 | 解决的问题 |
|------|-----------|
| **双层 RHI 架构** | 底层 `graphics_interface` + 高层 `render/RHI`，兼顾移植性和易用性 |
| **多队列支持** | Graphics Queue + Compute Queue + Copy Queue 的并行调度 |
| **描述符管理器** | DescriptorHeap / DescriptorPool 的分配与回收 |
| **Native Pointer 透传** | 允许外部库（如 DLSS/FSR）直接操作底层资源 |

#### 3.1 双层 RHI 架构（chaos 的设计精华）

 chaos 引擎的 RHI 采用"底层接口层 + 高层业务层"的双层结构：

```
┌─────────────────────────────────────────┐
│  高层业务层：render/RHI/                  │
│  DynamicRHI、RHICommandContext、         │
│  RHIComputeContext、PSOManager          │
├─────────────────────────────────────────┤
│  底层接口层：graphics_interface/          │
│  RenderDeviceBase、CommandListBase、     │
│  TextureBase、PipelineStateBase         │
├─────────────────────────────────────────┤
│  平台后端：D3D12 / Vulkan / Metal / AGC  │
└─────────────────────────────────────────┘
```

- **底层接口层**足够薄，移植到新平台时只需实现少量纯虚类。
- **高层业务层**提供面向渲染管线的易用接口，避免上层直接碰底层对象。

**这是 vibe coding 时可以借鉴的结构**：
- 先实现底层接口（`DeviceBase`、`CommandListBase`、`TextureBase`）。
- 再在高层的 `DynamicRHI` 中做后端选择和工具封装（`createBuffer`、`createTexture2D` 等）。
- 如果以后想支持 WebGPU 或新的主机 API，只需在底层加一个新后端，上层代码完全不动。

#### 3.2 多队列与异步计算

现代 GPU 至少有三个硬件队列：
- **Graphics Queue**：负责光栅化绘制。
- **Compute Queue**：负责 Compute Shader、后处理、物理模拟。
- **Copy Queue**：负责 CPU → GPU 内存上传。

工业级 RHI 需要暴露这些队列，并支持**跨队列同步**（如 Fence / Timeline Semaphore）：

```cpp
enum class EQueueType { Graphics, Compute, Copy };

class IRHIDevice {
    virtual IRHICommandBuffer* createCommandBuffer(EQueueType queue) = 0;
    virtual void submit(IRHICommandBuffer* cmd, EQueueType queue) = 0;
    virtual IFence* createFence() = 0;
    virtual void waitForFence(IFence* fence, uint64_t value) = 0;
    virtual void signalFence(IFence* fence, uint64_t value, EQueueType queue) = 0;
};
```

**典型应用场景**：
1. Copy Queue 上传纹理数据 → `signalFence(fence, 1)`。
2. Graphics Queue 绘制场景前 → `waitForFence(fence, 1)`，确保纹理已上传完成。
3. Compute Queue 并行运行 SSAO / TAA → 通过 Fence 与 Graphics Queue 同步。

#### 3.3 Native Pointer 透传与外部库集成

当你的引擎需要集成 DLSS、FSR、NRD、RayTracing Denoiser 等第三方库时，这些库通常需要直接操作底层 API 的原生对象（`ID3D12Resource*`、`VkImage`）。

RHI 应该提供一种**可控的透传机制**：

```cpp
class ITexture {
public:
    virtual void* getNativeHandle() = 0; // D3D12: ID3D12Resource*, Vulkan: VkImage
};
```

> NVIDIA 的 NRD 集成文档也提到了这种模式：如果 RHI 能暴露 Native Pointer，集成层的工作量会大幅减少。

---

## ECS 重构映射：如果源码不是 ECS，到 ECS 该怎么办？

chaos 引擎的 RHI 层本身不直接涉及 ECS（它位于渲染管线的最底层）。但当我们把 RHI 放进自研 ECS 引擎时，必须想清楚：**哪些东西应该是 Component，哪些应该是 System**。

### ECS 映射表

| 传统 OOP 概念 | ECS 表达 | 说明 |
|--------------|---------|------|
| `Mesh` | `MeshComponent` | 持有顶点/索引缓冲句柄（`RHIBufferHandle`） |
| `MaterialInstance` | `MaterialComponent` | 持有 PSO 句柄、纹理句柄列表、uniform 数据 |
| `Camera` | `CameraComponent` | view / projection 矩阵、FOV、近远平面 |
| `RenderWindow` | `WindowComponent` | 窗口句柄、SwapChain 句柄、分辨率 |
| `RenderPipeline::renderDeferredPipeline()` | `RenderSystem` | 查询所有带 `MeshComponent + TransformComponent + MaterialComponent` 的实体，生成 Draw Call |
| `PSOManager` | `PipelineCacheSystem` | 全局资源，作为 World 资源或 Singleton Component |
| `RHICommandBufferPool` | `CommandBufferPool` | 通常作为 `RenderSystem` 的内部成员，不直接是 Component |

### ECS 下的渲染一帧

```cpp
class RenderSystem : public ISystem {
public:
    void tick(World& world, float dt) override {
        auto query = world.query<TransformComponent, MeshComponent, MaterialComponent>();

        auto* cmd = m_rhi->createCommandBuffer();
        cmd->begin();
        cmd->setViewport(m_window_width, m_window_height);

        for (auto [entity, transform, mesh, material] : query) {
            // 更新 per-draw uniform（如 MVP 矩阵）
            material.uniforms.mvp = camera.proj * camera.view * transform.world_matrix;
            m_rhi->updateUniformBuffer(material.uniform_buffer, &material.uniforms);

            cmd->bindPipeline(material.pipeline);
            cmd->bindVertexBuffer(mesh.vertex_buffer);
            cmd->bindIndexBuffer(mesh.index_buffer);
            cmd->drawIndexed(mesh.index_count);
        }

        cmd->end();
        m_rhi->submit(cmd);
        m_swap_chain->present();
    }
};
```

**为什么 ECS 更适合渲染？**
- **Cache Locality**：`MeshComponent` 可以按 `vertex_buffer` 排序，减少 PSO 切换。
- **组合灵活性**：一个实体可以有 `MeshComponent + MaterialComponent`，也可以额外加上 `ShadowCasterComponent`、`TransparentComponent`，无需继承。
- **AI 可观测性**：`RenderSystem` 的输入（所有 Component 数据）完全平铺、可序列化。AI Agent 可以精确知道"这一帧要画什么"。

---

## AI 友好设计红线检查

| 检查项 | RHI 层的实现策略 |
|--------|-----------------|
| **状态平铺** | `MeshComponent`、`MaterialComponent` 直接存储 `RHIBufferHandle`、`RHIPipelineHandle` 等平铺句柄，避免深层指针嵌套。 |
| **自描述** | 所有 `TextureDesc`、`PipelineDesc`、`BufferDesc` 都通过反射注册到 `TypeRegistry`。AI 可以在不读头文件的情况下知道每个字段的含义。 |
| **确定性** | 给定相同的 ECS 查询结果，`RenderSystem` 必须按**确定性顺序**提交 Draw Call（例如按材质句柄排序）。这保证了同一世界状态在回放时产生相同的命令序列和像素输出。 |
| **工具边界** | AgentBridge 可以提供 `query_render_stats()` 和 `capture_frame()` 工具，返回本帧的 Draw Call 数量、PSO 切换次数、渲染目标列表等结构化 JSON。 |
| **Agent 安全** | RHI 层不提供"直接删除底层资源"的 Agent 接口。所有资源销毁走 `ResourceManager` 的引用计数或帧延迟释放队列，防止 AI 误删正在 GPU 上使用的纹理。 |

---

## 设计权衡表

| 决策 | 选项 A | 选项 B | 推荐 |
|------|--------|--------|------|
| Barrier 管理 | RHI 自动推导 | 交给上层 RenderGraph | **小型引擎选 A，工业级选 B**。RHI 自动推导简单但不够灵活；RenderGraph 能全局优化 Barrier。 |
| SwapChain 位置 | 在 RHI 内 | 在 platform 层 | **在 RHI 内**。SwapChain 与底层 API 强耦合（DXGI / VkSwapchainKHR）。 |
| 多线程命令 | 每个线程独立创建 | 从池中分配 | **用池**。减少分配器开销，且便于统一追踪。 |
| Shader 编译 | 运行时编译 | 离线编译为二进制 | **离线编译**。启动时间和运行时卡顿都会大幅改善。 |
| PSO 缓存键 | 64 位 Hash | 完整结构体比较 | **64 位 Hash**。碰撞概率极低，查找速度极快。 |

---

## 如果我要 vibe coding，该偷哪几招？

1. **句柄 + 工厂模式**：用 `uint32_t` 句柄隐藏底层资源，上层代码彻底与平台解耦。
2. **命令缓冲池**：多线程渲染的基础装备，不要让每个线程自己 `new/delete`。
3. **PSO 全局缓存**：D3D12/Vulkan 下避免编译卡顿的救命稻草，用 Hash 键做并发查表。
4. **双层 RHI 架构**：底层薄、高层厚。底层只负责"翻译"，高层负责"易用"。
5. **Native Pointer 透传**：为将来集成 DLSS/FSR/NRD 预留后门，但不要滥用。
6. **确定性渲染**：`RenderSystem` 中对 Draw Call 排序，确保回放一致性——这对网络同步和 AI 调试都至关重要。

---

> **下一步预告**：[[资源管理]] —— RHI  abstraction 已经到位，但纹理、网格、着色器的生命周期谁来管理？异步加载、热重载、引用计数，这些都是让引擎从"能跑"走向"能迭代"的关键。
