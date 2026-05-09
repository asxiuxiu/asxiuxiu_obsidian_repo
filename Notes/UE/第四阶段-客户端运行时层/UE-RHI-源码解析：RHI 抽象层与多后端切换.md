---
title: UE-RHI-源码解析：RHI 抽象层与多后端切换
date: 2026-05-09
tags:
  - ue-source
  - engine-architecture
  - rhi
  - rendering
aliases:
  - UE-RHI-源码解析
  - RHI 抽象层与多后端切换
---

> [[Notes/UE/00-UE全解析主索引.md|← 返回 UE 全解析主索引]]

# RHI 抽象层与多后端切换

## 引言：为什么需要一个"渲染硬件接口"？

想象你要写一款能在 Windows、Linux、macOS、iOS、Android 上运行的游戏。每个平台的底层图形 API 都不一样——Windows 上可能是 DirectX 12 或 Vulkan，macOS/iOS 上是 Metal，Android 上是 Vulkan 或 OpenGL ES，有些服务器甚至没有显卡。

如果渲染代码直接调用 `ID3D12Device::CreateCommittedResource` 或者 `vkCreateBuffer`，那每支持一个平台就要重写一套渲染系统。更麻烦的是，现代 GPU 编程模型差异巨大：D3D12 和 Vulkan 用显式的命令列表和 Barrier，OpenGL 是全局状态机，Metal 又有自己的资源编码方式。

**RHI（Render Hardware Interface）** 就是 UE 用来解决这个问题的层。它像一座桥：上层（Renderer、RenderCore）只说一种"通用语言"，RHI 负责把这种语言翻译成各平台 GPU 能听懂的具体指令。而且 UE 的 RHI 不止做了简单的 API 翻译——它还承担了**跨线程命令录制**、**多后端动态切换**、**GPU 资源生命周期管理**等更复杂的职责。

---

## 模块定位

RHI 模块位于 `Engine/Source/Runtime/RHI/`，是 UE 渲染栈的最底层之一，直接坐落在各平台具体的图形驱动之上。

| 属性 | 内容 |
|------|------|
| 模块名 | `RHI` |
| 构建文件 | `RHI/RHI.Build.cs` |
| 公共接口 | `RHI/Public/*.h`（~50+ 头文件） |
| 平台实现 | `RHI/Private/Windows/`、`Linux/`、`Android/`、`Apple/` |
| 下游消费者 | `RenderCore`、`Renderer`、`Engine`（通过 SceneProxy / MeshBatch） |
| 运行时加载的后端 | `D3D11RHI`、`D3D12RHI`、`VulkanRHI`、`OpenGLDrv`、`MetalRHI`、`NullDrv` |

从 `RHI.Build.cs` 可以看到一个关键设计：RHI 模块本身**静态链接**到引擎核心（依赖 `Core`、`TraceLog`、`ApplicationCore`），但各后端（`D3D12RHI`、`VulkanRHI` 等）是**动态加载模块**（`DynamicallyLoadedModuleNames`）。这意味着引擎启动时，RHI 先加载，然后根据平台配置和硬件能力，动态挑选并加载一个具体的后端 DLL。

---

## 问题链：从 naive 方案到 UE 的设计演进

### 问题 0：直接调用平台 API 行不行？

最直观的方案：Windows 用 D3D12，Linux 用 Vulkan，写一堆 `#ifdef PLATFORM_WINDOWS`。每个 `CreateBuffer`、`DrawPrimitive`、`Present` 都直接调原生 API。

**立刻暴露的问题**：
- 代码里到处都是平台分支，维护噩梦。
- D3D12 要求显式管理命令列表和同步，OpenGL 没有这些概念，无法统一抽象。
- 现代引擎需要多线程录制渲染命令，但 D3D12 的 CommandList 不是线程安全的（需要仔细规划），OpenGL 上下文只能绑定到一个线程。
- 不同 API 的资源对象生命周期管理完全不同。

**结论**：必须有一层统一抽象，把"做什么"和"怎么做"彻底分开。

---

### 问题 1：如何统一各平台的"做什么"？→ FDynamicRHI 桥接层

UE 的答案是 `FDynamicRHI`——一个纯虚接口类，定义了所有 GPU 操作的统一契约。

> 文件：`RHI/Public/DynamicRHI.h`，第 205~1000 行

```cpp
class FDynamicRHI
{
public:
    virtual void Init() = 0;
    virtual void Shutdown() = 0;
    virtual const TCHAR* GetName() = 0;

    // 创建各类 GPU 资源
    virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) = 0;
    virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer) = 0;
    virtual FTextureRHIRef RHIAsyncCreateTexture2D(...) = 0;
    virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, ...) = 0;

    // 命令上下文与提交
    virtual IRHICommandContext* RHIGetDefaultContext() = 0;
    virtual IRHIComputeContext* RHIGetCommandContext(ERHIPipeline Pipeline, FRHIGPUMask GPUMask) = 0;
    virtual void RHIFinalizeContext(FRHIFinalizeContextArgs&& Args, TRHIPipelineArray<IRHIPlatformCommandList*>& Output) = 0;
    virtual void RHISubmitCommandLists(FRHISubmitCommandListsArgs&& Args) = 0;

    // ... 数十个纯虚接口
};
```

`FDynamicRHI` 的设计思路是**"最大公约数"**：接口签名取各主流 API 能力的交集，并向下兼容。比如：
- 所有平台都支持创建纹理、缓冲、着色器、管线状态。
- 不是所有平台都支持光追，所以 `RHICreateRayTracingGeometry` 有默认空实现（`checkNoEntry()`），支持光追的后端（D3D12、Vulkan）去覆盖它。
- 不是所有平台都支持 Mesh Shader，所以 `RHICreateMeshShader` 返回空引用作为默认行为。

全局只有一个 `GDynamicRHI` 指针（`DynamicRHI.cpp` 第 35 行），运行时指向当前激活的后端实例。上层的所有操作最终都通过 `GDynamicRHI->RHIxxx(...)` 派发。

这种设计就是经典的**桥接模式（Bridge Pattern）**：抽象（`FDynamicRHI`）与实现（`FD3D12DynamicRHI`、`FVulkanDynamicRHI` 等）分离，两者可以独立演进。

---

### 问题 2：渲染线程不能阻塞 GPU，怎么办？→ 命令列表与延迟执行

游戏引擎有一个核心矛盾：**CPU 要准备下一帧的数据，GPU 还在执行上一帧的指令**。如果 CPU 每发一条 GPU 命令都同步等待，帧率直接崩溃。

UE 的解决方案是**命令列表（Command List）模式**：渲染线程（Render Thread）不负责直接调用 GPU API，而是把命令**录制**到一个内存队列里，再由专门的 RHI 线程（或任务）**异步执行**这些命令。

#### 命令列表的继承体系

> 文件：`RHI/Public/RHICommandList.h`

```
FRHICommandListBase          ← 基类，管理内存分配器和命令链表
  └── FRHIComputeCommandList  ← 只发计算/传输命令
        └── FRHICommandList   ← 增加图形绘制命令（Draw、SetRenderTargets 等）
              └── FRHICommandListImmediate  ← 立即执行，直接与 RHI 线程交互
```

`FRHICommandListBase` 内部有一个 `FMemStackBase MemManager`（内存栈分配器）和一个命令链表。所有 RHI 命令都被封装成 `FRHICommandBase` 的子类对象，分配在内存栈上，链成单向链表：

> 文件：`RHI/Public/RHICommandList.h`，第 348~376 行

```cpp
struct FRHICommandBase
{
    FRHICommandBase* Next = nullptr;
    virtual void ExecuteAndDestruct(FRHICommandListBase& CmdList) = 0;
};

template <typename RHICmdListType, typename LAMBDA>
struct TRHILambdaCommand final : public FRHICommandBase
{
    LAMBDA Lambda;
    void ExecuteAndDestruct(FRHICommandListBase& CmdList) override final
    {
        Lambda(*static_cast<RHICmdListType*>(&CmdList));
        Lambda.~LAMBDA();  // 手动析构，因为内存来自栈分配器
    }
};
```

`ALLOC_COMMAND` 宏负责从 `MemManager` 分配内存并 placement-new 命令对象。整个命令列表的录制是**零堆分配**的（除了命令引用的外部资源），性能极高。

#### Top-of-Pipe vs Bottom-of-Pipe

RHI 命令列表有两个关键概念：
- **Top-of-Pipe**：渲染线程正在**录制**命令的状态。此时 `GDynamicRHI` 的接口不会被直接调用，命令被序列化到内存栈。
- **Bottom-of-Pipe**：RHI 线程正在**执行**命令的状态。此时命令的 `ExecuteAndDestruct` 被调用，内部才会真正触碰 `ID3D12CommandList` 或 `VkCommandBuffer`。

`FRHICommandListBase::IsBottomOfPipe()` 的实现很简洁：

```cpp
inline bool IsBottomOfPipe() const
{
    return Bypass() || IsExecuting();
}
```

- `Bypass()`：如果 RHI 线程被禁用（某些平台或调试模式），命令在录制时立即执行。
- `IsExecuting()`：当前正处于 RHI 线程的回放阶段。

#### 命令执行的入口：FRHICommandListExecutor

`FRHICommandListImmediate`（立即命令列表）是特殊的——它不延迟执行，而是直接与当前 RHI 线程交互，支持 `ImmediateFlush`、`Submit` 等操作。`FRHICommandListExecutor` 是全局调度器，负责把普通命令列表分发到 RHI 线程执行。

**这个设计的核心收益**：渲染线程可以连续录制多帧的命令列表而不被 GPU 阻塞；RHI 线程独立运行，把命令翻译并提交给 GPU；两者通过命令队列和 Fence 同步。

---

### 问题 3：怎么在运行时切换 D3D12 / Vulkan / OpenGL？→ DynamicRHI 多后端切换

这是 UE RHI 最有特色的设计之一。引擎启动时，RHI 本身已经加载了，但具体用哪个后端是**运行时决定**的。

#### 后端选择流程（Windows 平台）

> 文件：`RHI/Private/Windows/WindowsDynamicRHI.cpp`

Windows 上的后端选择逻辑非常复杂，因为它要兼顾项目配置、用户偏好、硬件能力、命令行覆盖等多重因素：

```cpp
// Windows 上的 RHI 搜索优先级
const EWindowsRHI GRHISearchOrder[] =
{
    EWindowsRHI::D3D12,
    EWindowsRHI::D3D11,
    EWindowsRHI::Vulkan,
    EWindowsRHI::OpenGL,
};
```

选择流程（`LoadDynamicRHIModule`，第 1147~1241 行）：

1. **解析配置**：读取 `DefaultGraphicsRHI`（项目设置）、`TargetedRHIs`（烘焙目标）、`D3DRHIPreference`（用户设置）。
2. **命令行覆盖**：`-d3d12`、`-vulkan`、`-opengl` 等参数可以强制指定后端。
3. **Feature Level 决策**：根据 GPU 能力和项目配置，决定用 SM5、SM6 还是 ES3_1。
4. **加载模块**：通过 `FModuleManager::LoadModulePtr<IDynamicRHIModule>(ModuleName)` 动态加载后端 DLL。
5. **能力检测**：调用 `DynamicRHIModule->IsSupported(DesiredFeatureLevel)`，后端自我检测是否能在当前硬件上运行。
6. **失败回退**：如果 D3D12 不支持，自动回退到 D3D11；如果 D3D11 也不支持，尝试 Vulkan；最后才是 OpenGL。

```cpp
// PlatformCreateDynamicRHI() 的核心逻辑
FDynamicRHI* PlatformCreateDynamicRHI()
{
    ERHIFeatureLevel::Type RequestedFeatureLevel;
    const TCHAR* LoadedRHIModuleName;
    IDynamicRHIModule* DynamicRHIModule = LoadDynamicRHIModule(RequestedFeatureLevel, LoadedRHIModuleName);

    if (DynamicRHIModule)
    {
        FDynamicRHI* DynamicRHI = DynamicRHIModule->CreateRHI(RequestedFeatureLevel);
        // ...
        return DynamicRHI;
    }
    return nullptr;
}
```

`IDynamicRHIModule` 是后端的工厂接口，只有一个纯虚方法：

```cpp
class IDynamicRHIModule : public IModuleInterface
{
public:
    virtual bool IsSupported(ERHIFeatureLevel::Type DesiredFeatureLevel) = 0;
    virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel) = 0;
};
```

每个后端模块（`D3D12RHI`、`VulkanRHI` 等）都实现这个接口。`CreateRHI()` 返回该后端的 `FDynamicRHI` 子类实例，比如 `FD3D12DynamicRHI`、`FVulkanDynamicRHI`。

#### 初始化全局状态

一旦 `GDynamicRHI` 被创建，`RHIInit()`（`DynamicRHI.cpp` 第 278~417 行）会完成后续初始化：

1. 调用 `GDynamicRHI->Init()` 初始化后端（创建设备、交换链等）。
2. 验证命令列表上下文。
3. 设置默认 GPU Mask（多 GPU 场景）。
4. 提取适配器信息（驱动版本、名称）写入崩溃上下文。
5. 检测黑名单驱动并弹出警告（`RHIDetectAndWarnOfBadDrivers`）。

**这个设计让 UE 具备了极强的平台适应性**：同一套游戏包，在不同玩家的机器上可能跑 D3D12、D3D11 或 Vulkan，完全由运行时环境和硬件能力决定，不需要重新编译。

---

### 问题 4：GPU 资源怎么安全释放？→ FRHIResource 的延迟删除队列

GPU 资源（纹理、缓冲、着色器）有一个棘手的生命周期问题：CPU 侧决定"删除"一个纹理时，GPU 可能还在上一帧的渲染中读取它。如果立即释放，轻则画面闪烁，重则驱动崩溃。

UE 的 `FRHIResource` 设计了**引用计数 + 延迟删除队列**的双重保险。

> 文件：`RHI/Public/RHIResources.h`，第 53~236 行

```cpp
class FRHIResource
{
public:
    inline uint32 AddRef() const
    {
        int32 NewValue = AtomicFlags.AddRef(std::memory_order_acquire);
        return uint32(NewValue);
    }

    inline uint32 Release() const
    {
        int32 NewValue = AtomicFlags.Release(std::memory_order_release);
        if (NewValue == 0)
        {
            MarkForDelete();  // 引用计数归零，但不立即删除
        }
        return uint32(NewValue);
    }

private:
    void MarkForDelete() const;
    static void DeleteResources(TArray<FRHIResource*> const& Resources);
    // ...

    class FAtomicFlags
    {
        std::atomic_uint Packed = { 0 };
        // 低 30 位：引用计数
        // 第 30 位：MarkedForDelete
        // 第 31 位：Deleting
    };
};
```

`FAtomicFlags` 用一个 32 位原子整数打包了三种状态：引用计数（30 位）、"已标记删除"位、"正在删除"位。当 `Release()` 把引用计数降到 0 时，对象不会立即被 `delete`，而是被放入一个**全局延迟删除队列**。

`FRHICommandListExecutor` 会在合适的时机（通常是帧边界或 GPU Fence  signaled 后）批量调用 `DeleteResources()`，真正释放那些 GPU 已不再使用的资源。这确保了 CPU 删除操作不会 race GPU 的执行。

---

## 数据层：核心数据结构解剖

### 1. FRHIResource 家族

`FRHIResource` 是所有 GPU 资源的根基类，子类覆盖了渲染所需的全部资源类型：

| 子类 | 职责 |
|------|------|
| `FRHISamplerState` / `FRHIRasterizerState` / `FRHIDepthStencilState` / `FRHIBlendState` | 固定功能管线状态 |
| `FRHIVertexDeclaration` | 顶点输入布局 |
| `FRHIShader` → `FRHIVertexShader` / `FRHIPixelShader` / `FRHIComputeShader` / `FRHIMeshShader` / `FRHIRayTracingShader` | 各阶段着色器 |
| `FRHIGraphicsPipelineState` / `FRHIComputePipelineState` | 管线状态对象（PSO） |
| `FRHIUniformBuffer` | 常量缓冲 |
| `FRHIViewableResource` → `FRHIBuffer` / `FRHITexture` | 可创建视图（SRV/UAV）的资源 |
| `FRHIView` → `FRHIShaderResourceView` / `FRHIUnorderedAccessView` | 着色器资源视图 |
| `FRHIViewport` / `FRHIGPUFence` / `FRHIRenderQuery` | 交换链、同步、查询 |

所有资源句柄都用 `TRefCountPtr<FRHIResource>` 管理（即 `FTextureRHIRef`、`FBufferRHIRef` 等），确保自动引用计数。

### 2. 命令上下文：IRHIComputeContext 与 IRHICommandContext

> 文件：`RHI/Public/RHIContext.h`

```cpp
class IRHIComputeContext
{
public:
    virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) = 0;
    virtual void RHIDispatchComputeShader(uint32 X, uint32 Y, uint32 Z) = 0;
    virtual void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) = 0;
    virtual void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) = 0;
    // ...
};

class IRHICommandContext : public IRHIComputeContext
{
public:
    virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* State, uint32 StencilRef, bool bApplyAdditionalState) = 0;
    virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) = 0;
    virtual void RHIDrawIndexedPrimitive(...) = 0;
    virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& Info, const TCHAR* Name) = 0;
    virtual void RHIEndRenderPass() = 0;
    // ...
};
```

`IRHIComputeContext` 是计算/传输上下文的基类，`IRHICommandContext` 继承它并扩展图形命令。这种分层设计允许异步计算队列（Async Compute）独立于图形队列运行——D3D12 和 Vulkan 都支持这种硬件特性。

后端会为每个 pipeline（Graphics / AsyncCompute）创建独立的 context 对象。比如 D3D12 后端有 `FD3D12CommandContext`，Vulkan 后端有 `FVulkanCommandListContext`。

### 3. 命令列表内存布局

`FRHICommandListBase` 使用 `FMemStackBase`（内存栈分配器）而非普通堆分配来存储命令。这是因为：
- 命令列表生命周期明确（一帧内录制、执行、丢弃）。
- 栈分配器支持 bulk-free，执行完一帧后一次性释放整页内存，没有碎片化问题。
- 命令对象之间通过 `Next` 指针形成侵入式链表，无需额外容器。

```cpp
class FRHICommandListBase
{
protected:
    FMemStackBase MemManager;      // 栈分配器
    FRHICommandBase** CommandLink; // 链表尾指针
    int32 NumCommands = 0;
    // ...
};
```

---

## 逻辑层：关键调用链追踪

### 调用链 1：后端初始化（Windows 平台）

```
FEngineLoop::PreInit()
  └── RHIInit(bHasEditorToken)
        ├── if (!FApp::CanEverRender()) InitNullRHI();
        │     └── 加载 NullDrv 模块（Dedicated Server 无头模式）
        └── else
              ├── GDynamicRHI = PlatformCreateDynamicRHI()
              │     └── LoadDynamicRHIModule()
              │           ├── ParseWindowsDynamicRHIConfig()   // 读项目配置
              │           ├── ChooseDefaultRHI()               // D3D12 → D3D11 → Vulkan → OpenGL
              │           ├── ChooseForcedRHI()                // 处理 -d3d12 / -vulkan 等命令行
              │           ├── ChooseFeatureLevel()             // SM6 / SM5 / ES3_1
              │           ├── FModuleManager::LoadModulePtr("D3D12RHI")
              │           └── DynamicRHIModule->CreateRHI(SM6) // 返回 FD3D12DynamicRHI*
              ├── GDynamicRHI->Init()                         // 创建 D3D12Device、SwapChain
              └── RHIDetectAndWarnOfBadDrivers()              // 黑名单驱动检测
```

### 调用链 2：一帧内的命令录制与提交

```
Render Thread (录制阶段)
  └── FDeferredShadingSceneRenderer::Render()
        └── FRDGBuilder::Execute()  // RenderGraph 执行
              └── AddPass(...)      // 每个 Pass 录制 RHI 命令
                    └── FRHICommandList::DrawPrimitive(...)
                          └── ALLOC_COMMAND(FRHICommandDrawPrimitive)(...)
                                // 命令对象分配在 MemStack，加入链表

RHI Thread (执行阶段)
  └── FRHICommandListExecutor::ExecuteInner()
        └── while (Cmd = Head)
              ├── Cmd->ExecuteAndDestruct(RHICmdList)
              │     └── 对于 DrawPrimitive：
              │           Context->RHIDrawPrimitive(...)
              │           └── FD3D12CommandContext::RHIDrawPrimitive()
              │                 └── pCommandList->DrawInstanced(...)
              └── Cmd = Cmd->Next
```

### 调用链 3：PSO 创建（跨线程安全）

```
Render Thread
  └── FRHICommandList::CreateGraphicsPipelineState(Initializer)
        └── GDynamicRHI->RHICreateGraphicsPipelineState(Initializer)
              └── FD3D12DynamicRHI::RHICreateGraphicsPipelineState()
                    ├── 查 PipelineStateCache（线程安全哈希表）
                    ├── 缓存命中 → 直接返回已有 PSO
                    └── 缓存未命中 → D3D12SerializeRootSignature / CreateGraphicsPipelineState
                          // PSO 编译可能耗时数毫秒，首次编译会 hitch
```

注意 `RHICreateGraphicsPipelineState` 被标记为 `FlushType: Thread safe`，意味着它可以从渲染线程或并行翻译线程安全调用。但底层 D3D12 的 `CreateGraphicsPipelineState` 本身不是无锁的，所以 D3D12RHI 内部会用锁保护。

---

## 与上下层的关系

### 上层：RenderCore 与 Renderer

- **RenderCore** 定义了 `FRenderResource`（渲染资源的 CPU 侧代理，如 `FTextureResource`、`FVertexBuffer`），以及 `FRDGBuilder`（渲染图）。RenderCore 不直接调 GPU API，而是通过 `FRHICommandList` 发命令。
- **Renderer** 实现具体的渲染管线（延迟渲染、前向渲染、后处理）。它用 `FMeshBatch` 描述一坨要画的物体，用 `FMaterialRenderProxy` 描述材质参数，最终都转化为 `FRHICommandList` 上的 DrawCall。

### 下层：平台后端

- **D3D12RHI**：最复杂的后端之一，实现了显式的 CommandAllocator / CommandList / Fence 管理，支持 Async Compute、Mesh Shader、Ray Tracing。
- **VulkanRHI**：与 D3D12 架构高度同构，也使用显式 CommandBuffer、Pipeline、DescriptorSet。
- **MetalRHI**：针对 Apple 芯片优化，使用 Metal 的 CommandBuffer / RenderCommandEncoder 模型。
- **OpenGLDrv**：传统的状态机封装，无显式 CommandBuffer，命令立即执行。
- **NullDrv**：空实现，用于 Dedicated Server 或 CI 环境。

### 同层邻居

- **ShaderCore**：编译着色器并输出字节码（`TArray<uint8>`），RHI 用这些字节码创建 `FRHIShader`。
- **TraceLog**：RHI 通过 Trace 通道记录 GPU 事件，供 UnrealInsights 分析。

---

## 设计亮点与可迁移经验

### 1. 桥接模式 + 抽象工厂 = 运行时多后端切换

`FDynamicRHI` + `IDynamicRHIModule` + `FModuleManager` 的组合，让"用什么 GPU API"从编译期决策变成了运行时决策。这对跨平台引擎至关重要——同一套二进制可以根据用户硬件自动降级（D3D12 → D3D11 → Vulkan）。

**可迁移经验**：如果你的引擎需要支持多图形 API，不要把 `#ifdef` 散布在渲染代码里。定义一个纯虚接口类，把平台差异完全隔离到独立的模块/DLL 中。

### 2. 命令模式 + 内存栈 = 零开销跨线程录制

`FRHICommand<>` 宏体系 + `FMemStackBase` 让渲染线程可以零堆分配地批量录制命令。命令对象的析构和执行是合一的（`ExecuteAndDestruct`），执行完立即在原地析构，无需二次遍历。

**可迁移经验**：需要跨线程传递大量小对象时，考虑用侵入式链表 + 线性分配器（Bump Allocator），比 `std::vector<std::unique_ptr<Command>>` 快一个数量级。

### 3. 引用计数 + 延迟删除 = GPU-CPU 资源生命周期解耦

`FRHIResource` 的 `Release()` 不直接 `delete`，而是标记延迟删除。这解决了"CPU 侧引用已释放，GPU 还在用"的经典 race condition。

**可迁移经验**：任何 GPU 或异构计算场景，资源的 CPU 侧释放都应该通过延迟队列 + Fence 同步，而不是立即释放。

### 4. 数据驱动的平台能力描述

`FGenericDataDrivenShaderPlatformInfo` 用 bitfield 描述每个着色器平台的能力（`bSupportsRayTracing`、`bSupportsMeshShadersTier0`、`bIsMobile` 等），让高层代码可以查询特性而非硬编码平台判断。

**可迁移经验**：用数据（配置表 / bitfield）描述平台能力，而不是 `#ifdef PLATFORM_XXX`。新增平台时只需添加配置，无需修改业务代码。

---

## 关键源码速查

| 主题 | 文件（相对 `Engine/Source/Runtime/`） | 行号范围 |
|------|--------------------------------------|---------|
| FDynamicRHI 纯虚接口 | `RHI/Public/DynamicRHI.h` | 205~1000 |
| 全局 RHI 初始化 | `RHI/Private/DynamicRHI.cpp` | 278~417 |
| Windows 后端选择 | `RHI/Private/Windows/WindowsDynamicRHI.cpp` | 1147~1241 |
| 命令列表基类 | `RHI/Public/RHICommandList.h` | 454~900 |
| 命令对象基础结构 | `RHI/Public/RHICommandList.h` | 348~376 |
| 计算/图形上下文接口 | `RHI/Public/RHIContext.h` | 256~883 |
| GPU 资源根基类 | `RHI/Public/RHIResources.h` | 53~236 |
| RHI 全局能力与状态 | `RHI/Public/RHIGlobals.h` | 87~300 |
| 模块构建定义 | `RHI/RHI.Build.cs` | 1~61 |

---

## 关联阅读

- **上层渲染架构**：[[UE-RenderCore-源码解析：渲染图与渲染线程]]（RenderGraph、RDGBuilder、RHI 命令提交）
- **D3D12 后端实现**：[[UE-D3D12RHI-源码解析：D3D12 后端实现]]
- **Vulkan 后端实现**：[[UE-VulkanRHI-源码解析：Vulkan 后端实现]]
- **渲染一帧完整链路**：[[UE-专题：渲染一帧的生命周期]]（World::Tick → RenderGraph → RHICommandList → SwapChain）
- **多线程专题**：[[UE-专题：多线程与任务系统]]（Render Thread、RHI Thread、TaskGraph 的关系）
- **材质与着色器编译**：[[UE-专题：材质与着色器编译链路]]（ShaderCore → ShaderCompileWorker → DDC → RHI）

---

## 索引状态

- **所属阶段**：第四阶段 4.1 渲染与表现管线
- **索引对应**：`[[UE-RHI-源码解析：RHI 抽象层与多后端切换]]`
- **分析覆盖**：接口层（FDynamicRHI、命令列表体系、资源抽象）→ 数据层（FRHIResource、内存栈、上下文）→ 逻辑层（多后端切换、命令执行链路、PSO 缓存）
- **索引状态更新**：⬜ → ✅
