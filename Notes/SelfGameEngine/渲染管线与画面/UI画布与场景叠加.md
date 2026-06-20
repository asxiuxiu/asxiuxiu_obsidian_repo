---
order: 47
title: UI 画布与场景叠加
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - ui
  - canvas
  - overlay
aliases:
  - UI 画布与场景叠加
---

> **前置依赖**：[[2D渲染基础与批次合批]]、[[RenderGraph与多Pass资源管理]]
> **本模块增量**：理解 Screen/World/Viewport 三种画布空间、多画布系统、RenderGraph 节点插入策略，以及后处理与 UI 的隔离机制。你将明确阶段 5 自研 2D 与阶段 6 自研 UI 框架的边界。
>
> 本笔记探讨的核心问题是：**2D UI 和 3D 场景在同一帧内共存时，坐标系怎么转换？渲染顺序怎么安排？后处理会不会把 UI 也糊掉？**

## 问题 4：UI 画布——屏幕空间与世界空间

2D 渲染不是单一平面。游戏中的 UI 至少出现在三种空间：

1. **屏幕空间（Screen Space）**：HUD、主菜单、暂停面板——它们跟随屏幕，不随相机移动。
2. **世界空间（World Space）**：角色头顶的血条、地面上的标记箭头、全息投影——它们在 3D 场景中有固定位置，随相机移动而相对运动。
3. **局部空间（Local Space）**：某些 UI 元素依附于 3D 物体表面（如驾驶舱仪表盘、游戏内手机屏幕），需要跟随物体的局部坐标系。

### 场景与根因

如果你只有一种"2D 渲染系统"，默认按屏幕空间处理，那么世界空间血条会面临困境：血条需要知道它在 3D 世界中的位置，每帧转换成屏幕坐标，还要处理"被墙挡住时不显示"、"距离太远时缩小"等规则。如果把这些逻辑硬塞进屏幕空间渲染器，代码会很快腐化。

**根因：不同空间的 UI 有不同的几何来源（屏幕像素 vs 世界坐标）、不同的裁剪规则（屏幕矩形裁剪 vs 视锥剔除）、不同的遮挡关系（UI 层级 vs 深度缓冲）。强行统一会丢失关键语义。**

### 分支研究

#### 分支 A：单一画布，所有 2D 元素统一按屏幕空间处理

**核心思路**：即使是世界空间 UI，也在 CPU 端预先将世界坐标转换为屏幕坐标，然后作为普通 2D 元素绘制。

**适用场景**：极其简单的世界空间 UI（如少量血条），且不需要遮挡检测。

**隐藏代价**：
- **遮挡信息丢失**。世界空间 UI 被场景物体遮挡时应该隐藏（或变透明），但屏幕空间渲染器不知道 3D 深度信息。
- **透视变形问题**。当世界空间 UI 位于屏幕边缘时，透视投影会产生非线性变形，单纯投影到屏幕坐标无法保留 3D 朝向。
- **分辨率依赖**。屏幕坐标与窗口分辨率绑定，窗口大小变化时需要重新计算所有世界空间 UI 的位置。

**失效条件**：任何需要精确 3D 定位、遮挡检测、或面向相机的世界空间 UI。

#### 分支 B：多画布系统（Multi-Canvas）

**核心思路**：引擎支持多种画布类型，每种画布有自己的坐标系、投影矩阵和渲染时机。

```cpp
enum class CanvasSpace {
    Screen,     // 屏幕空间：正交投影，像素坐标
    World,      // 世界空间：透视投影，世界坐标
    Viewport,   // 视口空间：归一化设备坐标（NDC），常用于全屏特效 UI
};

struct Canvas {
    CanvasSpace space;
    Rect render_target_rect;  // 在最终画面上的输出区域
    uint32_t sort_layer;      // 画布层级（如：场景 = 0，世界 UI = 100，屏幕 UI = 200）
};
```

**渲染流程**：
1. **Screen Canvas**：在 3D 场景渲染完成后叠加。使用正交投影矩阵，坐标直接对应屏幕像素。
2. **World Canvas**：在 3D 场景渲染期间（如 Forward/Translucent Pass）绘制。每个 World Canvas 元素实际上是带特殊材质（始终面向相机，或无光照 Unlit）的 3D Quad，深度写入关闭或按需求开启。
3. **Viewport Canvas**：用于需要与 3D 渲染结果对齐的特效（如选中框、后处理 mask），使用 NDC 坐标。

**适用场景**：几乎所有现代游戏引擎。UE 的 `UGameViewportClient`（Screen）、`UWidgetComponent`（World）就是典型实现。Bevy 的 `bevy_ui` 默认是 Screen 空间，但通过给 UI Entity 添加 `Transform` 组件（配合 `GlobalTransform`）可以实现世界空间 UI。

**隐藏代价**：
- **渲染顺序复杂**。Screen Canvas 必须在 3D 场景之后绘制，World Canvas 必须在 3D 场景期间或之前绘制。需要 RenderGraph 显式管理画布 Pass 的依赖关系。
- **事件路由需要区分空间**。点击事件需要知道"当前点击的是屏幕空间 UI，还是世界空间 UI，还是 3D 场景"。这要求输入 System 在路由前查询画布层级和命中测试结果。

### 决策分析

**默认推荐：分支 B——多画布系统，在 ECS 中通过 `Canvas` 组件区分空间。**

ECS 映射：
- 每个画布是一个 Entity，携带 `Canvas` 组件（定义空间类型和输出矩形）。
- 2D 元素（Sprite、Text）通过 `UIBelongsToCanvas` 组件关联到画布 Entity。
- 渲染 System 按 `CanvasSpace` 分组处理：Screen 组在 PostProcess 之后绘制；World 组在 Translucent Pass 中绘制。

**阶段 5 的简化**：先只实现 Screen Canvas（用于 HUD 和 ImGui 替代准备）。World Canvas 的接口预留，但内部实现可以先退化为"每帧投影到屏幕坐标"（分支 A 的简化版），在阶段 7（Gameplay 运行时）需要角色头顶血条时，再升级为真正的 3D 空间渲染。


## 问题 5：2D/UI 与 3D 场景如何正确叠加？——渲染顺序与深度

即使有了独立的 2D 管线和多画布系统，还有一个关键问题：一帧内，3D 场景和 2D UI 的渲染顺序是什么？UI 是否参与深度测试？

### 场景与根因

最 naive 的做法是：先清屏，画 3D 场景，然后直接在上面画 2D UI。这在大多数情况下是对的，但当你引入以下需求时，顺序变得微妙：

- **后处理（PostProcess）**：Bloom 应该作用于 3D 场景，但不应该让 UI 文字也发光——否则按钮文字会糊成一片。
- **世界空间 UI**：血条应该在 3D 角色上方，但不应该遮挡住它身后的其他角色。
- **3D UI 内嵌于场景**：游戏内的电脑屏幕、仪表盘显示器，它们的 UI 内容需要先渲染到一张纹理（RenderTarget），然后作为普通 3D 材质贴到模型上。

**根因：2D/UI 渲染不是"最后一层涂上去"这么简单。不同类别的 UI 需要在 3D 管线的不同时机插入，与深度缓冲、后处理、RenderTarget 发生复杂的交互。**

### 分支研究

#### 分支 A：UI 永远最后画，不参与深度测试

**核心思路**：3D 场景（含后处理）全部完成后，关闭深度测试（`DepthWrite = false`, `DepthFunc = Always`），用 Alpha 混合直接叠加 UI。

**适用场景**：纯屏幕空间 HUD、不需要与 3D 场景发生遮挡关系的世界空间 UI。

**隐藏代价**：
- **后处理污染**。如果 3D 场景的后处理（如 Bloom、TAA）作用于全屏，UI 叠加后也会带上这些效果。解决方案是在后处理之前将 3D 场景输出到 `SceneColor` 纹理，后处理只作用于 `SceneColor`，UI 在后处理完成后叠加到最终画面。
- **世界空间 UI 深度错误**。角色血条如果不参与深度测试，会穿透墙壁显示在墙前面。

#### 分支 B：UI 分阶段插入 3D 管线

**核心思路**：不是"3D 完成后画 UI"，而是将不同类型的 UI 插入到 RenderGraph 的不同节点：

```
RenderGraph 一帧结构：
  Shadow Pass
  GBuffer Pass
  Deferred Lighting Pass
  Forward / Translucent Pass
    └── World Space UI 在这里绘制（参与深度测试）
  PostProcess Stack（Bloom, TAA, Tonemapping）
    └── 输入：SceneColor；输出：PostProcessColor
  UI Pass
    └── Screen Space UI 在这里绘制（不参与深度，Alpha 混合到 PostProcessColor）
  Present
```

**适用场景**：需要精确控制 UI 与 3D 遮挡关系、后处理隔离的项目。

**隐藏代价**：
- **RenderGraph 复杂度增加**。UI Pass 需要作为显式节点接入 RenderGraph，管理它与前序 Pass 的纹理依赖（如 `PostProcessColor` 作为输入附件）。
- **世界空间 UI 的材质限制**。World Space UI 如果走 Forward Pass，意味着它需要使用 Forward Shader（Unlit），不能与 Deferred 材质混用。

### 引擎对照

> **参考：chaos / UE / Bevy 对「UI 叠加」是怎么做的？**
>
> - **chaos** 的主 UI 框架渲染到独立的 UI 渲染目标纹理，然后在后处理阶段的专用合成 Pass 中与场景颜色缓冲混合。这意味着主 UI 不受场景后处理影响，但也没有深度信息。ImGui 则直接在后备缓冲上绘制（Present 前或 Present 后，取决于配置）。原生场景装饰 UI 作为普通渲染实体走延迟/前向管线，天然参与深度测试。
> - **UE** 的 Slate 渲染通过 `FSlateRHIRenderer` 在 Render Thread 中构建独立的 Slate RDG。对于屏幕空间 UI，Slate 渲染在 PostProcess 之后，通过 `ENQUEUE_RENDER_COMMAND` 提交，与 3D 场景通过 RDG 资源依赖隔离。对于世界空间 UI（`UWidgetComponent`），UMG 先将 Slate 渲染到 `RenderTarget`，然后该 RenderTarget 作为普通纹理供 3D 材质采样。
> - **Bevy** 的 `bevy_ui` 渲染发生在 `Render` Schedule 的 `RenderPass` 阶段。由于 Bevy 使用 Pipelined Rendering，UI 渲染与 3D 渲染共享同一个 RenderGraph。UI 节点默认在 3D 主 Pass 之后执行，通过 `Camera` 组件的 `target` 指定输出到 SwapChain Texture。世界空间 UI 可以通过将 UI Entity 作为 3D 物体的子实体（`Parent` 组件）来实现，渲染时走 3D 变换管线。
>
> **默认推荐：分支 B——在 RenderGraph 中显式插入 UI Pass，区分 Screen 和 World 两种叠加时机。**
>
> 具体策略：
> 1. **Screen Space UI**：在 PostProcess 之后、Present 之前，作为最终的 `UIPass` 节点。输入 `PostProcessColor`，输出到 `FinalColor`。`UIPass` 关闭深度写入，使用预乘 Alpha 或标准 Alpha 混合。
> 2. **World Space UI**：在 Forward/Translucent Pass 中绘制。每个 World Space UI 元素本质上是一个 `Billboard Quad`（始终面向相机）或普通 Quad，使用 Unlit 材质，深度写入按需开启（通常开启，以正确遮挡）。
> 3. **RenderTarget UI**（如游戏内显示器）：先渲染到一个独立的 `RenderTarget` 纹理，然后在 3D 场景的 Material Pass 中作为普通纹理采样。这是 UE `UWidgetComponent` 的标准做法，阶段 5 暂不实现，阶段 6/7 需要时引入。
>
> **ECS 映射**：
> - `ScreenCanvas` 组件标记需要走 `UIPass` 的 2D 元素。
> - `WorldCanvas` 组件标记需要走 Forward Pass 的 2D 元素，其 `GlobalTransform` 参与 3D 场景图更新。
> - RenderGraph 的 `Setup` 阶段根据场景中存在的画布类型，动态决定是否添加 `UIPass` 节点和 World UI Forward Pass 子节点。

---


> **下一步**：[[后处理栈架构]]，因为阶段 5 的 2D/UI 渲染已经能正确叠加在 3D 场景上。验收标准还要求 Bloom 光晕——后处理系统是提升画面表现力的关键。
