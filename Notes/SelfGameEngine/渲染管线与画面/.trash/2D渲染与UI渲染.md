---
title: 2D渲染与UI渲染
date: 2026-05-25
tags:
  - self-game-engine
  - rendering
  - 2d
  - ui
  - sdf
  - batching
aliases:
  - 2D渲染与UI渲染
  - 2D UI 渲染管线
---

> [[Notes/SelfGameEngine/0_RoadMap|← 返回 SelfGameEngine 构建手册]]

> **前置依赖**：[[渲染一帧的生命周期]]（理解 Extract 阶段、RenderGraph、逻辑-渲染并行架构）、[[材质与着色器系统]]（理解 Shader 变体、材质实例、BindGroup 绑定）、[[资源管理]]（理解纹理句柄、异步加载、图集 Atlas）
> **本模块增量**：深入理解 2D 渲染与 UI 渲染在 ECS 架构下的数据组织、批次合批策略、文字渲染方案、画布分层以及与 3D 场景的混合方式。你将掌握从 ECS 组件到屏幕上 2D 像素的完整数据流，并能为阶段 6 的自研 UI 框架奠定渲染基础。
>
> 本笔记探讨的核心问题是：**在已经跑通的 3D 渲染管线之上，如何叠加一个高效、清晰、可交互的 2D/UI 渲染层？** 我们将从"为什么不能用画立方体的方式画按钮"出发，逐层分析 2D 数据的 ECS 表达、批次合并、文字渲染、画布空间以及与 ImGui 的协作边界。

---

## 问题 0：为什么 3D 渲染管线不能直接画 2D/UI？

想象你已经用阶段 5.4 的渲染管线画出了一个旋转的带纹理立方体。现在你想在屏幕角落加一个"暂停"按钮和一行 FPS 文字。最直觉的想法是：按钮不就是一个扁平的立方体吗？用两个三角形拼成一个 Quad，贴一张按钮纹理，放在相机前面不就行了？

这个思路在 demo 级别确实能跑，但当你真的这么做了，会立刻撞上三个隐形墙壁：

**第一，投影矩阵的语义冲突。** 3D 场景使用透视投影（Perspective Projection），远处的物体看起来更小。但 UI 元素必须严格按像素对齐——一个 100×30 像素的按钮，在 1920×1080 和 2560×1440 的屏幕上都应该占据完全相同的逻辑尺寸（或按 DPI 缩放），不能因为"离相机近"就被透视拉伸。如果你把按钮放在 `z = 0.1` 处，透视矩阵会让按钮边缘产生微小的梯形畸变；如果你放在 `z = 0`（近平面上），深度缓冲的精度问题会让它跟场景物体发生 Z-fighting。

**第二，渲染顺序与深度测试的矛盾。** 3D 管线依赖深度缓冲（Z-Buffer）来决定像素遮挡关系。但 UI 是**叠加**在场景之上的，它的遮挡规则不是"谁离相机近谁赢"，而是"谁在逻辑上层谁赢"。如果 UI 也写深度，一个稍微靠前的 3D 物体会意外遮挡掉 HUD 血条；如果 UI 不写深度，它又可能被场景中的透明物体搞乱混合顺序。更麻烦的是，UI 内部也有层级——按钮应该在面板上方，文字应该在按钮上方——这种层级关系用深度值来编码非常脆弱。

**第三，光照与材质的开销浪费。** 3D 管线为每个物体计算法线、光照、阴影、环境光遮蔽。但 UI 元素通常只需要"原样显示纹理颜色 + 透明度混合"。让按钮走一遍完整的 Deferred Lighting Pass，不仅浪费 GPU 算力，还会因为光照计算让按钮颜色变暗或染上场景光源的色偏。

**根因在于：3D 渲染解决的是"模拟物理世界中的光照和遮挡"，而 2D/UI 渲染解决的是"在屏幕平面上以精确像素和层级关系呈现信息"。两者的目标、约束和优化方向完全不同。**

所以，我们需要一条**独立的 2D 渲染路径**——它不是 3D 管线的"简化版"，而是与 3D 管线**并行共存**的专用子系统。阶段 5 的目标就是打通这条路径，让 3D 场景和 2D UI 在同一帧内正确叠加。

---

## 问题 1：2D 渲染的数据在 ECS 中如何组织？

既然要为 2D 建立独立路径，第一个问题是：2D 元素（精灵、UI 节点、文字）在 ECS 中应该是什么形态？

在 OOP 引擎中，答案通常是继承树：`UIElement` → `UIImage` / `UIText` / `UIButton`，每个对象自带位置、尺寸、纹理引用。但在 ECS 中，数据是平铺的组件，对象身份由 Entity ID 承载。

### 分支研究

#### 分支 A：每个 2D 元素一个 Entity，组件完全扁平

**核心思路**：每个可见的 2D 片（Sprite、文字字符、UI 矩形）都是一个 Entity，携带 `Sprite2D`、`Transform2D`、`ColorTint` 等组件。

```cpp
// 一个 2D 按钮
entity e = world.spawn();
world.add<Transform2D>(e, { .position = {100, 50}, .scale = {1,1} });
world.add<Sprite2D>(e, { .texture = handle_button, .uv_rect = {0,0,1,1} });
world.add<ColorTint>(e, { .color = {1,1,1,1} });
```

**适用场景**：简单 HUD、少量动态 2D 元素（如粒子图标）。

**隐藏代价**：
- **层级关系难以表达**。ECS 没有天然父子概念。如果按钮有一个子元素"按钮文字"，文字需要跟随按钮移动，扁平 Entity 无法直接表达这种依附关系。你需要额外的 `UIParent` 组件存储父 Entity ID，并在每帧手动传播变换——这相当于在 ECS 内重建一套微型场景图。
- **裁剪和遮挡顺序复杂**。UI 通常需要按深度/层级排序后绘制，扁平 ECS 查询默认按存储顺序返回，不满足渲染顺序要求。需要额外的 `SortKey` 组件和排序 System。

**失效条件**：任何有层级、有裁剪、有大量元素的 UI 系统。

#### 分支 B：2D 元素作为 3D Entity 的"渲染标签"

**核心思路**：复用已有的 3D Transform 和渲染基础设施，给需要 2D 渲染的 Entity 打上一个 `RenderAs2D` 标签，渲染 System 在提取阶段（Extract）将其转换到屏幕空间。

```cpp
struct RenderAs2D {
    Vec2 screen_position;  // 屏幕空间坐标
    Vec2 size_pixels;      // 像素尺寸
    uint32_t layer;        // 渲染层级
};
```

**适用场景**：世界空间 UI（如角色头顶血条），这类元素既有 3D 世界位置，又需要面向相机的 2D 外观。

**隐藏代价**：
- **耦合了 3D 和 2D 的更新逻辑**。世界空间 UI 需要每帧跟随 3D 实体移动，它的屏幕位置由 `WorldToScreen` 矩阵决定。如果逻辑线程在更新 3D 位置，渲染线程需要同步读取并转换——引入了跨线程数据依赖。
- **不适用于纯屏幕空间 UI**。主菜单、HUD 没有对应的 3D 实体，强行映射到 3D 空间（如放在相机近平面上）会绕回问题 0 的投影冲突。

**失效条件**：纯屏幕空间 UI 占主导地位的项目。

#### 分支 C：独立的 2D ECS 子世界（Render World 内的 2D 层）

**核心思路**：在阶段 5.4 建立的 Render World 中，为 2D 渲染维护一套独立的组件类型和 System。主 World 的 UI 逻辑 System 通过 Extract 阶段将 2D 数据复制到 Render World 的 2D 层，Render World 内部按 2D 规则处理。

```cpp
// Render World 中的 2D 组件（与主 World 分离）
struct RenderSprite2D {
    TextureHandle texture;
    Rect uv;
    uint32_t sort_key;   // 层级 + 材质哈希编码的排序键
};

struct RenderTransform2D {
    Vec2 position;       // 已经转换到屏幕空间或 NDC
    Vec2 scale;
    float rotation;
};

struct RenderClipRect {
    Recti clip;          // 裁剪矩形（像素坐标）
};
```

**适用场景**：几乎所有需要 2D 渲染的项目。这是 Bevy 的 `bevy_ui` 和 `bevy_sprite` 采用的方案。

**隐藏代价**：
- **Extract 阶段需要额外拷贝**。主 World 的 UI 状态每帧都要复制到 Render World，增加了 CPU 开销。但如阶段 5.4 所述，这种拷贝是"零锁并行"的必要代价。
- **两套组件类型需要维护映射**。主 World 的 `UIText` 组件需要 Extract 为 Render World 的 `RenderGlyphBatch`，映射逻辑需要随 UI 功能扩展而更新。

### 引擎对照

> **参考：chaos / UE / Bevy 对「2D 数据组织」是怎么做的？**
>
> - **chaos** 的 2D/UI 数据并非走 ECS 主 World，而是分散在三套管线中：主 UI 框架（基于 HTML/CSS 的中间件）维护自己的 DOM/Canvas 对象树，最终渲染为全屏纹理；ImGui 维护即时模式的顶点列表；引擎原生的场景装饰 UI 使用简化 Mesh 和材质实例作为普通渲染实体提交，走标准 3D 管线。这三套数据在渲染前被统一转换为纹理或 Draw Call，但转换逻辑各不相同。
> - **UE** 的 Slate 是纯 C++ 对象树（`SWidget` 继承体系），不走 ECS。Slate 在 Game Thread 遍历 Widget 树生成 `FSlateWindowElementList`，再通过 `ENQUEUE_RENDER_COMMAND` 投递到 Render Thread。对于 UMG（蓝图 UI），`UWidget` 底层映射到 `SWidget`，数据组织是 OOP 的。
> - **Bevy** 的 `bevy_ui` 是 ECS 原生方案。UI 节点就是 Entity，`Node` 组件控制布局，`Style` 组件控制外观，`Text` 组件存储字符串。渲染时，Extract 阶段将 UI 数据提取到 Render World，由 `UiRenderSystem` 生成 2D 批次。`bevy_sprite` 同理——每个 Sprite 是一个 Entity，携带 `Sprite` + `Transform` + `Handle<Texture>` 组件。
>
> **默认推荐：分支 C——在 Render World 内建立独立的 2D 组件层，吸收 Bevy 的 ECS 原生设计，同时借鉴 UE Slate 的"绘制元素列表"概念。**
>
> 理由：
> 1. **ECS 一致性**：2D 元素和 3D 实体使用同一套 ECS 基础设施（Entity、Component、Query），AI 可观测性统一。Bevy 已经验证了这条路径的可行性。
> 2. **逻辑-渲染解耦**：Extract 阶段明确划分了"主 World 的 UI 逻辑状态"和"Render World 的渲染输入数据"，与阶段 5.4 的三线程模型保持一致。
> 3. **层级关系的 ECS 表达**：通过给 Entity 添加 `UIParent` 组件（存储父 Entity ID）和 `UIChildren` 组件（存储子 Entity ID 列表），可以在 ECS 中表达树形结构。Bevy 使用类似的 `Parent` / `Children` 组件对。虽然 ECS 的扁平存储对树遍历不友好，但 UI 树的深度通常较浅（< 10 层），每帧自顶向下遍历一次是可接受的。
>
> **简化路径（阶段 5）**：如果 UI 层级非常浅（只有一层 HUD，无嵌套面板），可以暂时省略 `UIParent`/`UIChildren`，所有 2D 元素视为扁平列表，用 `sort_key` 控制层级。阶段 6 引入复杂 UI 框架后再补全层级组件。

---

## 问题 2：如何减少 2D Draw Call？——批次与合批

假设你的 HUD 有 200 个元素：血条背景、血条填充、技能图标、文字、小地图边框。如果每个元素单独调用一次 `DrawIndexed`，200 个 Draw Call 在移动端或低端 PC 上会把 CPU 端压垮。3D 场景有类似的合批问题，但 2D 的"合批潜力"更大——因为 2D 元素通常共享同一个纹理图集（Texture Atlas）和同一个着色器。

### 场景与根因

为什么 2D 的 Draw Call 开销比 3D 更敏感？因为 2D 元素的单个 Draw Call 工作量极小——通常只是一个 Quad（4 顶点、6 索引）。3D 的一个 Draw Call 至少有几百到几千个顶点，GPU 执行时间较长，CPU 有喘息空间；而 2D 的 Quad 在 GPU 上瞬间完成，CPU 还没准备好下一个 Draw Call，GPU 就空闲了。结果就是**CPU 绑定（CPU-bound）**，帧率被 CPU 提交命令的速度限制。

**根因：API 级别的 Draw Call 固定开销（State Validation、Command Buffer 追加、Driver 处理）与 2D 元素极小的 GPU 工作量之间的不匹配。**

### 分支研究

#### 分支 A：CPU 侧动态顶点缓冲合批（Dynamic Batching）

**核心思路**：每帧在 CPU 侧收集所有需要绘制的 2D Quad，将它们的顶点数据拼接成一个大数组，一次性上传到 GPU，用一个 Draw Call 绘制。

```cpp
struct SpriteVertex {
    Vec2 position;   // 屏幕空间或 NDC
    Vec2 texcoord;
    Color color;
};

void Batch2DSystem(RenderWorld* world, SpriteBatch* batch) {
    batch->clear();
    // 查询所有需要渲染的 2D 精灵
    for (auto [sprite, transform] : world->query<RenderSprite2D, RenderTransform2D>()) {
        // 计算该 Sprite 的四个顶点
        Vec2 verts[4] = compute_quad_corners(transform, sprite.size);
        // 追加到批次缓冲
        batch->append_quad(verts, sprite.uv, sprite.color);
    }
    // 一次性上传并绘制
    batch->flush(ctx);  // -> glBufferSubData / vkCmdUpdateBuffer -> DrawIndexed(count)
}
```

**适用场景**：2D 元素数量中等（< 2000 个 Quad）、纹理切换不频繁、每帧大部分元素都会动。

**隐藏代价**：
- **CPU 内存拷贝开销**。每帧都要把所有 Quad 的顶点数据拷贝到 CPU 缓冲，再上传到 GPU。2000 个 Quad × 4 顶点 × 16 字节 = 128 KB，尚可接受；但如果是 10000 个 Quad，CPU 拷贝成本会显著上升。
- **纹理切换打断批次**。如果第 1 个 Quad 用按钮纹理，第 2 个 Quad 用图标纹理，第 3 个 Quad 又用按钮纹理，批次会被纹理切换切成 3 段。如果不按纹理排序，合批效率极低。

**失效条件**：元素数量极大（> 5000）、纹理高度碎片化、或静态元素占主导（不值得每帧重建顶点缓冲）。

#### 分支 B：纹理图集 + 静态合批（Texture Atlas + Static Batching）

**核心思路**：预先将所有 2D 纹理（UI 皮肤、图标、字体字形）打包成一张或多张大图集（Atlas）。运行时所有 2D 元素都采样这张 Atlas，从而消除纹理切换。对于静态 UI（如主菜单背景、面板边框），预先将顶点数据生成到静态 VBO 中，每帧直接绘制。

**适用场景**：UI 风格统一、纹理数量有限、静态元素多的项目。

**隐藏代价**：
- **图集打包的复杂度**。需要离线工具将纹理打包，处理边缘 bleed（防止 mipmap 采样越界），并维护 UV 映射表。动态加载的新纹理如果不在图集中，会破坏合批。
- **图集尺寸限制**。移动端图集通常限制在 2048×2048 或 4096×4096，如果纹理太多需要多张图集，图集之间的切换仍然会打断批次。
- **动态元素无法享受静态合批**。血条填充、滚动列表、动画图标等每帧变化的元素，仍然需要动态顶点缓冲。

**失效条件**：纹理数量巨大且动态变化（如用户自定义头像、运行时生成的图标）。

#### 分支 C：GPU Instancing

**核心思路**：对于共享相同几何（Quad）和相同材质（纹理 + 着色器）的元素，使用 GPU Instancing 一次性绘制多个实例。每个实例的差异化数据（位置、颜色、UV）通过实例化属性（Instance Attribute）或 Uniform Buffer / Storage Buffer 传入。

```cpp
// 实例数据（存储在 SSBO 或 Instanced Vertex Buffer 中）
struct SpriteInstance {
    Vec2 position;
    Vec2 scale;
    Vec4 uv_rect;
    Color color;
    uint32_t texture_index;  // 如果使用纹理数组
};

// 顶点着色器
layout(location = 0) in vec2 a_pos;     // 单位 Quad 顶点 (0..1)
layout(std430, binding = 0) readonly buffer Instances {
    SpriteInstance data[];
} instances;

void main() {
    SpriteInstance inst = instances.data[gl_InstanceID];
    vec2 world_pos = a_pos * inst.scale + inst.position;
    gl_Position = vec4(world_pos, 0.0, 1.0);
    v_uv = mix(inst.uv_rect.xy, inst.uv_rect.zw, a_pos);
    v_color = inst.color;
}
```

**适用场景**：大量同质元素（如粒子、瓦片地图、列表项）。

**隐藏代价**：
- **实例数据仍需 CPU 准备**。虽然减少了 Draw Call，但每帧仍需要把实例数组上传到 GPU。2000 个实例 × 32 字节 = 64 KB，上传开销低于分支 A 的完整顶点数据，但仍不可忽视。
- **需要纹理数组（Texture Array）或绑定表（Bindless）支持**。如果每个实例使用不同纹理，传统 Instancing 需要所有实例共享同一张纹理。解决方案是使用纹理数组（限制：所有纹理尺寸相同）或绑定纹理（Bindless Textures，需要 Vulkan/D3D12 和硬件支持）。

**失效条件**：OpenGL ES 2.0 等旧 API 不支持 Instancing 或纹理数组。

### 引擎对照

> **参考：chaos / UE / Bevy 对「2D 合批」是怎么做的？**
>
> - **chaos** 的原生场景装饰 UI 作为普通渲染实体提交，走标准 3D 管线的合批逻辑（按材质和 Mesh 排序）。ImGui 的合批是顶点级别的——ImGui 每帧生成一个巨大的顶点缓冲，所有控件共享同一张字体图集和一张白色纹理，因此通常只有一个 Draw Call。主 UI 框架则完全在 GPU 外渲染为一张全屏纹理，最终通过一个 Quad Draw Call 合成到场景。
> - **UE Slate** 的 `FSlateElementBatcher` 在生成 `FSlateWindowElementList` 时，会自动将相同材质（`FSlateBrush`）和相同裁剪区的 Box/Text/Line 元素合并到同一个顶点缓冲中。Slate 使用纹理图集（Font Atlas 和 Brush Atlas）来减少纹理切换。对于 UMG，`FWidgetRenderer` 将 Slate 渲染到 RenderTarget，最终也只是一个全屏 Quad。
> - **Bevy** 的 `bevy_sprite` 使用动态合批：`SpriteRenderSystem` 收集所有 Sprite，按纹理和材质排序，生成 `QuadBundle` 批次。Bevy 的 UI 渲染（`bevy_ui`）同样采用顶点缓冲合批，将所有 UI 节点按 `UiPipeline` 排序后批量提交。Bevy 正在推进 Bindless 纹理支持，未来有望实现跨纹理的 Instancing。
>
> **默认推荐：阶段 5 采用「图集 + 动态顶点缓冲合批」的混合方案，为阶段 6 预留 Instancing 升级路径。**
>
> 具体策略：
> 1. **阶段 5（当前）**：实现一个 `SpriteBatch` 系统，每帧收集所有 2D 元素，按 `(texture_handle, pipeline, clip_rect)` 排序，将相同组的 Quad 顶点拼接上传，每组一个 Draw Call。静态和动态元素走同一条路径——先保证正确性，再优化性能。
> 2. **图集优先级**：将引擎默认 UI 纹理（按钮、边框、滑轨）和字体字形预先打包到一张 Atlas。游戏项目特定纹理如果数量少，也建议打包；如果数量多且动态，允许独立纹理，接受额外的 Draw Call。
> 3. **阶段 6 升级**：当 UI 元素数量超过 2000 或出现性能瓶颈时，引入 GPU Instancing + 纹理数组。将 `SpriteBatch` 的顶点生成逻辑改为实例数据生成逻辑，着色器改为 Instance 模式。
>
> **为什么不是 Instancing 优先？** Instancing 需要纹理数组或 Bindless 支持，而阶段 5 的 RHI 抽象层可能还未覆盖这些高级特性。此外，UI 元素的差异很大（不同尺寸、不同 UV、不同裁剪），Instance 数据的填充并不比动态顶点简单多少。UE Slate 和 Bevy 当前版本都选择了动态顶点合批而非 Instancing，这是经过验证的"最小正确路径"。

---

## 问题 3：文字渲染——如何做到清晰且可缩放？

2D/UI 渲染中最特殊的元素是文字。一个按钮可以用一张静态纹理，但文字内容是动态的——"FPS: 60" 每帧都在变，"玩家名称" 由运行时输入决定。更麻烦的是，文字需要清晰锐利，而且可能在不同尺寸下显示（标题大字、注释小字、缩放动画）。

### 场景与根因

最 naive 的方案是：**预先将所有可能用到的字符渲染成位图，存到一张纹理图集里**。运行时根据字符编码查找对应的位图区域，画一个贴上了字符位图的 Quad。

这个方案的问题在于**缩放**。位图字体在原始尺寸下很清晰，但放大后会像素化（出现锯齿和块状边缘）；缩小后又因为采样不足而模糊。如果为每个字号都生成一张位图，内存爆炸——中文字体有上万个字符，10 个字号就是 10 万张位图。

**根因：位图是离散的像素网格，而文字轮廓本质上是连续的矢量曲线（贝塞尔曲线）。在栅格化后的位图上做缩放，只是在做像素插值，无法恢复原始轮廓信息。**

### 分支研究

#### 分支 A：多字号位图图集（Bitmap Font Atlas）

**核心思路**：离线生成若干关键字号（如 12px、16px、24px、48px）的位图图集，运行时选择最接近的字号，必要时用线性插值缩放。

**适用场景**：像素风游戏、复古风格 UI、文字尺寸固定且种类少的项目。

**隐藏代价**：
- **内存与包体膨胀**。每个字号一张图集，中文字体轻易突破 100MB。
- **非整数倍缩放质量差**。16px 缩放到 20px 的模糊程度比原生 20px 差很多。
- **无法动态改变字号**。如果玩家调整 UI 缩放比例，可能找不到对应的位图字号。

**失效条件**：需要流畅字号变化、高分辨率显示、或国际化多语言的项目。

#### 分支 B：Signed Distance Field（SDF）字体

**核心思路**：不存储字符的"颜色位图"，而是存储每个像素到字符轮廓边缘的**有符号距离**。正值表示在字形内部，负值表示在外部，零值正好在边缘。

```cpp
// SDF 纹理采样与边缘重建（片段着色器）
uniform sampler2D u_font_atlas;
uniform vec4 u_text_color;
in vec2 v_uv;

void main() {
    float sdf = texture(u_font_atlas, v_uv).r;  // 采样距离值
    // smoothstep 在轮廓附近做抗锯齿过渡
    // u_buffer 是轮廓阈值（通常为 0.5）
    // u_gamma 控制过渡宽度（通常为 0.05 ~ 0.1 / 像素密度）
    float alpha = smoothstep(u_buffer - u_gamma, u_buffer + u_gamma, sdf);
    out_color = vec4(u_text_color.rgb, alpha * u_text_color.a);
}
```

**关键机制**：
- 纹理中每个纹素存储的不是颜色，而是"距离"。距离场是平滑连续的，因此放大时不会出现块状边缘——shader 通过 `smoothstep` 在任意精度下重建出锐利的轮廓。
- 由于只用一个通道（R8）存储距离，图集内存开销与灰度位图相同。
- 可以通过调整 `smoothstep` 的阈值轻松实现描边、外发光、阴影等效果——这些在传统位图中需要额外的纹理或通道。

**适用场景**：几乎所有现代游戏 UI。Unity 的 TextMeshPro、Godot、Unreal Engine 5.5+ 都支持 SDF 字体。

**隐藏代价**：
- **角落变圆**。单通道 SDF 在每个像素只存储"到最近边缘的距离"，当多个边缘在角落汇合时，距离场无法精确保留锐角，导致小字号下的角落轻微变圆。
- **不适用于极小号字**。当字形缩到 8px 以下时，SDF 纹理的分辨率不足，笔画的粗细和间距会失真。这是因为 SDF 不支持 hinting（像素级对齐微调），而小字号位图的 hinting 对可读性至关重要。
- **生成开销**。从矢量字体（TTF/OTF）生成 SDF 图集需要计算距离变换，比直接栅格化慢。但这是一次性离线操作。

#### 分支 C：Multi-Channel Signed Distance Field（MSDF）

**核心思路**：使用 RGB 三个通道分别编码三个方向上的距离场，通过交叉重建精确保留锐角。

**关键优势**：解决了单通道 SDF 的"角落变圆"问题。MSDF 在放大后仍能保持字体的锐利拐角，是目前游戏行业文字渲染的**事实标准**。

**隐藏代价**：
- **生成算法更复杂**。需要基于字体矢量轮廓进行分区计算，离线工具链（如 `msdf-atlas-gen`）比单通道 SDF 重。
- **纹理内存 ×3**。三个通道意味着图集大小是单通道 SDF 的 3 倍（但仍是 RGBA 位图的 75%，因为不需要 Alpha 以外的颜色信息）。

**适用条件**：对字体质量要求高、需要大字标题、或文字有缩放动画的项目。

#### 分支 D：GPU 实时矢量渲染（Slug 算法等）

**核心思路**：不在 CPU 侧栅格化字体，而是将字体矢量轮廓（贝塞尔曲线）直接上传到 GPU，由片段着色器在运行时求解"当前像素是否在曲线内"。

**关键优势**：完全分辨率无关，无纹理内存，支持任意字号和变形。

**隐藏代价**：
- **极高的 GPU 片段开销**。每个像素都要求解多次贝塞尔曲线方程，文字密集时 GPU 负担沉重。
- **算法复杂度高**。需要处理曲线自交、空洞、重叠等拓扑问题，着色器代码量巨大。
- **工具链不成熟**。直到 2026 年 Slug 专利解除后才出现开源实现，目前缺乏广泛验证的离线-运行时一体化工具链。

**失效条件**：当前阶段（阶段 5）不建议采用。作为前沿技术关注即可。

### 决策分析

**默认推荐：阶段 5 采用 MSDF 作为主力字体渲染方案，同时保留位图字体的降级路径。**

理由：
1. **MSDF 是工业级游戏 UI 的标准选择**。UE 5.5+、Unity TextMeshPro、Godot 4 都原生支持 MSDF。它在质量、性能、内存之间取得了最佳平衡。
2. **SDF/MSDF 的图集内存可控**。一张 2048×2048 的 R8/MSDF 图集可以容纳常用数千个字符（包括中文）。配合纹理压缩（BC4/BC5），显存占用在 4~12 MB 级别。
3. **着色器统一**。MSDF 和单通道 SDF 的着色器结构几乎相同，只是 MSDF 需要多采样两个通道并做交叉计算。阶段 5 先实现 MSDF，自然覆盖了 SDF 的能力。
4. **位图降级路径**：对于极小号注释文字（< 10px），可以预生成一张 hinted 位图图集作为补充，在检测到小字号时切换采样源。

**简化路径（阶段 5）**：如果工具链集成成本过高，可以先实现单通道 SDF（用 `stb_truetype` + 简单距离变换生成），在阶段 6 再升级为 MSDF（引入 `msdfgen` 或预计算管线）。

---

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

---

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

## 问题 6：与 ImGui 的协作边界——阶段 5 的临时方案

阶段 5 的验收标准中有一条："屏幕角落有 2D UI 文字和可点击按钮"。但阶段 6 才会开始自研 Retained-Mode UI 框架。在阶段 5，我们的按钮和文字从哪里来？

答案是：**ImGui 仍然是阶段 5 的 UI 工具，但我们要明确它与自研 2D 渲染管线的协作边界，为阶段 6 的替换做好准备。**

### 场景与根因

ImGui 是即时模式（Immediate Mode）UI 库，每帧由 C++ 代码直接描述 UI 结构（`ImGui::Begin("Panel"); ImGui::Button("Click");`）。它的优势是集成简单、无需状态管理；劣势是样式僵硬、布局能力弱、每帧重建所有状态。

阶段 5 的自研 2D 渲染管线（SpriteBatch、MSDF 字体、多画布）与 ImGui 的关系是什么？是竞争还是互补？

**根因：ImGui 和自研 2D 管线解决的是不同层次的问题。ImGui 是"UI 逻辑 + 渲染"的一体化方案；自研 2D 管线是"渲染基础设施"，不假设上层是 Immediate 还是 Retained 模式。**

### 分支研究

#### 分支 A：阶段 5 完全依赖 ImGui，不自研 2D 渲染

**核心思路**：跳过阶段 5.5 的 2D 渲染开发，直接用 ImGui 画所有 2D 元素。

**适用场景**：快速原型、个人 demo、确定不需要自研 UI 框架的项目。

**隐藏代价**：
- **阶段 6 回探成本极高**。如果阶段 5 没有任何自研 2D 渲染代码，阶段 6 需要从零构建 UI 渲染核心，同时重写所有面板。
- **ImGui 无法绘制游戏内精致 UI**。ImGui 的字体渲染是传统的位图图集，缩放模糊；它的绘制 API 只有基础图元，不支持 SDF 效果、圆角矩形、阴影、渐变等现代 UI 视觉需求。
- **与 3D 场景的混合受限**。ImGui 默认绘制到屏幕空间，难以实现世界空间 UI。

**失效条件**：roadmap 已明确要求阶段 6 替换 ImGui，因此此分支不可行。

#### 分支 B：自研 2D 管线为主，ImGui 作为独立后端叠加

**核心思路**：阶段 5 实现完整的自研 2D 渲染管线（SpriteBatch、MSDF、Screen Canvas），用于绘制游戏内 HUD 和测试性 UI。ImGui 仍然通过其自身的图形后端（如 `imgui_impl_opengl3`）直接绘制到 BackBuffer，但在自研管线的 `UIPass` 之后执行。

**协作边界约定**：
1. **渲染顺序**：3D 场景 → PostProcess → 自研 2D UI（HUD） → ImGui（调试面板）。
2. **输入优先级**：ImGui 窗口获得输入焦点时，底层 gameplay 输入被屏蔽（ImGui 的 `WantCaptureMouse` / `WantCaptureKeyboard`）。
3. **资源隔离**：ImGui 使用自己的字体图集和着色器，不占用自研管线的 SpriteBatch 和 MSDF 资源。
4. **数据隔离**：ImGui 的 UI 状态（窗口位置、折叠状态）由 ImGui 内部管理，不参与 ECS。自研 2D UI 的元素是 ECS Entity，可在 Inspector 中查看。

**适用场景**：阶段 5 到阶段 6 的过渡期。

**隐藏代价**：
- **两套路由并存**。输入事件需要先经过 ImGui 的路由，再经过自研 UI 的路由（如果存在），最后到达 gameplay。逻辑上需要明确的优先级顺序。
- **视觉风格割裂**。ImGui 的默认蓝色主题与自研 UI 的视觉风格可能完全不同，在过渡期内屏幕上有两种 UI 并存。

#### 分支 C：将 ImGui 的渲染嫁接到自研 2D 管线

**核心思路**：不调用 `imgui_impl_opengl3` 的渲染代码，而是让 ImGui 的每帧顶点输出（`ImDrawData`）进入自研 SpriteBatch，由自研管线统一绘制。

**适用场景**：希望彻底统一渲染路径，让 ImGui 也能享受自研管线的 SDF 字体、后期视觉效果的场景。

**隐藏代价**：
- **集成复杂度高**。需要解析 `ImDrawList` 的顶点格式，转换为自研管线的批次格式。ImGui 的顶点包含位置、UV、颜色，与自研 SpriteVertex 结构兼容，但 ImGui 的绘制命令（`ImDrawCmd`）包含裁剪矩形和纹理绑定，需要映射到自研管线的排序和裁剪逻辑。
- **失去 ImGui 后端的跨平台便利性**。`imgui_impl_opengl3` / `imgui_impl_vulkan` 已经处理了各平台的细节，嫁接到自研管线意味着你要自己处理这些。

### 决策分析

**默认推荐：分支 B——自研 2D 管线与 ImGui 后端并行，通过渲染顺序和输入优先级明确协作边界。**

理由：
1. **最小侵入性**。ImGui 的后端代码不需要任何修改，保持了阶段 1~4 的所有调试面板功能零回归。
2. **阶段 6 替换路径清晰**。当自研 UI 框架（阶段 6）完成后，逐步将 ImGui 面板替换为自研 UI 面板。替换过程是"功能对等功能"的，不需要改动底层渲染。
3. **阶段 5 的 2D 管线得到实战验证**。即使 ImGui 仍负责编辑器面板，自研 2D 管线也需要绘制 HUD 和游戏内 UI——这足以验证 SpriteBatch、MSDF、Canvas 的正确性。
4. **chaos 和 UE 都是多管线并存**。chaos 同时运行主 UI 框架、ImGui、原生场景装饰 UI 三套管线；UE 有 Slate + UMG + WidgetComponent 多套系统。工业实践中"多套 UI 渲染并存"是常态，不是技术债务。

**阶段 5 的具体协作约定**：
```cpp
// 一帧渲染顺序（RenderGraph 节点）
// 1. 3D 场景 Pass 链
// 2. PostProcess Pass
// 3. Self2D_UIPass    ← 自研 2D 管线：HUD、测试按钮、MSDF 文字
// 4. ImGui_Pass       ← ImGui 后端：调试面板、日志窗口、Inspector
// 5. Present
```

输入优先级（从高到低）：ImGui（如果 `WantCaptureMouse`）→ 自研 UI（如果命中 UI 元素）→ Gameplay（3D 场景交互）。

---

## 工业级设计清单与默认路径

### 阶段 5 的"最小正确"2D/UI 渲染架构

| 模块 | 默认推荐 | 简化过渡路径 | 阶段 6 升级方向 |
|------|---------|-------------|----------------|
| 数据组织 | Render World 独立 2D 组件层 | 扁平 Entity + `sort_key` | 引入 `UIParent`/`UIChildren` 层级 |
| 合批策略 | 动态顶点缓冲 + 纹理图集 | 无图集，按纹理分批次 | GPU Instancing + 纹理数组 |
| 文字渲染 | MSDF 字体图集 | 单通道 SDF | 增加位图降级（极小字号） |
| 画布系统 | Screen Canvas 为主，World Canvas 预留接口 | 仅 Screen Canvas | 完整 World Canvas + RenderTarget Canvas |
| 叠加时机 | RenderGraph 显式 UIPass（PostProcess 后） | 直接在 SwapChain 上绘制 | 多阶段插入（Forward/UIPass） |
| ImGui 边界 | 并行后端，渲染顺序在后 | — | 完全替换为自研 UI |

### 默认路径必须满足的两个约束

1. **ECS 一致性**：所有 2D 渲染输入数据都是 Render World 中的组件，可以被 System 批量查询，可以被 AI 通过 ECS Schema 观察和操作。
2. **UE 工业级设计吸收**：RenderGraph 显式 Pass 节点、多画布空间区分、后处理与 UI 隔离——这些设计思想直接来自 UE 的 Slate 与 UMG 管线架构。在 ECS 中的映射方式是：将 UE 的"绘制元素列表"概念转换为 Render World 的组件查询结果，将 UE 的"命令队列投递到渲染线程"机制转换为 Extract + Render System 的延迟执行。

### AI 友好设计检查

| 检查项 | 状态 | 说明 |
|--------|------|------|
| **状态平铺** | ✅ | 2D 元素的位置、颜色、纹理、裁剪全部是 ECS 组件，可序列化。 |
| **自描述** | ✅ | `SpriteBatch` 的每帧输出可以被记录为"本帧绘制了 N 个 Quad，使用 M 张纹理"。 |
| **确定性** | ✅ | 给定相同的 ECS 组件状态，2D 渲染的输出（顶点顺序、排序键、Draw Call 分组）完全确定。支持快照回放。 |
| **工具边界** | ✅ | AI 可以通过 MCP 接口修改 `RenderTransform2D`、`ColorTint` 等组件，实时改变 UI 外观。 |
| **Agent 安全** | ⚠️ 预留 | 阶段 6 的 UI 事件路由需要引入白名单，防止 AI 误触敏感按钮（如"退出游戏"）。 |

---

> **下一步**：[[UI渲染核心]]，因为阶段 5 的 2D 渲染管线已经为 UI 提供了底层绘制能力（SDF 字体、批次合批、RenderTarget），阶段 6 需要在此基础上构建 Retained-Mode UI 的渲染核心——支持更复杂的矢量图元、9-slice 贴图、高频状态变化下的稳定帧率，以及与自研 UI 框架的完整对接。
>
> 在前往阶段 6 之前，你应该已经能在屏幕角落看到自研 2D 管线渲染的文字和按钮，并且确认它与 3D 场景、后处理、ImGui 的叠加顺序正确无误。
