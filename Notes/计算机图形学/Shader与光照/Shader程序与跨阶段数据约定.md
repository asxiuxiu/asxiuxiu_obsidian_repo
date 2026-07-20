---
title: Shader 程序与跨阶段数据约定
description: Shader 不是 GLSL 的语法细节，而是一类运行在 GPU 上的程序。理解它的本质：为什么必须是独立程序、跨阶段数据如何约定、不同 API 如何用不同语法表达同一套数据流契约。
date: 2026-07-20
tags:
  - graphics
  - shader
  - glsl
  - hlsl
  - spirv
  - pipeline
  - interpolation
aliases:
  - Shader 数据流
  - 着色器跨阶段数据
  - Shader 程序本质
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算|用 Shader 表达逐像素计算]] — 你已经理解顶点/片段着色器的分工
> - [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]] — 你已经理解顶点属性如何从 VBO 流进顶点着色器
> - [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] — 你已经理解重心坐标和属性插值
>
> **本模块要解决的像素问题**：我们已经知道 GPU 用两段小程序替代软渲染器的 `computeColor()`。但 Shader 不是 C++，它必须被单独编译、阶段之间必须显式约定数据接口。这个约定的本质是什么？为什么不同 API 的语法不同，但数据流动的方式一样？
>
> **本模块增量**：你能把 Shader 从「某种具体 API 的语法」还原成「跨阶段数据约定 + 编译产物」，能在 GLSL/HLSL/MetalSL/SPIR-V 之间做概念映射，能独立判断一个数据应该走 Attribute、Uniform 还是 Stage Input/Output。
>
> **下一步**：[[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform 与 Vertex Attribute]] — 数据接口懂了，但 MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？光源位置呢？

---

# Shader 程序与跨阶段数据约定

## 问题 0：我们已经在用 Shader 画画了，为什么还要单独讲「Shader 程序」？

在 [[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算|用 Shader 表达逐像素计算]] 里，你已经见过 Shader 长什么样：顶点着色器算位置、片段着色器算颜色，中间用 `vColor` 传递数据。那段笔记讲的是「怎么用 Shader 表达像素计算」。

但这里有一个更基础的问题被默认了：**为什么 GPU 上的计算必须写成这种独立的小程序？** 为什么不能直接写一段 C++，让编译器把它编译成 GPU 指令？为什么顶点着色器和片段着色器之间要特别声明 `in`/`out`？为什么一段文本要经过「编译」和「链接」两个阶段？

这些问题不解决，你写的 Shader 永远是「某种 API 的语法」而不是「可迁移的知识」。本篇的目标是把 Shader 还原成它的本质：**一段运行在 GPU 上的数据并行程序，以及它和 CPU 代码、和 GPU 管线其他阶段之间的数据约定**。

---

## 问题 1：Shader 为什么必须是独立程序，而不能是普通 C++？

### 最 naive 的想法：把 C++ 函数标记成「在 GPU 上跑」

假设我们可以这样写：

```cpp
// 想象中的 GPU 函数
[[gpu]] vec4 shadeFragment(vec3 normal, vec3 lightDir) {
    float diff = max(dot(normal, lightDir), 0.0);
    return vec4(diff, diff, diff, 1.0);
}
```

然后编译器自动把它编译成 GPU 指令。听起来很美好，但 GPU 和 CPU 的执行模型差异太大，C++ 的很多假设在 GPU 上不成立。

**GPU 程序和普通 C++ 函数的核心差异**：

| 维度 | CPU 程序（C++） | GPU Shader |
|---|---|---|
| 执行频率 | 调用一次执行一次 | 每个顶点/像素自动展开成千上万份 |
| 内存模型 | 可以任意取地址、递归、动态分配 | 没有栈、没有递归、内存访问高度受限 |
| 并行约束 | 顺序执行，假设单线程 | 同一批线程必须执行相同指令流（SIMT） |
| 输入来源 | 函数参数、全局变量、堆 | 固定的「阶段输入」：Attribute、Uniform、Stage Input |
| 输出去向 | 返回值、引用参数、全局状态 | 固定的「阶段输出」：`gl_Position`、`FragColor` 等 |
| 调用方式 | 被其他代码显式调用 | 被 GPU 硬件阶段隐式触发 |

这些差异不是「编译器不够智能」，而是 GPU 的硬件设计使然。Shader 语言之所以存在，是因为我们需要一种**显式表达数据并行、固定输入输出、无递归无动态内存**的程序形式。

> 现代 C++ 确实有一些 GPU 扩展（如 CUDA、SYCL），但它们针对的是通用并行计算，不是图形管线。图形 Shader 之所以用专门语言，是因为它必须和光栅化器、深度测试、混合这些固定硬件阶段精确对接。

---

## 问题 2：Shader 程序的本质契约是什么？

不管用 GLSL、HLSL、MetalSL 还是 SPIR-V，一个 Shader 程序必须回答三个问题：

1. **你运行在哪个阶段？** —— 顶点、片段、几何、计算……每个阶段有不同的调用频率和输入输出义务。
2. **你的输入从哪里来？** —— 顶点属性（per-vertex）、常量（per-draw/per-pass）、上一个阶段的输出（per-fragment interpolated）。
3. **你的输出到哪里去？** —— 位置、颜色、深度，或者下一个阶段的输入。

这三个问题构成了 **Shader 的跨阶段数据约定**。API 的语法只是在不同地方声明这个约定。

```
软渲染器里的 computeColor()：
  输入：重心坐标、三角形顶点属性、光源、材质
  输出：一个颜色

GPU Shader 的等价契约：
  顶点着色器：输入顶点属性 + Uniform，输出裁剪空间位置 + 需要插值的属性
  光栅化器（固定硬件）：把顶点输出插值成片段输入
  片段着色器：输入插值后的属性 + Uniform，输出颜色 + 可选深度
```

注意：**光栅化器是这个约定里不可编程的部分**。它保证「顶点着色器输出什么，片段着色器就按重心坐标插值后收到什么」。Shader 语言要做的，就是让编译器能验证这个接口是否匹配。

---

## 问题 3：跨阶段数据约定具体长什么样？

### 约定的三个要素

```
顶点着色器输出          片段着色器输入
┌─────────────┐         ┌─────────────┐
│ out vec3    │         │ in vec3     │
│ vNormal;    │────────▶│ vNormal;    │
└─────────────┘         └─────────────┘
      │                        │
      │   光栅化器插值          │
      ▼                        ▼
   每顶点一个值            每像素一个插值
```

一个合法的跨阶段约定必须满足：

1. **类型匹配**：顶点输出 `vec3`，片段输入也必须是 `vec3`（或兼容类型）。
2. **频率匹配**：顶点阶段输出的是「每顶点」数据，片段阶段收到的是「每像素插值后」数据——你不能在顶点阶段输出一个已经是「每像素」的值。
3. **命名或位置匹配**：GLSL 早期靠变量名匹配；现代 API 普遍用显式 location，因为变量名在编译后可能丢失，也不利于二进制中间格式。

> 类型匹配和频率匹配是**语义约束**，命名/位置匹配是**链接约束**。编译器先检查语法和类型，链接器再检查阶段接口。

### 为什么需要「显式 location」？

```glsl
// GLSL 330+
layout (location = 0) out vec3 vNormal;
layout (location = 1) out vec2 vUV;

// 片段着色器
layout (location = 0) in vec3 vNormal;
layout (location = 1) in vec2 vUV;
```

显式 location 的好处：
- 不依赖变量名，Stage 接口可以独立演进
- SPIR-V/DXIL 等二进制中间格式天然靠 location/semantic 匹配
- 引擎做 Shader 变体时，可以只改源码不改接口布局

这对应到跨 API 思维：GLSL 的 `layout(location=N)`、HLSL 的 `SV_Position` / 自定义 semantic、SPIR-V 的 `Location` decoration，都是同一种约定的不同写法。

---

## 问题 4：数据通道按更新频率分层——Attribute、Uniform、Stage Input 的本质区别

在前面的笔记里你已经见过三种数据入口。现在从「约定」的角度重新看它们：

| 数据通道 | 频率 | 来源 | 在 Shader 中的角色 |
|---|---|---|---|
| Attribute / Vertex Input | 每顶点不同 | VBO / Vertex Buffer | 顶点着色器的「逐顶点输入」 |
| Uniform / Constant | 每 Draw Call / 每 Pass 内恒定 | CPU 设置 / Constant Buffer | 所有 Shader 阶段的「全局只读常量」 |
| Stage Input / Varying | 每像素不同（由硬件插值） | 上一阶段输出 | 片段着色器的「逐像素输入」 |

这个分层不是 GLSL 特有的，而是所有图形 API 的共同设计：

- **Attribute**：解决「每个顶点有自己的位置/法线/UV」
- **Uniform**：解决「一次绘制中所有顶点/像素共享 MVP、光源、材质参数」
- **Stage Input**：解决「顶点阶段算出来的值，怎么按像素频率传给片段阶段」

> 下篇笔记 [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform 与 Vertex Attribute]] 会专门讲 Attribute 和 Uniform 的具体 API、数量限制、`std140` 对齐。本篇只建立「为什么分三条通道」的概念框架。

---

## 问题 5：Shader 为什么要分「编译」和「链接」两个阶段？

你已经知道一段 GLSL 要经过 `glCompileShader` 和 `glLinkProgram`。但概念上，这两个阶段各自在做什么？

### 编译阶段：检查单个 Shader 是否合法

编译只关心**当前阶段**：
- 语法是否正确
- 类型是否匹配
- 是否使用了本阶段不存在的功能（如在顶点着色器里写 `discard`）
- 是否超过本阶段的资源限制（如顶点属性数量）

### 链接阶段：检查多个 Shader 组合起来是否合法

链接关心**阶段之间的契约**：
- 顶点输出和片段输入的类型/位置是否匹配
- Uniform 名是否一致、位置是否分配
- 是否有必须输出的变量没有声明（如片段着色器必须有颜色输出）
- 是否有未使用的输入被优化掉，导致接口不一致

这个划分不是 OpenGL 的怪癖，而是所有 Shader 系统的共同需求：

```
源码文本
   │
   ▼
┌─────────────┐   按阶段检查语法/类型/限制
│  单阶段编译  │
└──────┬──────┘
       │
       ▼
┌─────────────┐   匹配阶段接口、分配资源槽位、生成最终 GPU 二进制
│  多阶段链接  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 可执行程序   │
└─────────────┘
```

Vulkan/D3D12 把这个过程拆得更显式：Shader 先离线编译成 SPIR-V/DXIL 字节码，运行时再和渲染状态一起打包成 PSO。但「单阶段检查 + 多阶段组合验证」的本质没有变。

---

## 问题 6：GLSL、HLSL、MetalSL、SPIR-V 到底在争什么？

它们不是四种不同的「Shader 技术」，而是**同一套跨阶段数据约定**的四种不同语法/中间表示。

### 同一个最小 Shader 的四种写法

顶点着色器输出位置和颜色：

```glsl
// GLSL
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 0) out vec3 vColor;
uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
```

```hlsl
// HLSL
struct VSInput {
    float3 pos : POSITION;
    float3 color : COLOR;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 color : COLOR;
};

cbuffer PerObject : register(b0) {
    float4x4 uMVP;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = mul(float4(input.pos, 1.0), uMVP);
    output.color = input.color;
    return output;
}
```

```metal
// Metal Shading Language
#include <metal_stdlib>
using namespace metal;

struct VSInput {
    float3 pos [[attribute(0)]];
    float3 color [[attribute(1)]];
};

struct VSOutput {
    float4 pos [[position]];
    float3 color;
};

vertex VSOutput main(VSInput in [[stage_in]], constant float4x4& uMVP [[buffer(0)]]) {
    VSOutput out;
    out.pos = uMVP * float4(in.pos, 1.0);
    out.color = in.color;
    return out;
}
```

### 它们映射到同一套概念

| 概念 | GLSL | HLSL | MetalSL | SPIR-V |
|---|---|---|---|---|
| 顶点属性 | `layout(location=N) in` | `POSITION` / `TEXCOORD` semantic | `[[attribute(N)]]` | `Input` variable with `Location` decoration |
| 每 Draw Call 常量 | `uniform` / UBO | `cbuffer` / `ConstantBuffer` | `[[buffer(N)]]` constant | `UniformConstant` / PushConstant |
| 顶点→片段输出 | `layout(location=N) out/in` | `struct` with custom semantic | `vertex` return struct | `Output`/`Input` with `Location` |
| 裁剪空间位置 | `gl_Position` | `SV_Position` | `[[position]]` | `BuiltIn Position` |
| 片段颜色输出 | `out vec4 FragColor` | `SV_Target` | `[[color(N)]]` | `Output` with `Location` |
| 编译产物 | 驱动私有二进制 | DXIL 字节码 | metallib 二进制 | SPIR-V 中间码 |

### 关键洞察

- **GLSL** 是「源码 + 运行时编译链接」的一体化语言，学习曲线最平缓。
- **HLSL** 把常量显式放进 `cbuffer`，用 semantic 区分系统内置变量和自定义变量，更符合 D3D 的显式哲学。
- **MetalSL** 用 C++14 语法，用 `[[...]]` 属性标注资源绑定和阶段语义，和 Apple 的平台绑定较深。
- **SPIR-V** 不是给人写的，而是给编译器用的**二进制中间表示**。它的意义在于：引擎可以只维护一套 Shader 源码/中间产物，跨平台分发时转成 Vulkan/SPIR-V、D3D/DXIL、Metal/metallib。

> 这就是为什么现代引擎越来越倾向于**离线编译到 SPIR-V/DXIL**，而不是运行时编译 GLSL。对学习者来说，GLSL 仍是最好的入门语言；对工程来说，中间表示才是跨后端的答案。

---

## 问题 7：从「文本」到「GPU 指令」的完整旅程

把上面所有概念串起来，一段 Shader 源码到 GPU 执行经历了什么？

```
CPU 侧：
1. 写源码（GLSL / HLSL / MetalSL）
2. [可选] 预处理器处理 #include / 宏
3. 编译器编译成中间产物：
   - GLSL → 驱动私有二进制（OpenGL 运行时编译）
   - HLSL → DXIL 字节码
   - MetalSL → metallib 二进制
   - 通用路线 → SPIR-V
4. [运行时] 把各阶段 Shader 组合、验证接口、生成最终 PSO / Program

GPU 侧：
5. 绘制命令触发管线
6. 顶点着色器按顶点并行执行
7. 光栅化器按约定插值
8. 片段着色器按像素并行执行
```

这个旅程里，**步骤 3 的编译产物和步骤 4 的阶段接口验证**是 Shader 系统的核心。具体调用 `glCompileShader` 还是 `dxc -T vs_6_0` 只是实现差异。

---

## 问题 8：最常见的错误不是语法错，而是「约定没对好」

新手写 Shader 时，最常遇到的问题往往不是「C 语言语法错了」，而是三类约定错误：

### 错误 1：阶段接口类型不匹配

```glsl
// 顶点
out vec3 vColor;

// 片段
in vec2 vColor;  // 类型 vec2 ≠ vec3 → 链接失败或静默错误
```

### 错误 2：数据频率错配

把应该每 Draw Call 传的 MVP 矩阵放进顶点属性（这正是 Uniform 与 Vertex Attribute 那篇笔记要分析的 naive 方案），或者把应该每顶点传的 UV 当成 Uniform。

### 错误 3：以为 Uniform 是全局共享的

Uniform 是 **Program / PSO 的私有状态**。在 OpenGL 里切换 Program 后 Uniform 会变化；在现代 API 里，Constant Buffer 的绑定跟着命令列表或 DescriptorSet。它不是「设置一次永远有效」的全局变量。

### 错误 4：混淆「源码」和「可执行程序」

源码文件 `.vert` / `.frag` 只是文本；编译后是中间产物；链接/创建 PSO 后才是 GPU 能执行的程序。改源码不自动生效，必须重新走一遍编译链路——这也是引擎需要 Shader 热重载系统的原因。

---

## 与 SelfGameEngine 的关系

### Shader 不是材质，材质也不是 Shader

在引擎的材质系统里，你会看到「材质」被拆成 Template-Asset-Instance 三层。Shader 只是其中的 **Template 层**：它定义了「有哪些数据通道、光照怎么算」。而具体颜色、粗糙度、贴图引用是 **Instance 层** 的参数，通过 Uniform / Constant Buffer 上传。

### 为什么引擎需要 SPIR-V/DXIL 抽象

引擎的 RHI 抽象层与着色器变体系统会讲到：RHI 不应该假设后端一定是 OpenGL。如果 Shader 源码是 GLSL，Vulkan 后端就要额外做 GLSL→SPIR-V 转换；如果源码统一编译成 SPIR-V，OpenGL 后端反而需要 SPIR-V→GLSL 回退或专用路径。这个选择没有唯一正确答案，但理解「SPIR-V 是中间表示、GLSL 只是众多前端之一」是做出正确选择的前提。

---

## 设计 checklist

| 检查项 | 标准 |
|---|---|
| 阶段接口 | 顶点输出和片段输入的类型、location 必须匹配 |
| 数据频率 | 每顶点 → Attribute；每 Draw → Uniform/Constant；每像素插值 → Stage Input |
| 编译产物 | 区分源码、中间表示、可执行 Program/PSO 三个层级 |
| 跨 API 思维 | 能把 GLSL 的 `in/out/uniform` 映射到 HLSL 的 `struct/cbuffer/semantic` |
| 错误排查 | 编译失败看单阶段语法；链接失败看阶段接口；运行结果错看数据频率和绑定 |

---

## 本模块还缺什么？

| 已建立 | 待深入 |
|---|---|
| Shader 作为跨阶段数据约定的本质 | Uniform 和 Attribute 的具体 API 与限制 |
| 编译/链接的概念模型 | UBO / Constant Buffer 的对齐与绑定 |
| 跨 API 语言映射 | Shader 变体、热重载、缓存的引擎实践 |

> **下一步**：[[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform 与 Vertex Attribute]] — 数据接口懂了，但 MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？光源位置呢？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
