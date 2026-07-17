---
title: 用 Shader 表达逐像素计算
description: 软渲染器里一个 computeColor() 函数同时做了顶点变换和逐像素着色。GPU 上必须把这段计算拆成顶点着色器和片段着色器。理解为什么这样拆分，以及数据怎么在两者之间流动。
date: 2026-07-07
tags:
  - graphics
  - shader
  - glsl
  - vertex-shader
  - fragment-shader
  - interpolation
aliases:
  - Shader 程序
  - 顶点和片段着色器
  - GLSL 数据流
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/GPU编程基础/软渲染器到GPU管线的映射|软渲染器到 GPU 管线的映射]] — 你知道软渲染器的 `computeColor()` 对应 GPU 的片段着色器
> - [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]] — 你知道怎么声明顶点属性布局
>
> **本模块增量**：你能解释为什么 GPU 要把逐像素计算拆成顶点着色器和片段着色器，能说清 in/out/uniform 三种数据通道的区别，能写出一个最小 GLSL 顶点+片段 Shader 并说明数据如何在它们之间流动。
>
> **下一步**：[[Notes/计算机图形学/GPU编程基础/一条绘制命令触发整条流水线|一条绘制命令触发整条流水线]] — Shader 写好了，怎么触发 GPU 从"准备好的顶点数据"走到"屏幕上的像素"？

---

# 用 Shader 表达逐像素计算

## 问题 0：软渲染器的 `computeColor()` 在 GPU 上怎么表达？

在软渲染器里，计算颜色是一个 C++ 函数：

```cpp
Vec3 computeColor(Vec3 bary, Triangle tri, Lights lights, Material mat) {
    // 1. 用重心坐标插值出当前像素的 UV、法线、世界位置
    Vec2 uv = interpolate(bary, tri.uvs);
    Vec3 normal = interpolate(bary, tri.normals);
    Vec3 worldPos = interpolate(bary, tri.worldPositions);

    // 2. 采样纹理
    Vec3 albedo = sampleTexture(mat.diffuseMap, uv);

    // 3. 计算光照
    Vec3 color = blinnPhong(albedo, normal, worldPos, lights);

    return color;
}
```

这个函数被 CPU 对每个像素调用一次。输入是：
- 当前像素的重心坐标
- 三角形的顶点属性
- 全局常量（光源、材质）

在 GPU 上，这段逻辑被拆成两段程序：**顶点着色器**和**片段着色器**。

---

## 问题 1：为什么 GPU 要把计算拆成两段？

软渲染器里，一个函数完成所有事情。但 GPU 的并行模型要求：

1. **顶点处理是每顶点并行**：每个顶点执行一次，输入是顶点属性，输出是裁剪空间位置
2. **片段处理是每像素并行**：每个像素执行一次，输入是插值后的属性，输出是颜色

这两者之间有固定硬件——**光栅化器**负责把顶点输出插值成片段输入。

```
顶点数据
   │
   ▼
┌─────────────┐
│ 顶点着色器   │  ← 每个顶点执行一次
│ 输出：位置   │
│      + 颜色 │
│      + UV   │
└─────────────┘
   │
   ▼
光栅化器（固定硬件）
   │  插值：把顶点属性变成每个像素的属性
   ▼
┌─────────────┐
│ 片段着色器   │  ← 每个像素执行一次
│ 输出：颜色   │
└─────────────┘
```

**关键原因**：GPU 的并行粒度不同。顶点并行和像素并行需要不同的线程组织方式，由固定硬件光栅化器把它们衔接起来。

---

## 问题 2：顶点着色器里放什么？

顶点着色器负责**每个顶点**的计算：

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
    vColor = aColor;
}
```

**输入**：
- `in vec3 aPos`：顶点位置（来自顶点缓冲区，由 VAO 解释格式）
- `in vec3 aColor`：顶点颜色（同样来自顶点缓冲区）
- `uniform mat4 mvp`：每个 Draw Call 共享的 MVP 矩阵

**输出**：
- `gl_Position`：裁剪空间位置，必须输出
- `out vec3 vColor`：用户自定义的逐顶点属性，会被光栅化器插值

> [!faq] CPU 上传的 color 和顶点着色器输出的 color，是同一个东西吗？
> 是——同一份数据在管线不同阶段的位置，不是两个 color。顶点属性只存在于顶点着色器阶段，**片段着色器没有资格直接读顶点缓冲区**，它的输入只能来自顶点着色器的 `out` 经光栅化器插值后的结果。所以顶点着色器必须当"搬运工"，把 `aColor` 写进 `vColor`，数据才能继续流到像素阶段：
>
> CPU 顶点缓冲区 → `in aColor`（顶点着色器输入）→ 直通或加工 → `out vColor`（顶点着色器输出）→ 光栅化器插值 → 片段着色器 `in vColor`
>
> 输出也不必等于输入。比如 `vColor = computeLighting(aPos)` 就是 Gouraud 着色——光照在顶点阶段算好，由硬件把颜色插值到每个像素。
>
> 这个"搬运"规则对**所有**顶点属性都成立，不只是 color。UV、法线、切线……任何片段着色器要用的逐顶点数据，都必须由顶点着色器显式转发（直通，或加工后再输出）：
>
> ```glsl
> // 顶点着色器
> layout(location = 2) in vec2 aUV;
> layout(location = 3) in vec3 aNormal;
> out vec2 vUV;
> out vec3 vNormal;
>
> void main() {
>     vUV = aUV;                                // 直通
>     vNormal = normalMatrix * aNormal;         // 加工：法线变换到世界空间
>     ...
> }
> ```
>
> 片段着色器想用却忘了在顶点着色器里转发，是新手最常见的"数据去哪了"时刻——数据一直都在显存里，但没有 `out` 这座桥，它永远到不了像素阶段。

---

## 问题 3：片段着色器里放什么？

片段着色器负责**每个像素**的计算：

```glsl
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
```

**输入**：
- `in vec3 vColor`：光栅化器插值后的颜色

**输出**：
- `out vec4 FragColor`：最终像素颜色

---

## 问题 4：数据怎么从顶点着色器流到片段着色器？

核心机制：**光栅化插值**。

三个顶点各自输出一个 `vColor`：
- 顶点 A：`vColor = red`
- 顶点 B：`vColor = green`
- 顶点 C：`vColor = blue`

光栅化器对三角形内部的每个像素，根据重心坐标插值出中间的 `vColor`：

```
顶点 A (red)
   \
    \   像素 P 的 vColor = red * wA + green * wB + blue * wC
     \
      C (blue)
     /
    /
   B (green)
```

这就是彩色渐变三角形的来源。

---

## 问题 5：uniform 是什么？

`uniform` 是**每个 Draw Call 共享**的数据。例如 MVP 矩阵、光源位置、相机位置。

```glsl
uniform mat4 mvp;
uniform vec3 lightPos;
```

在 CPU 侧：
```cpp
glUseProgram(shaderProgram);
GLint loc = glGetUniformLocation(shaderProgram, "mvp");
glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
```

**为什么需要 uniform？**

因为 MVP 矩阵对当前绘制的所有顶点都一样。如果把它放进顶点属性，就要给每个顶点都复制一份 16 个 float，浪费显存和带宽。

---

## 问题 6：attribute、varying、uniform 的对应关系

| 数据通道 | 更新频率 | 软渲染器对应物 | GLSL 关键字 |
|---|---|---|---|
| 顶点属性 | 每顶点不同 | `Vertex` 结构体成员 | `in`（顶点着色器） |
| 顶点→片段插值 | 每像素不同（由硬件插值） | 重心坐标插值结果 | `out` / `in` |
| 全局常量 | 每 Draw Call / 每帧 | `mvp` 矩阵、`lightPos` | `uniform` |

---

## 问题 7：不同 API 如何表达 Shader 程序？

| API | Shader 对象 | 管线对象 | 特点 |
|---|---|---|---|
| OpenGL | `GLuint shader` | `GLuint program` | 编译 + 链接成 program，用 `glUseProgram` 绑定 |
| Vulkan | `VkShaderModule` | `VkPipeline` | Shader 模块 + PSO 一起创建 |
| D3D12 | `ID3DBlob` | `ID3D12PipelineState` | PSO 包含 Shader 字节码和完整渲染状态 |
| Metal | `MTLFunction` | `MTLRenderPipelineState` | 从 `MTLLibrary` 取函数，再创建管线状态 |

---

## 问题 8：为什么现代 API 把 Shader 和渲染状态打包成 PSO？

PSO（Pipeline State Object）包含：
- 顶点着色器
- 片段着色器
- 顶点格式解释
- 深度测试模式
- 混合模式
- 剔除模式
- ...

**好处**：
1. 驱动在创建 PSO 时就能做完整验证
2. 运行时切换状态更快（驱动不需要重新推断）
3. 支持多线程录制命令缓冲

---

## 问题 9：MVP + 纹理采样，就是 Shader 的全部了吗？

不是——差得远。本模块的 MVP 和纹理采样只是 Shader 的**第一个用例**，不是它的定义。Shader 的定义是：**GPU 上每顶点/每像素执行一次的通用程序**。你往里填什么计算，它就是什么——同一个骨架，填入光照方程就是 Blinn-Phong，填入微表面模型就是 PBR，填入卷积核就是模糊。

但这里有一个容易踩的坑：很多听起来"就是写 Shader"的高级效果，真正的难点并不在 Shader 里，而在 **Shader 之外的编排**：

- **阴影**：先把场景从光源视角画进一张"深度纹理"（靠离屏渲染目标和固定功能硬件完成），主 Pass 的片段着色器只做最后一步"比较当前像素的深度和深度纹理里存的值"。大头是多 Pass 的组织，不是那几行比较代码。
- **深度图的各种应用**：深度测试本身是光栅化器之后的固定硬件完成的，根本不经过 Shader。Shader 干的是另一件事——把深度存成纹理，再在别的 Pass 里消费它：让粒子被前景柔和遮挡、让远处失焦模糊、让物体接触处变暗。这些都是后续阶段会展开的概念，现在只需知道它们都是"深度纹理消费"这一种模式。
- **粒子**：每个粒子的着色很 trivial（一张贴图 + 颜色渐变），难点在粒子的运动模拟放在哪算（CPU 还是 GPU 的通用计算单元）、成千上万个半透明粒子按什么顺序画（透明混合是固定功能状态，顺序错了画面就错）。
- **水体**：顶点波形起伏、法线扰动确实是 Shader，但水面的反射和折射需要先把场景画到纹理里再采样——又是多 Pass 编排。
- **毛发**：各向异性的光照模型是片段着色器里的数学，但工程痛点是让成千上万根半透明发丝以正确顺序混合。

所以准确的说法是：**本模块建立的可编程模型是地基，Shader 的纵深在地基之上的编排层**——多 Pass、渲染目标、固定功能状态、渲染顺序的组合。路线图的后续阶段基本就是沿着这条线展开的：

| 你可能关心的问题 | 本质 | 在路线中的位置 |
|---|---|---|
| 光照怎么算 | 片段着色器里的光照方程 | [[Notes/计算机图形学/Roadmap#阶段三：GLSL 数据流与光照基础\|阶段三]]、[[Notes/计算机图形学/Roadmap#阶段八：基于物理的渲染（PBR）\|阶段八 PBR]] |
| 深度图如何应用 | 深度纹理的生产与消费 | [[Notes/计算机图形学/Roadmap#阶段五：帧缓冲、色彩与后处理\|阶段五]]、[[Notes/计算机图形学/Roadmap#阶段七：阴影\|阶段七]] |
| 粒子怎么做 | 通用计算模拟 + 透明混合排序 | [[Notes/计算机图形学/Roadmap#阶段六：混合、透明与渲染顺序\|阶段六]]、[[Notes/计算机图形学/Roadmap#阶段十二：Compute Shader 与 GPU 通用计算\|阶段十二]] |
| 水体、毛发怎么渲染 | 多 Pass 编排 + 特殊光照模型 | [[Notes/计算机图形学/Roadmap#阶段十三：高级渲染专题\|阶段十三专题]] |
| 几百个光源怎么办 | 把光照搬到屏幕空间统一算 | [[Notes/计算机图形学/Roadmap#阶段九：多光源与延迟渲染\|阶段九]] |
| Shader 多了怎么管理 | 变体、编译缓存、热重载 | [[Notes/计算机图形学/Roadmap#阶段十一：引擎渲染架构\|阶段十一]] |

这些概念现在都不需要懂——列出它们只是为了说明：你手里这个"顶点进、像素出"的模型，比看起来走得更远。

---

## 本模块还缺什么？

| 已建立 | 待实践 |
|---|---|
| 顶点/片段着色器的分工 | Shader 编译链接的具体 API 调用 |
| in/out/uniform 数据流 | 实际触发 GPU 管线的绘制命令 |
| 光栅化插值的直觉 | 与软渲染器 `computeColor()` 的完整对照 |

---

> **下一步**：[[Notes/计算机图形学/GPU编程基础/一条绘制命令触发整条流水线|一条绘制命令触发整条流水线]]
>
> 顶点数据上 GPU 了，格式解释清楚了，Shader 也写好了。但 GPU 不会自动开始工作——你需要一条绘制命令来触发整条流水线。

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
