---
title: PBR 基础
description: Blinn-Phong 能画出明暗，但参数像黑魔法。理解微表面模型、金属/粗糙度工作流、Cook-Torrance BRDF 与能量守恒，把材质从「调参艺术」变成「物理描述」。
date: 2026-06-28
tags:
  - graphics
  - pbr
  - brdf
  - microfacet
  - metallic-roughness
  - cook-torrance
  - glsl
  - material
aliases:
  - Physically Based Rendering
  - PBR 基础
  - 物理渲染基础
  - 微表面模型
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|GPU上的Blinn-Phong光照]] — 你已经能把逐像素光照写进 GLSL
> - [[Notes/计算机图形学/纹理系统/多重纹理与材质|多重纹理与材质]] — 你已经理解 Albedo/Normal/Metallic/Roughness 的槽位分工
>
> **本模块增量**：学完这篇笔记后，你能解释 Blinn-Phong 的局限、用微表面模型理解粗糙度、按金属/粗糙度工作流组织材质参数，并写出简化版 Cook-Torrance BRDF 的 GLSL 实现。
>
> **下一步**：[[Notes/计算机图形学/现代渲染技术/阴影映射|阴影映射]] — PBR 让表面正确响应光，但物体之间还缺少遮挡关系。下一步让光也「知道」被挡住。

---

# PBR 基础

## 问题 0：Blinn-Phong 能跑通，但为什么美术还是调不出「真实感」？

你在 [[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|GPU上的Blinn-Phong光照]] 里已经实现了完整的光照：环境光 + 漫反射 + 高光。模型会转、会亮、会有反光点。但如果你让美术做一个「生锈金属」或「磨砂塑料」，问题马上出现。

**最 naive 的方案**：给美术一堆 Blinn-Phong 参数，让他们反复试错。

```glsl
uniform vec3 uObjectColor;      // 物体颜色
uniform float uShininess;       // 光泽度
uniform float uSpecularStrength;// 高光强度
```

**立刻发现的问题**：

- **参数没有物理意义**：`shininess = 32` 到底对应「多光滑」？塑料、金属、石头各自该填多少？全靠手感。
- **换一盏灯就崩**：同一个材质在暖光下好看，换到冷光下可能过曝或发灰，因为漫反射和高光的能量分配是硬编码的。
- **金属画不像**：Blinn-Phong 里金属只是「高光强一点、颜色白一点」，但真实金属的镜面反射会带颜色（比如金、铜），而且金属没有漫反射。
- **能量不守恒**：你完全可以把 `uSpecularStrength` 调得极高，让反射光超过入射光，整个画面「亮得不自然」。

**根本原因**：Blinn-Phong 是**经验模型**——它不是从光的物理行为推导出来的，而是程序员调出来的「看起来像」的公式。要让材质在不同光照下都稳定、可预测，需要一套基于物理的描述方式。

这就是 **PBR（Physically Based Rendering，基于物理的渲染）** 要解决的问题。

---

## 问题 1：PBR 到底是什么？它和「物理仿真」是一回事吗？

**PBR 不是物理仿真。** 它不会追踪每一束光子的路径，也不会解麦克斯韦方程。它是一套**比 Blinn-Phong 更贴近物理直觉的近似模型**，核心目标有三个：

1. **基于微表面模型（Microfacet Model）**：把表面看成无数微小镜面的统计集合。
2. **能量守恒（Energy Conservation）**：反射光总量不超过入射光。
3. **使用物理上可解释的 BRDF（Bidirectional Reflectance Distribution Function）**：描述光线从入射方向到出射方向的分布。

> **人话解释 BRDF**：它回答「一束光从某个方向照进来，有多少能量会朝我看过去的方向反射出去」。Blinn-Phong 也可以被写成 BRDF 的形式，但它不是能量守恒的，所以不算 PBR。

工业界最常用的 PBR BRDF 是 **Cook-Torrance**。它把反射拆成两部分：

$$
f_r = k_d \, f_{\text{diffuse}} + k_s \, f_{\text{cook-torrance}}
$$

- $k_d$：入射光中被**折射**进表面、再漫反射出来的比例（对应非金属的「颜色」）。
- $k_s$：入射光中被**直接镜面反射**的比例。
- $f_{\text{diffuse}}$：漫反射项，通常用 Lambertian 模型 $\frac{\text{albedo}}{\pi}$。
- $f_{\text{cook-torrance}}$：镜面反射项，由微表面模型的 D/F/G 三项组成。

**关键约束**：$k_d + k_s \leq 1$。也就是说，光要么漫反射出去，要么镜面反射出去，不能凭空变多。这正是能量守恒的数学表达。

---

## 问题 2：微表面模型怎么理解？粗糙度到底是什么？

真实世界没有绝对光滑的表面。即使是一面镜子，在显微镜下也布满微小的凹凸。PBR 假设：**宏观表面由无数取向各异的微小镜面（microfacet）组成**。

- **光滑表面**：大部分微表面的法线方向一致，镜面反射集中在一个方向，高光小而锐利。
- **粗糙表面**：微表面法线方向分散，反射光朝各个方向散射，高光大而柔和。

**粗糙度（Roughness）** 就是描述这种法线方向分散程度的参数。值为 0 时所有微表面对齐，形成完美镜面；值为 1 时法线完全随机，接近漫反射。

这个模型解释了为什么 Blinn-Phong 里的 `shininess` 不直观：它只是一个「高光集中度」的经验指数，而粗糙度有明确的物理图像——**微表面法线的统计分布**。

Cook-Torrance 的镜面项写作：

$$
f_{\text{cook-torrance}} = \frac{D(\mathbf{h}) \, F(\mathbf{v}, \mathbf{h}) \, G(\mathbf{l}, \mathbf{v})}{4 \, (\mathbf{n} \cdot \mathbf{l}) \, (\mathbf{n} \cdot \mathbf{v})}
$$

其中：

- $\mathbf{n}$：宏观法线
- $\mathbf{l}$：指向光源的方向
- $\mathbf{v}$：指向相机的方向
- $\mathbf{h} = \frac{\mathbf{l} + \mathbf{v}}{\|\mathbf{l} + \mathbf{v}\|}$：半程向量（和 Blinn-Phong 里一样）

三个函数 D、F、G 分别回答三个问题。

---

## 问题 3：D/F/G 三个函数到底在算什么？

### D —— 法线分布函数（Normal Distribution Function, NDF）

**问题**：微表面法线朝向半程向量 $\mathbf{h}$ 的比例是多少？

粗糙度越低，法线越集中；粗糙度越高，法线越分散。最常用的是 **GGX（Trowbridge-Reitz）** 分布：

$$
D_{\text{GGX}}(\mathbf{h}) = \frac{\alpha^2}{\pi \, ((\mathbf{n} \cdot \mathbf{h})^2 \, (\alpha^2 - 1) + 1)^2}
$$

其中 $\alpha = \text{roughness}^2$（迪士尼的做法，让低粗糙度区域变化更平缓）。

> **直觉**：D 越大，表示越多微表面的法线刚好能把光「镜面反射」进相机，像素就越亮。

### F —— 菲涅尔项（Fresnel）

**问题**：入射光有多少被反射，有多少折射进表面？

这取决于**入射角**。垂直看水面时，大部分光透进去，水面几乎透明；贴近水面看时，反射骤然变强，水面像镜子。这就是菲涅尔效应。

Schlick 近似公式：

$$
F_{\text{Schlick}}(F_0, \mathbf{v}, \mathbf{h}) = F_0 + (1 - F_0) \, (1 - (\mathbf{v} \cdot \mathbf{h}))^5
$$

- $F_0$：垂直入射时的基础反射率。
  - 非金属（塑料、石头、木头）：$F_0 \approx 0.04$（灰度）。
  - 金属：$F_0$ 是有颜色的，比如金 $(1.0, 0.78, 0.34)$、铜 $(0.95, 0.64, 0.54)$。

### G —— 几何遮蔽函数（Geometry / Shadowing-Masking）

**问题**：微表面之间互相遮挡，导致部分光无法到达或被反射出去，损失了多少？

粗糙表面凹凸剧烈，入射光和出射光都可能被 neighboring 微表面挡住。GGX-Smith 几何函数：

$$
G_{\text{GGX}}(\mathbf{n}, \mathbf{l}, \mathbf{v}) = G_{\text{GGX}}(\mathbf{n} \cdot \mathbf{l}) \cdot G_{\text{GGX}}(\mathbf{n} \cdot \mathbf{v})
$$

其中：

$$
G_{\text{GGX}}(n \cdot x) = \frac{n \cdot x}{(n \cdot x) \, (1 - k) + k}, \quad k = \frac{(\alpha + 1)^2}{8}
$$

> **直觉**：G 让掠射角（grazing angle）和粗糙表面的反射变暗，避免能量虚高。没有 G，BRDF 会在某些角度反射超过 100% 的光。

---

## 问题 4：为什么用金属/粗糙度工作流，而不是高光/光泽度？

Blinn-Phong 用「高光强度 + 光泽度」描述材质，但这两个参数都和物理量没有直接对应。现代 PBR 更常用 **Metallic-Roughness 工作流**（glTF、UE、Unity 默认）。

它用两张核心纹理回答两个简单问题：

| 纹理 | 问题 | 取值含义 |
|------|------|---------|
| **Albedo / Base Color** | 这个表面本身是什么颜色？ | 非金属：漫反射颜色；金属：镜面反射颜色 |
| **Metallic** | 它是金属吗？ | 0 = 绝缘体（塑料/石头/木头），1 = 金属 |
| **Roughness** | 表面有多粗糙？ | 0 = 镜面，1 = 接近漫反射 |

**核心规则**：

- 当 `metallic = 0` 时，Albedo 是**漫反射颜色**，$F_0 = 0.04$，有漫反射、有菲涅尔镜面。
- 当 `metallic = 1` 时，Albedo 是**镜面反射颜色**（金属的固有色），$F_0 = \text{albedo}$，**没有漫反射**（$k_d = 0$）。
- 中间值只应在过渡区域（如灰尘覆盖的金属），大多数像素应该是 0 或 1。

```glsl
vec3 F0 = mix(vec3(0.04), albedo, metallic);
vec3 kS = fresnelSchlick(F0, max(dot(v, h), 0.0));
vec3 kD = (1.0 - kS) * (1.0 - metallic); // 金属没有漫反射
```

> **为什么这样设计？** 因为真实世界里「金属」和「非金属」是两种完全不同的光学类型：金属会立即吸收折射光，所以没有漫反射；非金属会散射部分光，所以有颜色。用一个二值开关比用连续参数更符合物理直觉。

---

## 问题 5：能量守恒到底怎么保证？

Blinn-Phong 最大的隐患是：你可以同时把漫反射和高光都调得很强，导致总反射光超过入射光。PBR 通过两个机制避免这个问题。

### 机制 1：漫反射与镜面反射互斥

入射光的能量被分成两部分：

$$
k_d + k_s = 1
$$

镜面反射越强（菲涅尔项 $F$ 越大），漫反射就越弱：

```glsl
vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
```

### 机制 2：BRDF 积分不超过 1

Cook-Torrance 的镜面项经过 D/F/G 的组合，保证了在所有观察方向和入射方向下，反射积分不会超过入射能量。具体推导涉及半球积分，但工程上你只要记住：

- 用 GGX + Schlick + Smith 这套组合，是现代实时 PBR 的「标准答案」。
- 不要自己乱改分母或系数，否则容易破坏能量守恒。

> **诚实边界**：严格能量守恒只对单一光源、无自发光、不透明表面成立。多光源累加、环境光（IBL）、透明材质都需要额外处理。

---

## 问题 6：怎么把 PBR 写进 GLSL？

下面是一个**简化版单光源 PBR 片段着色器**。它不包含 IBL、阴影、HDR，但能清晰展示核心流程。

### 顶点着色器

```glsl
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
```

### 片段着色器

```glsl
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

layout(binding = 0) uniform sampler2D uAlbedoMap;
layout(binding = 1) uniform sampler2D uNormalMap;
layout(binding = 2) uniform sampler2D uMetallicRoughnessMap; // R=metallic, G=roughness

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;

out vec4 FragColor;

const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // 直接光照用 (r+1)^2 / 8
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main() {
    vec3 albedo = pow(texture(uAlbedoMap, vTexCoord).rgb, vec3(2.2)); // sRGB -> Linear
    vec3 tangentNormal = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
    // 简化：这里假设 vNormal 已经是世界空间法线，未使用 TBN
    vec3 N = normalize(vNormal);
    float metallic  = texture(uMetallicRoughnessMap, vTexCoord).r;
    float roughness = texture(uMetallicRoughnessMap, vTexCoord).g;

    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 L = normalize(uLightPos - vWorldPos);
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance 镜面项
    float D = DistributionGGX(NdotH, roughness);
    vec3  F = FresnelSchlick(F0, VdotH);
    float G = GeometrySmith(NdotV, NdotL, roughness);

    vec3 numerator = D * F * G;
    float denominator = 4.0 * NdotV * NdotL + 0.0001; // 防止除零
    vec3 specular = numerator / denominator;

    // 漫反射项
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // 单光源贡献（含 NdotL 余弦衰减）
    vec3 radiance = uLightColor;
    vec3 Lo = (diffuse + specular) * radiance * NdotL;

    // 环境光项（简化近似，IBL 会替换它）
    vec3 ambient = vec3(0.03) * albedo;
    vec3 color = ambient + Lo;

    // Linear -> sRGB gamma 校正
    color = color / (color + vec3(1.0)); // 简易 tone mapping
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
```

> **注意**：上面的 `tangentNormal` 读取后没有使用 TBN 变换，是为了让代码聚焦在 PBR 本身。真实场景请复用 [[Notes/计算机图形学/纹理系统/多重纹理与材质|多重纹理与材质]] 里的 TBN 矩阵。

---

## 问题 7：代码里这些细节为什么要这样写？

### 7.1 为什么 Albedo 要从 sRGB 转到 Linear？

纹理图片通常存储在 sRGB 颜色空间（为了在人眼看来更均匀）。但光照计算必须在线性空间进行，否则乘法叠加会出错。

```glsl
vec3 albedo = pow(texture(uAlbedoMap, vTexCoord).rgb, vec3(2.2));
```

输出前再转回 sRGB：

```glsl
color = pow(color, vec3(1.0 / 2.2));
```

> 如果你用 `GL_SRGB8` 作为纹理内部格式，OpenGL 会自动在采样时做 sRGB→Linear，代码里就不需要 `pow(2.2)`。

### 7.2 为什么金属没有漫反射？

```glsl
vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
```

当 `metallic = 1.0` 时，`kD = 0`，所有入射光要么被镜面反射，要么被金属吸收。Albedo 此时描述的是金属的镜面反射颜色。

### 7.3 为什么分母要加 0.0001？

```glsl
float denominator = 4.0 * NdotV * NdotL + 0.0001;
```

当表面背对光源或相机时，`NdotV` 或 `NdotL` 可能为 0，导致除零产生 NaN 或巨大值。加一个很小的数避免崩溃。

### 7.4 为什么粗糙度要平方？

```glsl
float a = roughness * roughness;
```

迪士尼发现，直接拿 roughness 代入 GGX 会让低粗糙度区域变化太剧烈，美术不好控制。`roughness²` 让曲线更平滑，成为事实标准。

---

## 与 SelfGameEngine 的关系

### PBR = 引擎材质系统的「高级材质」

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被拆成 Template-Asset-Instance 三层。PBR 不会推翻这套结构，而是**填充 Template 里的光照模型**。

一个 PBR 材质的 Asset 层大概长这样：

```cpp
struct PbrMaterialAsset {
    ShaderTemplateHandle templateRef; // 指向 "StandardPBR" 模板

    TextureHandle albedoMap;
    TextureHandle normalMap;
    TextureHandle ormMap;        // Occlusion/Roughness/Metallic 通道打包
    TextureHandle emissiveMap;

    Vec4  baseColorTint;         // Albedo 颜色缩放
    float metallicScale;
    float roughnessScale;
    float normalStrength;
    float emissiveScale;
};
```

渲染时：

```cpp
void ApplyPbrMaterial(const PbrMaterialAsset& mat) {
    BindTexture(0, mat.albedoMap,   albedoSampler);
    BindTexture(1, mat.normalMap,   normalSampler);
    BindTexture(2, mat.ormMap,      ormSampler);
    BindTexture(3, mat.emissiveMap, emissiveSampler);

    glUniform4f(baseColorTintLoc, mat.baseColorTint);
    glUniform1f(metallicScaleLoc,  mat.metallicScale);
    glUniform1f(roughnessScaleLoc, mat.roughnessScale);
    // ...
}
```

### 与 Shader 变体的关系

在 [[Notes/SelfGameEngine/渲染管线与画面/着色器变体与编译缓存|着色器变体与编译缓存]] 里，一个 Template 可能产生多个变体。PBR 会新增几个维度：

- `HAS_NORMAL_MAP`：是否采样法线贴图
- `HAS_ORM_MAP`：是否使用 ORM 打包纹理
- `HAS_EMISSIVE`：是否有自发光
- `USE_IBL`：是否启用环境光照（下节内容）

这些关键字组合会进入 `permutation_id` 的生成逻辑。PBR 并不会让变体爆炸更严重——它只是让「哪些贴图参与计算」这件事更明确。

### 与参数绑定的关系

[[Notes/SelfGameEngine/渲染管线与画面/材质参数绑定与GPU上传|材质参数绑定与GPU上传]] 提到按更新频率分层。PBR 的参数也可以这样分：

| 更新频率 | 参数示例 | 绑定方式 |
|---------|---------|---------|
| 每帧 / 每视图 | View/Projection、相机位置、光源 | UBO binding 0 |
| 每材质 | baseColorTint、metallicScale、roughnessScale | UBO binding 1 或 Push Constant |
| 每物体 | Model 矩阵 | UBO binding 2 或 Push Constant |
| 每像素 | Albedo/Normal/ORM 纹理 | Texture binding 0~2 |

---

## API 对照：我们在解决「把 PBR 公式写进可编程管线」这个问题

| 概念 | OpenGL (GLSL) | Vulkan (SPIR-V) | D3D12 (HLSL) |
|------|---------------|-----------------|--------------|
| 材质参数 | `uniform` / UBO | Push Constant / UBO / DescriptorSet | `cbuffer` / Root Constants |
| 纹理槽位 | `layout(binding = N)` | `DescriptorSet` binding | Root Signature descriptor table |
| PBR 公式位置 | Fragment Shader | Fragment Shader | Pixel Shader |
| 多纹理上限 | 16~32 个纹理单元 | 受 Descriptor Pool 限制 | 受 Root Signature 限制 |

> **个人项目推荐**：学习阶段用 OpenGL + `layout(binding = N)` 完全够用。向现代 API 迁移时，mentally map 为：「Albedo/Normal/ORM 分别对应 DescriptorSet 里 binding 0/1/2 的 texture descriptor，材质参数对应 binding 0 的 uniform buffer」。

---

## IBL 简介：PBR 不能只靠点光源

上面的代码只有一个点光源和一个恒定的环境光项。但真实世界里，物体被整个环境照亮：天空、地面、墙壁都会反射光。

**IBL（Image-Based Lighting，基于图像的光照）** 就是用一个环境贴图（通常是 HDR 立方体贴图）来模拟这种间接光照。它分两部分：

1. **漫反射 IBL**：用预过滤的辐照度贴图（Irradiance Map）给表面提供柔和的环境色。
2. **镜面反射 IBL**：用预过滤的环境贴图 + BRDF LUT 模拟不同粗糙度下的反射。

IBL 让金属的镜面反射能「照出」周围环境，是 PBR 画面从「塑料感」跃升到「电影感」的关键。但它需要立方体贴图、HDR、卷积预过滤等前置知识，本篇只做概念预告，具体实现会在后续笔记深入。

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| 漫反射与镜面反射 | 用 `kD = (1 - F) * (1 - metallic)` 保证互斥 |
| 菲涅尔基础反射率 | 非金属 $F_0 = 0.04$，金属 $F_0 = \text{albedo}$ |
| 粗糙度输入 | 采样后根据需要进行 `roughness²` 再代入 GGX |
| 法线归一化 | 片段着色器里必须 `normalize(vNormal)` |
| 颜色空间 | Albedo/Emissive 用 sRGB，采样后转 Linear 计算 |
| 防止除零 | Cook-Torrance 分母加极小值 |
| 能量守恒 | 不要手动 boost specular，让 D/F/G 自己决定 |

---

## 常见陷阱

### 陷阱 1：把 Albedo 当 Diffuse 用

Blinn-Phong 的 Diffuse 纹理常常包含烘焙阴影；PBR 的 Albedo 必须是**纯表面颜色**，不带任何光照信息。

### 陷阱 2：Metallic 用中间值做主要效果

除了灰尘、氧化等过渡区域，Metallic 应该接近 0 或 1。中间值会让材质看起来「既不像金属也不像塑料」。

### 陷阱 3：Roughness 和 Smoothness 搞混

有些引擎（如 Unity 早期）用 Smoothness = 1 - Roughness。导入纹理时注意通道含义，否则粗糙度会完全反掉。

### 陷阱 4：漏掉 Gamma 校正

PBR 对颜色空间极其敏感。Albedo 没转 Linear、输出没转 sRGB，金属高光会发灰、塑料会过曝。

### 陷阱 5：在顶点着色器里算 PBR

和 Blinn-Phong 一样，PBR 的所有 BRDF 计算必须在片段着色器里逐像素进行。顶点着色器只负责位置和法线。

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| Blinn-Phong 的局限 | 多光源累加与光衰减 |
| 微表面模型与粗糙度直觉 | IBL 环境光照的具体实现 |
| Cook-Torrance D/F/G 三项 | 阴影映射（物体间遮挡） |
| Metallic-Roughness 工作流 | Deferred / Forward+ 管线的 PBR 适配 |
| 简化版 GLSL PBR Shader | 能量守恒的环境光积分 |

> **下一步**：[[Notes/计算机图形学/现代渲染技术/阴影映射|阴影映射]] — PBR 让表面正确响应直接光，但物体之间还缺少遮挡。下一步让光也「知道」被挡住，画面才谈得上真实。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
