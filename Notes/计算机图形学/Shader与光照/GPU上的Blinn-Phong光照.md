---
title: 光照在管线中的位置
description: 模型已经能画到 GPU，但看起来还是一块纯色塑料。理解光照只是 Shader 数据流的一个应用实例：Blinn-Phong 是大白话层面的经验模型，现代引擎已经全面转向 PBR；重点不是背公式，而是标出光照计算在 GPU 管线中的位置、输入和输出。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - glsl
  - blinn-phong
  - shading
  - lighting
  - material
  - pbr
aliases:
  - GPU Blinn-Phong
  - Blinn-Phong光照
  - 光照着色器
  - 光照在管线中的位置
  - 从 Blinn-Phong 到 PBR
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/Shader与光照/Shader程序与跨阶段数据约定|Shader程序与跨阶段数据约定]] — 你已经理解 Shader 的跨阶段数据约定
> - [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|数据通道与更新频率]] — 你已经能按数据频率选择 Attribute / Uniform / Varying
> - [[Notes/计算机图形学/GPU编程基础/从OBJ文件到第一个模型|从 OBJ 文件到第一个模型]] — 你已经能把带法线的模型上传到 GPU
>
> **本模块要解决的像素问题**：屏幕上出现的是一个能旋转的模型，但每个面都一样亮，没有立体感。我们想让像素根据"光源方向、表面朝向、相机位置"改变颜色——这个计算在 GPU 管线的哪个阶段发生？需要哪些输入？从经验模型到物理模型，光照架构是怎么演进的？
>
> **本模块增量**：你能用大白话解释 Blinn-Phong / PBR 的核心思想，能在管线图中标出光照计算的位置和输入输出，能判断为什么现代引擎用 PBR 替代 Blinn-Phong。
>
> **下一步**：[[Notes/计算机图形学/Shader与光照/法线矩阵与法线变换|法线变换的几何直觉]] — 光照算出来了，但模型一旋转/缩放，光照方向就错——法线为什么不能用顶点同款矩阵变换？

---

# 光照在管线中的位置

## 问题 0：模型已经画出来了，但为什么像一块彩色塑料？

你在 [[Notes/计算机图形学/GPU编程基础/从OBJ文件到第一个模型|从 OBJ 文件到第一个模型]] 里已经能把一个带法线的立方体上传到 GPU。现在屏幕上出现了一个旋转的立方体——但每个面都是同一个颜色，没有明暗，没有立体感。

**最 naive 的方案**：片段着色器直接输出一个固定颜色。

```glsl
out vec4 FragColor;
uniform vec3 uObjectColor;

void main() {
    FragColor = vec4(uObjectColor, 1.0);
}
```

**立刻发现的问题**：立方体的六个面完全分不出朝向，旋转时只能靠轮廓判断深度。没有任何"被光照射"的暗示。

**根本原因**：像素颜色还没有和「光线方向」「表面朝向」「相机位置」建立关系。**光照计算就是建立这种关系的计算**。它不是新对象，而是片段着色器里的一段代码。

---

## 问题 1：光照计算在 GPU 管线的哪个位置？

我们已经知道 GPU 管线大致是：

```
顶点着色器 → 图元装配 → 光栅化器 → 片段着色器 → 输出合并
```

光照计算放在**片段着色器**里。为什么？

### 为什么不能放在顶点着色器里？

顶点着色器每个顶点执行一次。如果在这里算光照，然后插值到像素，会出现两个问题：

1. **高光会丢失**。一个三角形内部可能有高光峰值，但顶点没算到，插值后高光就消失了。
2. **方向向量不能线性插值**。法线、光线方向、视线方向直接线性插值会得到错误结果，导致三角形内部的光照歪掉。

所以顶点着色器只负责把**世界空间位置**和**世界空间法线**传给片段着色器；真正决定像素颜色的光照计算，在片段着色器里对每个像素重新做。

```
顶点着色器：aPos, aNormal  →  vWorldPos, vNormal
                              ↓
                         光栅化器插值
                              ↓
片段着色器：vWorldPos, vNormal + Uniform（光源、相机、材质） → FragColor
```

---

## 问题 2：光照的本质是什么？——给每个像素一个"被光照射后的颜色"

真实世界里，我们看到物体表面的亮度取决于光线怎么被表面反射。简化到可实时计算，通常拆成三项：

| 分量 | 大白话 | 解决什么像素问题 |
|------|--------|----------------|
| **环境光 Ambient** | 间接光照的偷懒近似 | 背光面不会纯黑 |
| **漫反射 Diffuse** | 粗糙表面把光均匀散射出去 | 朝向光源的一面更亮 |
| **镜面高光 Specular** | 光滑表面像镜子一样反射光线 | 出现亮斑、反光 |

Blinn-Phong 就是这三项的一个具体实现：

- 漫反射用「法线 n 和光线方向 l 的夹角余弦」：朝向光源越正，越亮。
- 高光用「法线 n 和半程向量 h 的夹角余弦」：视线越接近镜面反射方向，越亮。
- 环境光是一个常数，保证背光面有基本亮度。

> **不需要背公式**。核心直觉只有一句：**像素亮度 = 环境底光 + 朝向光源的程度 + 镜面反射的集中程度**。

---

## 问题 3：Blinn-Phong 的局限性——它只是经验模型

Blinn-Phong 在 20 年前是工业标准，但今天的主流引擎（UE、Unity、Godot 4、Bevy 的 PBR 管线）已经不用它作为默认光照模型。为什么？

### 三个致命问题

1. **参数不物理**
   - `shininess` 和 `specularStrength` 是魔法数字。美术调一个"看起来像金属"的材质，换一个光照环境就得重调。

2. **能量不守恒**
   - 漫反射 + 高光 + 环境光可以任意叠加，结果可能过曝或违反能量守恒。真实世界里，反射出去的光不可能比入射光多。

3. **无法表达真实材质**
   - 金属、塑料、石头、泥土在 Blinn-Phong 里只是"颜色 + 光泽度"的组合，没有物理区分。金属和塑料对光的反射机制完全不同，Blinn-Phong 用一个公式糊弄过去了。

> **结论**：Blinn-Phong 是"让像素看起来有光"的最小经验模型。学习它的价值在于理解"光照计算发生在片段着色器、需要法线/光源/相机三个输入"这个管线位置，而不是把它当成最终方案。

---

## 问题 4：PBR 的核心思想——用物理约束替代经验参数

PBR（Physically Based Rendering）不是更复杂的 Blinn-Phong，而是**换了一套约束**。

### PBR 与 Blinn-Phong 的本质区别

| 维度 | Blinn-Phong | PBR |
|------|-------------|-----|
| 参数意义 | 魔法数字（shininess、specular） | 物理属性（metalness、roughness、albedo） |
| 能量守恒 | 不保证 | 必须满足 |
| 金属/非金属 | 用同一个模型，靠参数区分 | 金属没有漫反射，非金属没有镜面反射 |
| 换光照环境 | 材质参数需要重调 | 同一份资产在任何光照下表现一致 |
| 环境光 | 常数近似 | 用 IBL（环境贴图积分）获取真实环境光照 |

### PBR 的直观理解

PBR 说：**一个材质的最终颜色 = 它本身是什么（albedo） + 它有多粗糙（roughness） + 它是不是金属（metalness） + 周围有什么光**。

- **金属**：镜面反射强，颜色来自环境光（所以金属的 albedo 影响镜面颜色）。
- **非金属**（塑料、石头、木头）：漫反射强，镜面反射是灰白色的。
- **粗糙度**：决定镜面反射有多模糊。roughness = 0 像镜子，roughness = 1 像漫反射。

> 这就是现代引擎材质系统的底层逻辑。Unity/UE 的材质编辑器里，`BaseColor`、`Metallic`、`Roughness`、`Normal` 这些参数不是随便取的，它们对应 PBR 模型里的物理量。

---

## 问题 5：从 Blinn-Phong 到 PBR 的架构演进

这个演进不是"公式变复杂了"，而是**指导思想变了**：

```
Blinn-Phong 时代：
  "我想让这个像素看起来亮一点，所以加一个高光项。"

PBR 时代：
  "这个表面是粗糙金属，根据微表面模型和能量守恒，它应该反射多少光？"
```

### 演进的三条主线

1. **从"看起来对"到"物理上对"**
   - Blinn-Phong 追求的是"调参数调出好看效果"。
   - PBR 追求的是"给定物理属性，自动算出正确结果"。

2. **从"单一光源公式"到"环境积分"**
   - Blinn-Phong 只算一个或几个方向光源。
   - PBR 用 IBL 把"整个天空"当作光源，通过预积分环境贴图来近似半球光照。

3. **从"每个着色器一个光照模型"到"统一材质工作流"**
   - 以前不同效果要写不同 Shader。
   - 现在引擎用统一的 PBR Shader，通过参数和贴图表达不同材质。

> **对自研引擎的启示**：你不需要自己实现一个完美的 PBR。但你的材质系统必须预留 `albedo/metallic/roughness/normal` 这套语义，因为你的资产管线迟早会对接 PBR 工作流。

---

## 问题 6：最小代码长什么样？——只看结构，不看细节

下面这段代码展示光照计算在管线中的位置。你不需要记住每一个函数名，只需要看懂信息流动：

```glsl
// 顶点着色器：只做坐标变换和法线传递
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
```

```glsl
// 片段着色器：用 Uniform 传进来的光源/相机/材质参数决定像素颜色
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform vec3 uLightColor;
uniform vec3 uObjectColor;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    vec3 ambient = 0.1 * uLightColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(result, 1.0);
}
```

> 这段代码的价值不是"能跑"，而是展示：**光照计算在片段着色器、用 Uniform 传光源和相机、用 Attribute 传顶点位置和法线、顶点着色器只负责准备世界空间数据**。

---

## 与现代 API 的对照

| 概念 | OpenGL (GLSL) | Vulkan / D3D12 / Metal |
|------|---------------|------------------------|
| 每顶点数据 | Attribute + VAO/VBO | Vertex Buffer + Vertex Input Layout |
| 每 Draw Call 常量 | `uniform` | Push Constants / Constant Buffer / DescriptorSet |
| 顶点→片段数据 | `out` / `in` | Location / Semantic |
| 光照计算位置 | Fragment Shader | Fragment / Pixel Shader |

**核心洞察**：不同 API 争的不是"光照怎么算"，而是"**数据怎么传到 Shader**"和"**阶段接口怎么约定**"光照公式本身几乎可以原样搬到任何 API。

---

## 与 SelfGameEngine 的关系

### 光照是材质系统的最小可运行版本

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被拆成 Template-Asset-Instance 三层：

- **Shader 源码** ≈ Material Template：定义有哪些数据通道、光照怎么算。
- **Uniform 参数集**（`uObjectColor`、`uLightPos` 等）≈ Material Asset / Instance：美术可调的值。
- **运行时绑定** ≈ Material Instance 在 GPU 上的具体执行。

Blinn-Phong 材质和 PBR 材质在引擎里的结构是**一样的**：都是 Shader + 参数集。区别只在于 Shader 里的光照公式和参数语义。

### 从最小版本到 PBR 的迁移路径

你的引擎可以先实现一个最小光照 Shader（Blinn-Phong 级别），验证"材质参数 → Uniform → GPU"这条链路通了。然后逐步把参数从 `shininess` 替换成 `metallic/roughness`，把环境光常数替换成 IBL 查询——**架构不变，公式升级**。

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| 光照计算位置 | 放在片段着色器（逐像素） |
| 数据频率 | 顶点位置和法线走 Attribute；光源、相机、材质参数走 Uniform |
| 空间一致性 | 法线、光源方向、视线方向必须在同一空间计算 |
| 法线变换 | 非均匀缩放时必须用法线矩阵（详见 [[Notes/计算机图形学/Shader与光照/法线矩阵与法线变换|法线变换的几何直觉]]） |
| 模型演进 | Blinn-Phong 是经验模型，PBR 是物理约束模型 |
| 引擎预留 | 材质系统应支持 `albedo/metallic/roughness/normal` 语义 |

---

## 本模块还缺什么？

| 已建立 | 待深入 |
|--------|--------|
| 光照在管线中的位置 | 法线为什么需要特殊变换 |
| Blinn-Phong 大白话 | 用纹理替代纯色（漫反射贴图/法线贴图） |
| PBR 思想演进 | PBR 的完整数学实现（[[Notes/计算机图形学/现代渲染技术/PBR基础|PBR基础]]） |
| 数据通道选择 | 多光源场景怎么组织 Uniform / Constant Buffer |

> **下一步**：[[Notes/计算机图形学/Shader与光照/法线矩阵与法线变换|法线变换的几何直觉]] — 光照让立方体有了立体感，但模型一缩放法线就错。下一篇解决这个几何问题。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
