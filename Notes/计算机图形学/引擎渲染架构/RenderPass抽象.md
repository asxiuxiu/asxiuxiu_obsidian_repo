---
title: RenderPass抽象
description: RHI 把 gl* 藏起来了，但一帧里还有场景 Pass、后处理 Pass、阴影 Pass。理解 RenderPass 抽象：如何表达 Pass 的输入/输出、依赖关系，以及它与 ECS System 的最小映射。
date: 2026-06-28
tags:
  - graphics
  - render-pass
  - rendering-architecture
  - rhi
  - engine-architecture
  - ecs
  - render-graph
aliases:
  - Render Pass Abstraction
  - Pass抽象
  - 渲染Pass抽象
---

> **前置依赖**：[[Notes/计算机图形学/引擎渲染架构/为什么需要渲染抽象|为什么需要渲染抽象]] — 你已经理解 RHI 的价值，能把绘制命令抽象成 `ICommandList`
> **本模块增量**：学完这篇笔记后，你能设计一个最小 RenderPass 抽象，表达 Pass 的输入/输出和依赖关系，并把它映射到 ECS System 上。
> **下一步**：[[Notes/计算机图形学/引擎渲染架构/DrawCall与合批|DrawCall与合批]] — Pass 里的 Draw Call 太多，怎么合并？

---

# RenderPass 抽象

## 问题 0：RHI 有了，但一帧不只有一个 Pass

在 [[Notes/计算机图形学/引擎渲染架构/为什么需要渲染抽象|为什么需要渲染抽象]] 里，我们把 `glUseProgram`、`glDrawElements` 这些调用封装进了 `ICommandList`。现在上层代码可以说：

```cpp
cmd->SetPipeline(pso);
cmd->SetVertexBuffer(vb);
cmd->DrawIndexed(count);
```

但一帧的渲染不是一次 `DrawIndexed` 就能完成的。以最简单的“场景 + 后处理”为例：

```
Pass 1：场景 Pass —— 画 3D 物体到场景纹理
Pass 2：后处理 Pass —— 对场景纹理做色调映射，画到屏幕
```

更复杂一点：

```
Pass 1：Shadow Pass —— 画阴影深度图
Pass 2：GBuffer Pass —— 画 Albedo/Normal/Depth
Pass 3：Lighting Pass —— 读 GBuffer 和 ShadowMap，输出光照结果
Pass 4：Bloom Pass —— 提取高光、模糊
Pass 5：ToneMapping Pass —— 合并 Bloom 和 SceneColor，输出到屏幕
```

**每个 Pass 都涉及三个问题**：
1. **输出到哪**：FBO、默认帧缓冲、ShadowMap、GBuffer……
2. **读什么输入**：上一 Pass 的纹理、外部资源、常量缓冲……
3. **画什么**：场景几何、全屏四边形、Compute Shader……

如果这些问题都由上层代码直接写死，引擎很快会变成一团意大利面。RHI 解决的是“怎么调用 API”，但**它没解决一帧里多个 Pass 怎么组织**。

这就是 RenderPass 抽象要回答的问题。

---

## 问题 1：最 naive 的方案——硬编码一帧的绘制顺序

假设你已经有了 RHI，最直接的写法是：在 `RenderFrame()` 里按顺序手写每个 Pass。

```cpp
void RenderFrame() {
    auto* cmd = device->CreateCommandList();

    // Pass 1：场景 Pass
    cmd->BeginRenderTarget(sceneRT);
    cmd->Clear({0,0,0,1}, true);
    for (auto& obj : sceneObjects) {
        cmd->SetPipeline(obj.material->pso);
        cmd->SetVertexBuffer(obj.mesh->vb);
        cmd->DrawIndexed(obj.mesh->indexCount);
    }
    cmd->EndRenderTarget();

    // Pass 2：后处理 Pass
    cmd->BeginRenderTarget(screenRT);
    cmd->Clear({0,0,0,1}, false);
    cmd->SetPipeline(postProcessShader);
    cmd->BindTexture(0, sceneRT.colorTexture);
    cmd->DrawIndexed(6);  // 全屏四边形
    cmd->EndRenderTarget();

    device->Submit(cmd);
    device->Present();
}
```

**立刻发现的问题**：

- **新增 Pass 要改核心函数**：每加一个阴影 Pass、Bloom Pass，都要跑到 `RenderFrame()` 里插入代码。这个函数会无限膨胀。
- **输入输出关系隐式表达**：后处理 Pass 读 `sceneRT.colorTexture` 这件事，藏在 `BindTexture(0, ...)` 这一行里。没有地方显式声明“这个 Pass 依赖场景 Pass 的输出”。
- **无法动态开关**：如果想做一个调试模式，只画 SceneColor 不做后处理，需要写 `#if DEBUG` 或者加一堆 `if`。
- **与现代 API 不对齐**：Vulkan 的 `VkRenderPass`、D3D12 的 `OMSetRenderTargets` 都要求你显式描述 Pass 的附件结构。硬编码写法把这些信息分散在代码各处，换后端时还是要重写。

**结论**：我们需要一个显式的“Pass”对象，把输入、输出、执行体封装在一起。

---

## 问题 2：RenderPass 到底是什么？

一个 **RenderPass（渲染通道）** 是渲染管线中的最小执行单元。它只描述三件事：

```
输入  →  [Pass 执行体]  →  输出
```

| 要素 | 含义 | 示例 |
|------|------|------|
| **输入** | 本 Pass 需要读取的资源 | 上一 Pass 的颜色纹理、ShadowMap、GBuffer、Uniform 缓冲 |
| **输出** | 本 Pass 写入的目标 | 颜色附件、深度附件、屏幕 |
| **执行体** | 实际录制绘制命令的回调 | 画场景几何、画全屏四边形、Dispatch Compute |

**关键认知**：Pass 不直接调用 `glDraw*` 或 `vkCmdDraw*`。Pass 只在被调度时，向 `ICommandList` 写入命令。真正决定“什么时候执行、资源状态怎么转”的是上层的调度器。

最小抽象可以长这样：

```cpp
struct RenderPassDesc {
    const char* name;
    std::vector<TextureHandle> colorOutputs;   // 颜色输出附件
    TextureHandle depthOutput;                 // 深度输出附件（可选）
    std::vector<TextureHandle> inputs;         // 输入纹理
    ClearState clearState;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual const RenderPassDesc& GetDesc() const = 0;
    virtual void Execute(ICommandList* cmd) = 0;
};
```

**状态变化图**：

```
没有 RenderPass 抽象：
  RenderFrame() 直接操作 ICommandList 和 RenderTarget
  输入输出关系散落在 BindTexture / BeginRenderTarget 调用里

有了 RenderPass 抽象：
  RenderFrame() 持有 RenderPass 列表
        │
        ▼
  每个 RenderPass 声明 inputs / outputs
        │
        ▼
  调度器根据依赖排序，逐个调用 RenderPass::Execute(cmd)
        │
        ▼
  每个 Pass 内部只关心“我要画什么”
```

---

## 问题 3：怎么表达 Pass 之间的依赖关系？

依赖关系不是额外加字段，而是**从输入/输出中自然推导出来的**。

```
Shadow Pass    写入 → shadowMap
Lighting Pass  读取 → shadowMap

因此：Lighting Pass 依赖于 Shadow Pass
```

```cpp
class RenderPassRegistry {
    std::vector<std::unique_ptr<IRenderPass>> passes;

public:
    void AddPass(std::unique_ptr<IRenderPass> pass) {
        passes.push_back(std::move(pass));
    }

    // 根据 inputs/outputs 推导依赖图
    std::vector<IRenderPass*> TopologicalSort() const {
        // 构建图：如果 Pass B 读取了 Pass A 写入的资源，则 A → B
        // 用 Kahn 算法拓扑排序
        // ...
    }
};
```

**为什么不用硬编码顺序？**

因为硬编码顺序一旦写错，就会出现“读还没写好的纹理”这种时序 bug。而由输入/输出推导顺序，能自动保证：**一个资源被写入之后，才会被读取**。

**最小依赖推导示例**：

```cpp
// 假设注册三个 Pass
registry.AddPass(std::make_unique<ShadowPass>());   // 输出 shadowMap
registry.AddPass(std::make_unique<ScenePass>());    // 输出 sceneColor
registry.AddPass(std::make_unique<ToneMapPass>());  // 输入 sceneColor，输出 screen

// 推导出的依赖图：
// ShadowPass
//
// ScenePass ──► ToneMapPass
//                ▲
//                │ （如果 ToneMap 也读 ShadowMap，ShadowPass 才会有箭头指向 ToneMapPass）
```

> 注意：ShadowPass 和 ScenePass 之间没有依赖，可以并行录制命令（如果 RHI 支持多上下文）。这是 RenderPass 抽象带来的第二个好处：并行性从依赖图中自然浮现。

---

## 问题 4：Pass 怎么注册？怎么让上层扩展？

引擎不可能把所有 Pass 都写死在核心代码里。我们需要一个**注册系统**，让上层模块（后处理、UI、阴影、Debug 线框）都能往管线里加 Pass。

### 分支 A：函数注册

```cpp
using PassFactory = std::function<std::unique_ptr<IRenderPass>()>;

class RenderPipeline {
    std::vector<PassFactory> factories;

public:
    void RegisterPass(PassFactory factory) {
        factories.push_back(factory);
    }

    void Build() {
        for (auto& factory : factories) {
            passes.push_back(factory());
        }
    }
};

// 上层模块注册
pipeline.RegisterPass([]() { return std::make_unique<ScenePass>(); });
pipeline.RegisterPass([]() { return std::make_unique<BloomPass>(); });
pipeline.RegisterPass([]() { return std::make_unique<ToneMapPass>(); });
```

**优点**：简单，C++ 原生支持。

**缺点**：每个 Pass 的输入输出还是隐式的——工厂函数创建 Pass 对象时才知道。

### 分支 B：声明式注册

每个 Pass 在注册时就声明输入输出，调度器据此自动推导依赖。

```cpp
struct PassRegistration {
    const char* name;
    std::vector<const char*> inputs;   // 资源名，如 "SceneColor"
    std::vector<const char*> outputs;  // 资源名，如 "BloomOutput"
    std::function<void(ICommandList*)> execute;
};

pipeline.RegisterPass({
    .name = "Bloom",
    .inputs = {"SceneColor"},
    .outputs = {"BloomOutput"},
    .execute = [](ICommandList* cmd) { /* ... */ }
});
```

**优点**：依赖关系显式、可静态检查、方便做可视化工具。

**缺点**：资源名需要全局约定，Pass 之间的接口契约变严格。

### 个人项目推荐

> **学习阶段**：用分支 A（函数注册）快速跑通。
>
> **引擎阶段**：用分支 B（声明式注册），因为这是 RenderGraph 的雏形。

---

## 问题 5：RenderPass 与 ECS System 怎么映射？

在 ECS 架构下，渲染管线不是“一个巨大的 RenderFrame 函数”，而是一组 System。

**最自然的映射方式**：

| 概念 | ECS 映射 |
|------|---------|
| 一个 RenderPass | 一个 Render World 的 System（或一个 System 负责的 Pass 列表） |
| Pass 的输入/输出 | 渲染资源句柄（`TextureHandle`、`RenderTargetHandle`），作为 ECS Resource 或组件数据 |
| Pass 的执行体 | System 的 `Update()` 函数，向 `ICommandList` 写入命令 |
| Pass 注册 | 插件系统在启动时注册渲染 System |

示例：

```cpp
// ScenePass 对应一个 ECS System
class SceneRenderPassSystem : public System {
public:
    void Update(World* world, ICommandList* cmd) {
        // 查询所有带 MeshRenderer + Transform 的实体
        auto query = world->Query<MeshRenderer, Transform>();

        cmd->BeginRenderTarget(world->Resource<SceneTextures>()->sceneColorRT);
        cmd->Clear({0,0,0,1}, true);

        for (auto [e, mesh, transform] : query) {
            cmd->SetPipeline(mesh.material->pso);
            cmd->SetVertexBuffer(mesh.vertexBuffer);
            cmd->SetIndexBuffer(mesh.indexBuffer);
            cmd->SetConstantBuffer(0, transform.matrix);
            cmd->DrawIndexed(mesh.indexCount);
        }

        cmd->EndRenderTarget();
    }
};
```

**关键设计点**：

- System 不直接拥有 CommandList 的生命周期。CommandList 由渲染管线在每一帧分配，按 Pass 顺序传给各个 System。
- System 之间不直接调用，而是通过共享的 ECS Resource（如 `SceneTextures`）传递资源句柄。
- 这种映射让 RenderPass 天然支持 ECS 的查询、组件遍历和插件扩展。

---

## 问题 6：最小可运行实现

下面是一个完整的、从 RHI 到 RenderPass 的最小迁移示例。

### Step 1：定义 Pass 描述和接口

```cpp
struct RenderPassDesc {
    const char* name;
    std::vector<TextureHandle> colorOutputs;
    TextureHandle depthOutput = TextureHandle::Invalid();
    std::vector<TextureHandle> inputs;
    ClearState clearState;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual const RenderPassDesc& GetDesc() const = 0;
    virtual void Execute(ICommandList* cmd) = 0;
};
```

### Step 2：实现两个具体 Pass

```cpp
class ScenePass : public IRenderPass {
    RenderPassDesc desc;

public:
    ScenePass(RenderTargetHandle sceneRT) {
        desc.name = "Scene";
        desc.colorOutputs = { sceneRT.colorTexture };
        desc.depthOutput = sceneRT.depthTexture;
        desc.clearState = ClearState::ColorAndDepth({0.1f,0.1f,0.1f,1.0f});
    }

    const RenderPassDesc& GetDesc() const override { return desc; }

    void Execute(ICommandList* cmd) override {
        cmd->BeginRenderTarget(sceneRT, desc.clearState);

        for (auto& obj : g_sceneObjects) {
            cmd->SetPipeline(obj.material->pipeline);
            cmd->SetVertexBuffer(obj.mesh->vertexBuffer);
            cmd->SetIndexBuffer(obj.mesh->indexBuffer);
            cmd->DrawIndexed(obj.mesh->indexCount);
        }

        cmd->EndRenderTarget();
    }

private:
    RenderTargetHandle sceneRT;
};

class ToneMapPass : public IRenderPass {
    RenderPassDesc desc;
    RenderTargetHandle screenRT;
    TextureHandle sceneColor;

public:
    ToneMapPass(TextureHandle sceneColor, RenderTargetHandle screenRT) {
        desc.name = "ToneMap";
        desc.inputs = { sceneColor };
        desc.colorOutputs = { screenRT.colorTexture };
        desc.clearState = ClearState::Color({0,0,0,1});

        this->sceneColor = sceneColor;
        this->screenRT = screenRT;
    }

    const RenderPassDesc& GetDesc() const override { return desc; }

    void Execute(ICommandList* cmd) override {
        cmd->BeginRenderTarget(screenRT, desc.clearState);
        cmd->SetPipeline(g_toneMapPipeline);
        cmd->BindTexture(0, sceneColor);
        cmd->DrawIndexed(6);  // 全屏四边形
        cmd->EndRenderTarget();
    }
};
```

### Step 3：调度器按依赖执行

```cpp
class SimpleRenderPipeline {
    std::vector<std::unique_ptr<IRenderPass>> passes;

public:
    void AddPass(std::unique_ptr<IRenderPass> pass) {
        passes.push_back(std::move(pass));
    }

    void Execute(ICommandList* cmd) {
        // 简化版：按注册顺序执行
        // 完整版：根据 GetDesc().inputs/outputs 拓扑排序
        for (auto& pass : passes) {
            pass->Execute(cmd);
        }
    }
};

// 使用
void RenderFrame(World* world, IRenderDevice* device) {
    auto* cmd = device->CreateCommandList();

    SimpleRenderPipeline pipeline;
    pipeline.AddPass(std::make_unique<ScenePass>(sceneRT));
    pipeline.AddPass(std::make_unique<ToneMapPass>(sceneRT.colorTexture, screenRT));

    pipeline.Execute(cmd);

    device->Submit(cmd);
    device->Present();
}
```

> **注意**：这个最小实现没有自动依赖推导，而是按注册顺序执行。它先把“Pass 作为独立对象”这件事跑通。拓扑排序和自动 Barrier 会在 [[Notes/SelfGameEngine/渲染管线与画面/RenderGraph与多Pass资源管理|RenderGraph与多Pass资源管理]] 中系统实现。

---

## 问题 7：API 对照——不同图形 API 怎么表达 Pass？

我们在解决的是“**如何描述一个渲染 Pass 的输入输出**”这个问题。不同 API 选择了不同的抽象：

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| 输出目标切换 | `glBindFramebuffer` | `vkCmdBeginRenderPass` + `VkFramebuffer` | `OMSetRenderTargets` |
| 输出目标描述 | FBO 附件 | `VkRenderPass` + `VkFramebuffer` | `D3D12_CPU_DESCRIPTOR_HANDLE` 数组 |
| 输入纹理绑定 | `glActiveTexture` + `glBindTexture` | `vkCmdBindDescriptorSets` | `SetGraphicsRootDescriptorTable` |
| 自动状态转换 | 无（全局状态机） | `SubpassDependency` | 手动 `ResourceBarrier` |
| 现代简化 | — | `VK_KHR_dynamic_rendering` | — |

**关键差异**：

- **OpenGL** 没有“Pass”对象，只有 `glBindFramebuffer`。所谓 Pass，就是“绑定一个 FBO，画一堆东西，再解绑”。这也是为什么最小 RenderPass 抽象在 OpenGL 后端很容易实现：每个 `BeginRenderTarget` 就是一个 Pass。
- **Vulkan** 的 `VkRenderPass` 是一个重量级的预编译对象，描述了附件格式、LoadOp/StoreOp、Subpass 依赖。工业引擎通常用 RenderGraph 自动生成它。
- **D3D12** 的 Pass 是运行时的描述符集合（RTV/DSV），Barrier 需要手动插入。

**个人项目推荐**：
- 学习阶段：用 OpenGL 的“FBO 绑定 = Pass”心智模型。
- 引擎阶段：设计自己的 `RenderPassDesc`，让 OpenGL/Vulkan/D3D12 后端各自翻译。Vulkan 后端可以先用 `VK_KHR_dynamic_rendering` 降低复杂度，避免一开始就被 `VkRenderPass` 的 boilerplate 淹没。

---

## 问题 8：从最小 RenderPass 到 RenderGraph

最小 RenderPass 抽象已经能组织一帧的 Pass，但它还有几个问题：

- **依赖顺序是手写的**：`SimpleRenderPipeline` 按注册顺序执行，没有自动推导。
- **中间资源生命周期没管理**：每个 Pass 自己创建纹理，容易内存泄漏或重复分配。
- **Barrier 没自动推导**：在 Vulkan/D3D12 上，Pass 之间需要手动插入 `ResourceBarrier`。

**工业级方案就是 RenderGraph**。RenderGraph 把一帧所有 Pass 和资源建模成有向无环图（DAG）：

```
ShadowMap ──► Lighting ──┐
                         ▼
GBuffer ────► Lighting ──► ToneMap ──► Screen
                         ▲
Bloom ──────►────────────┘
```

**RenderGraph 能做三件事**：
1. **自动拓扑排序**：根据输入输出推导执行顺序
2. **死 Pass 剔除**：如果一个 Pass 的输出没有被后续 Pass 读取，直接跳过
3. **自动 Barrier 推导**：在 Vulkan/D3D12 上自动插入资源状态转换

SelfGameEngine 的 [[Notes/SelfGameEngine/渲染管线与画面/RenderGraph与多Pass资源管理|RenderGraph与多Pass资源管理]] 已经深入讨论了这些。本篇的最小 RenderPass 抽象，就是 RenderGraph 的“前置故事片”。

---

## 与 SelfGameEngine 的关系

这篇笔记对应引擎 **阶段 5.4 “渲染一帧的生命周期”** 的入口。

SelfGameEngine 的相关笔记分工：
- [[Notes/SelfGameEngine/渲染管线与画面/RHI抽象层与命令模型|RHI抽象层与命令模型]]：解决“单个 Draw Call 怎么跨后端”
- [[Notes/SelfGameEngine/渲染管线与画面/RenderGraph与多Pass资源管理|RenderGraph与多Pass资源管理]]：解决“多 Pass 之间的资源依赖和 Barrier 自动推导”
- [[Notes/SelfGameEngine/渲染管线与画面/后处理栈架构|后处理栈架构]]：解决“后处理效果怎么组织成 Pass 链”
- [[Notes/SelfGameEngine/渲染管线与画面/ECS架构下的渲染世界设计|ECS架构下的渲染世界设计]]：解决“渲染 System 怎么在 ECS 中运行”

本篇图形学笔记回答的是：**在学习阶段，如何把零散的 Pass 抽象成可注册、可扩展的对象？以及它与 ECS System 的最短映射路径是什么？**

---

## 个人项目推荐

| 阶段 | 推荐做法 |
|------|---------|
| 学习阶段 | 在 `RenderFrame()` 里硬编码 Pass 顺序，但把每个 Pass 的输入输出注释清楚 |
| 引擎原型 | 实现 `IRenderPass` + `SimpleRenderPipeline`（按注册顺序执行），把 Pass 注册到 ECS System |
| 需要动态管线 | 升级到声明式注册，资源用字符串名约定 |
| 长期工业级 | 实现 RenderGraph：拓扑排序、死 Pass 剔除、自动 Barrier、瞬态资源别名 |

---

## 本模块还缺什么？

| 已理解 | 待实践 |
|--------|--------|
| RenderPass 的三要素（输入/输出/执行体） | 自动依赖推导与拓扑排序 |
| Pass 注册系统 | 死 Pass 剔除 |
| ECS System 映射 | 自动 Barrier 推导 |
| 最小可运行实现 | 与现代 API（Vulkan/D3D12）对齐 |

> **下一步**：[[Notes/计算机图形学/引擎渲染架构/DrawCall与合批|DrawCall与合批]] — Pass 里的 Draw Call 太多，怎么合并？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
