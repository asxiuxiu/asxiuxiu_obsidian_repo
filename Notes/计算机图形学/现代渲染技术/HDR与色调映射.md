---
title: HDR与色调映射
description: 延迟渲染的 Lighting Pass 输出的是线性 HDR 辐射度，普通显示器只能显示 0~1。理解 HDR 帧缓冲、曝光、Reinhard 与 ACES 色调映射，以及 Gamma 校正的位置。
date: 2026-06-28
tags:
  - graphics
  - hdr
  - tone-mapping
  - exposure
  - reinhard
  - aces
  - gamma-correction
  - post-processing
  - glsl
  - opengl
aliases:
  - HDR与色调映射
  - HDR Rendering
  - Tone Mapping
  - 高动态范围渲染
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/现代渲染技术/延迟渲染|延迟渲染]] — 你已经让 Lighting Pass 输出到浮点纹理
> - [[Notes/计算机图形学/帧缓冲与后处理/后处理管线|后处理管线]] — 你已经理解全屏 Pass 和乒乓缓冲
> - [[Notes/计算机图形学/现代渲染技术/PBR基础|PBR基础]] — 你已经理解线性空间的光照计算
>
> **本模块增量**：学完这篇笔记后，你能把 Lighting Pass 输出的 HDR 辐射度正确映射到屏幕可显示的 LDR 范围，并理解曝光、Gamma 校正、后处理栈的集成方式。
>
> **下一步**：回到 [[Notes/SelfGameEngine/0_RoadMap|SelfGameEngine 构建手册]] 阶段 6（动画/物理/Gameplay），让画面里的东西动起来。阶段九的现代渲染技术主线至此完成。

---

# HDR与色调映射

## 问题 0：Lighting Pass 输出了一堆超过 1.0 的数，屏幕该怎么显示？

你在 [[Notes/计算机图形学/现代渲染技术/延迟渲染|延迟渲染]] 里已经搭好了 G-Buffer + Lighting Pass。Lighting Pass 用 PBR 公式算出每个像素接收到的光能量，结果可能是这样的：

```glsl
vec3 color = (diffuse + specular) * radiance * NdotL;
// color 可能是 (0.3, 0.3, 0.3) —— 正常
// 也可能是 (12.5, 10.2, 8.0) —— 被太阳直射的高光
// 还可能是 (0.01, 0.005, 0.002) —— 阴影角落
```

普通显示器每个通道只能显示 0~255，对应 Shader 里的 0.0~1.0。**最 naive 的做法**：直接把 HDR 值截断到 `[0, 1]`。

```glsl
vec3 ldr = clamp(hdrColor, 0.0, 1.0);
FragColor = vec4(ldr, 1.0);
```

**立刻发现的问题**：

- 太阳直射的地方和一盏普通灯看起来一样亮——都是 1.0，失去层次感。
- 暗部被直接截断到 0，阴影里一点细节都没有。
- 一旦 clamp，HDR 信息就永久丢失了，后续想做 Bloom（提取高光）也做不成。

**核心矛盾**：物理世界的亮度范围是 `[0, 几万]`，显示器只能显示 `[0, 1]`。我们需要一种方法把大范围的亮度压缩到屏幕范围，同时尽量保留亮部和暗部细节。

---

## 问题 1：HDR 到底是什么？为什么要用浮点帧缓冲？

**HDR（High Dynamic Range，高动态范围）** 不是说“画面更亮更艳”，而是说：**渲染器内部保留的亮度范围可以远超显示器的 0~1**。

在 LDR 渲染里，帧缓冲是 8-bit 整数，每个通道只有 256 个级别，上限就是 1.0。任何超过 1.0 的亮度都会被截断。

在 HDR 渲染里，帧缓冲用浮点数（通常是 `R16G16B16A16_FLOAT` 或 `R11G11B10_FLOAT`），可以存 0.001 到 10000+ 的亮度值。这样：

- 一盏普通灯的 radiance 可以是 1.0
- 太阳光的 radiance 可以是 100.0
- 蜡烛的 radiance 可以是 0.5
- 它们之间的真实比例被保留下来

> **为什么叫“高动态范围”？** “动态范围”指最亮和最暗的比值。HDR 让这个比值可以达到几万；LDR 只能到 255:1。

### 修改 Deferred 管线：Lighting Pass 输出到浮点纹理

在 [[Notes/计算机图形学/现代渲染技术/延迟渲染|延迟渲染]] 里，Lighting Pass 输出到默认帧缓冲。现在要改成输出到一张 HDR 浮点纹理：

```cpp
// 创建 HDR 场景颜色纹理
GLuint hdrSceneColor;
glGenTextures(1, &hdrSceneColor);
glBindTexture(GL_TEXTURE_2D, hdrSceneColor);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
             GL_RGBA, GL_FLOAT, nullptr);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

// 创建 FBO 并挂接 HDR 颜色
GLuint hdrFBO;
glGenFramebuffers(1, &hdrFBO);
glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D, hdrSceneColor, 0);
// 深度附件按需挂接
```

Lighting Pass 现在绑定这个 `hdrFBO`，而不是默认帧缓冲：

```cpp
glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
glDisable(GL_DEPTH_TEST);

// 绑定 G-Buffer 纹理...
glUseProgram(lightingProgram);
DrawFullscreenQuad(); // 输出 HDR 颜色
```

> **关键状态变化**：Lighting Pass 的输出目标从“屏幕”变成“浮点纹理”。这个纹理里的值可以超过 1.0。

---

## 问题 2：把 HDR 压回 LDR，为什么不能直接线性缩放？

假设场景最亮的像素是 100.0，最暗的是 0.01。如果直接除以最大值 100.0：

```glsl
vec3 ldr = hdrColor / 100.0;
```

**问题**：

- 大部分像素本来就在 0~1 范围内，除以 100 后整体变暗。
- 暗部细节虽然保留了，但画面看起来像戴着墨镜。
- 最亮的太阳和次亮的灯泡之间的层次被压缩得难以区分。

我们需要一种**非线性压缩**：对暗部尽量保持线性（不变形），对亮部进行压缩（把高值压到 1.0 附近）。这就是**色调映射（Tone Mapping）**。

> **色调映射的本质**：一条把 `[0, ∞)` 映射到 `[0, 1]` 的曲线。好的曲线会保留中间调的对比度，同时把极高亮度平滑压缩到白。

---

## 问题 3：Reinhard — 最简单的色调映射曲线

Reinhard 是最简单的全局色调映射算子：

```
LDR = HDR / (HDR + 1)
```

它的曲线长这样：

- 当 HDR = 0 时，LDR = 0
- 当 HDR = 1 时，LDR = 0.5
- 当 HDR = 10 时，LDR ≈ 0.91
- 当 HDR → ∞ 时，LDR → 1

```glsl
vec3 reinhard(vec3 hdr) {
    return hdr / (hdr + vec3(1.0));
}

void main() {
    vec3 hdrColor = texture(uHDRScene, vUV).rgb;
    vec3 ldrColor = reinhard(hdrColor);
    FragColor = vec4(ldrColor, 1.0);
}
```

**优点**：简单，不会过曝，暗部细节保留好。

**缺点**：

- 画面容易发灰，因为所有高亮都被慢慢压向 1.0，缺少“电影感”。
- 对 RGB 三个通道分别做 Reinhard 会导致色彩饱和度下降（更专业的做法是先转亮度 Y，只对 Y 做映射，再转回 RGB）。

> **个人项目推荐**：Reinhard 适合作为第一个能跑的 tone mapping，但建议尽快换成 ACES 或 Filmic。

---

## 问题 4：曝光参数 — 如何控制画面整体亮度？

Reinhard 自动把所有亮度压到 `[0, 1]`，但摄影师知道：**同一场景在不同曝光下看起来完全不同**。从暗房间走到阳光下，人眼会自动适应；相机可以通过快门、ISO、光圈控制曝光。

在渲染里，我们用**曝光（Exposure）** 来模拟这个过程。最简单的曝光 tone mapping：

```glsl
vec3 exposed = hdrColor * exposure;
vec3 ldrColor = vec3(1.0) - exp(-exposed); // 指数曝光 + tone mapping
```

或者更常见的写法：

```glsl
uniform float uExposure; // 默认 1.0

vec3 toneMapExposure(vec3 hdr) {
    vec3 exposed = hdr * uExposure;
    return vec3(1.0) - exp(-exposed); // 1 - e^(-x)
}
```

**效果**：

- `exposure = 0.5`：整体偏暗，亮部细节更多
- `exposure = 1.0`：默认
- `exposure = 2.0`：整体偏亮，暗部细节更多

> **为什么用 `1 - exp(-x)`？** 这是摄影里“胶片响应曲线”的简化模型。低亮度时近似线性，高亮度时渐进饱和到 1.0，过渡比 Reinhard 更自然。

---

## 问题 5：ACES / Filmic — 为什么 Reinhard 看起来“像游戏截图”？

Reinhard 的问题是高光压缩太温和，导致画面缺乏“白色很白”的感觉。ACES（Academy Color Encoding System）和 Filmic tone mapping 用更复杂的 S 型曲线，让暗部更暗、亮部更白，同时保留中间调对比度。

### ACES Fitted Curve（简化版）

这是游戏行业最常用的 tone mapping 曲线之一：

```glsl
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

**效果对比**：

| 算子 | 暗部 | 亮部 | 整体观感 |
|------|------|------|---------|
| Clamp | 截断丢失 | 截断丢失 | 平淡、生硬 |
| Reinhard | 保留好 | 缓慢压到白 | 灰、像截图 |
| ACES | 略压暗 | 明亮但不爆 | 电影感 |

> **个人项目推荐**：默认用 ACES Fitted Curve，因为它在性能和画质之间取得了最好平衡，被 Unreal、Unity、Godot 等主流引擎广泛采用。

---

## 问题 6：Gamma 校正应该放在哪一步？

这是初学者最容易搞错的地方。

**正确顺序**：

```
HDR 线性光照计算 → Tone Mapping（仍在线性空间） → Gamma 校正 → 屏幕
```

```glsl
void main() {
    vec3 hdrColor = texture(uHDRScene, vUV).rgb; // 线性空间，可能 > 1.0

    // 1. Tone Mapping：把 HDR 压到 [0, 1]
    vec3 ldrColor = ACESFilm(hdrColor * uExposure);

    // 2. Gamma 校正：线性空间 → sRGB 空间（显示器预期）
    ldrColor = pow(ldrColor, vec3(1.0 / 2.2));

    FragColor = vec4(ldrColor, 1.0);
}
```

**为什么不能先做 Gamma 再做 Tone Mapping？**

- Gamma 校正是一个非线性操作。如果先 Gamma，Tone Mapping 面对的就已经是 sRGB 空间的颜色，物理关系被破坏了。
- 光照计算必须在线性空间，Tone Mapping 也必须在线性空间，只有最后输出才转 sRGB。

**常见的错误顺序**：

```glsl
// ❌ 错误：先 Gamma 再 Tone Mapping
vec3 gamma = pow(hdrColor, vec3(1.0 / 2.2));
vec3 ldr = ACESFilm(gamma); // 输入已经不是线性辐射度
```

> **如果你用 `GL_SRGB8_ALPHA8` 作为默认帧缓冲格式**，OpenGL 会自动在写入时做 sRGB 转换，Shader 里就不需要手动 `pow(1.0/2.2)`。但学习阶段手动写更清楚。

---

## 问题 7：自动曝光 — 让画面自己适应亮度

手动调 `uExposure` 很麻烦。更好的做法是让引擎根据场景亮度自动算曝光，模拟人眼适应。

**最简单的方法**：

1. 对 HDR 场景颜色算亮度图：

```glsl
float luminance = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
```

2. 把所有像素的亮度平均，得到场景平均亮度 `avgLum`。
   - 可以用 Compute Shader 做 reduce。
   - 也可以生成亮度图的 mipmap，读 1×1 最高层近似平均。

3. 根据平均亮度算曝光：

```glsl
float exposure = 0.18 / avgLum; // 0.18 是摄影里的“中间灰”
```

4. 为了模拟人眼适应的延迟，用帧间插值：

```glsl
float adaptedLum = mix(lastLum, currentLum, 1.0 - pow(0.98, 30.0 * deltaTime));
```

> **为什么用 0.18？** 这是摄影曝光里的“中间灰”标准——把场景平均亮度映射到 LDR 的 0.18 附近，画面看起来最自然。

### 更专业的曝光：EV100

现代引擎（如 Unreal）用 **EV100（Exposure Value at ISO 100）** 表示曝光：

```glsl
float exposure = 1.0 / (1.2 * avgLum); // 简化版
```

EV100 的好处是：美术可以直接用真实摄影的参数（光圈、快门、ISO）控制曝光，不同项目之间的亮度标准一致。

> 本篇只要求理解自动曝光的概念。具体实现（histogram、eye adaptation）属于后处理进阶，可以在后续按需深入。

---

## 完整可运行示例：最小 HDR + Tone Mapping 管线

### C++ 端：创建 HDR 帧缓冲

```cpp
struct HDRRenderTarget {
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthRBO = 0;
    int w = 0, h = 0;

    bool Init(int width, int height) {
        w = width; h = height;

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // HDR 浮点颜色纹理
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);

        // 深度附件（Lighting Pass 不需要深度测试，但 deferred 的 GBuffer 需要）
        glGenRenderbuffers(1, &depthRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }
};
```

### Tone Mapping 片段着色器

```glsl
#version 330 core
in vec2 vUV;

uniform sampler2D uHDRScene;
uniform float uExposure;

out vec4 FragColor;

vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrColor = texture(uHDRScene, vUV).rgb;

    // 曝光 + Tone Mapping
    vec3 mapped = ACESFilm(hdrColor * uExposure);

    // Gamma 校正
    mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}
```

### 渲染循环骨架

```cpp
void RenderFrame() {
    // Pass 1：Deferred Lighting → HDR FBO
    glBindFramebuffer(GL_FRAMEBUFFER, hdrRT.fbo);
    glViewport(0, 0, hdrRT.w, hdrRT.h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(lightingProgram);
    // 绑定 G-Buffer 纹理...
    DrawFullscreenQuad();

    // Pass 2：Tone Mapping → 屏幕
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(toneMapProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrRT.colorTex);
    glUniform1i(glGetUniformLocation(toneMapProgram, "uHDRScene"), 0);
    glUniform1f(glGetUniformLocation(toneMapProgram, "uExposure"), 1.0f);

    DrawFullscreenQuad();
}
```

---

## 状态变化图：HDR 管线的数据流

```
Pass 1：Lighting Pass
  输入：G-Buffer（Position/Normal/Albedo/Material）
  输出：HDR Scene Color（RGBA16F，值可 > 1.0）
  状态：depth test OFF, blend OFF
        │
        ▼
Pass 2：Tone Mapping Pass
  输入：HDR Scene Color
  参数：uExposure
  处理：ACESFilm(hdr * exposure) → pow(1/2.2)
  输出：默认帧缓冲（LDR, sRGB）
  状态：depth test OFF, blend OFF
```

> **关键规则**：HDR 纹理在 Lighting Pass 和 Tone Mapping Pass 之间是“写一次、读一次”。如果还要做 Bloom，需要在 Tone Mapping 之前从 HDR 纹理提取高光。

---

## 与 SelfGameEngine 的关系

### HDR/Tone Mapping = 后处理栈的“最后一个 Pass”

在 [[Notes/SelfGameEngine/渲染管线与画面/后处理栈架构|后处理栈架构]] 里，后处理栈组织了一系列全屏 Pass。Tone Mapping 通常是倒数第二个 Pass（最后一个是 FXAA/TAA 等抗锯齿）。

```cpp
postProcessStack.AddPass("Bloom", bloomShader);       // 需要 HDR 输入
postProcessStack.AddPass("ToneMapping", toneMapShader); // 把 HDR 转成 LDR
postProcessStack.AddPass("FXAA", fxaaShader);          // 在 LDR 上抗锯齿
```

**顺序非常关键**：

- **Bloom 必须在 Tone Mapping 之前**：Bloom 需要 HDR 里超过阈值的高亮信息。
- **FXAA 必须在 Tone Mapping 之后**：FXAA 工作在 LDR 空间，Tone Mapping 的非线性压缩会改变边缘强度。

### SceneColor 必须是 HDR 格式

引擎里的 `SceneTextures.sceneColor` 必须是浮点格式（`RGBA16F` 或 `R11G11B10F`），直到 Tone Mapping 完成。这比 LDR `RGBA8` 多占用一倍显存，但是现代渲染的必需品。

### 曝光参数的配置位置

在引擎的材质/后处理系统里，曝光通常放在 **PostProcessVolume** 或 **CameraComponent** 里：

```cpp
struct PostProcessSettings {
    float exposureCompensation = 0.0f; // EV 偏移
    float minExposure = -10.0f;        // 自动曝光下限
    float maxExposure = 10.0f;         // 自动曝光上限
    ToneMappingAlgorithm toneMapper = ToneMappingAlgorithm::ACES;
};
```

---

## API 对照：我们在解决「把浮点 HDR 值显示到 LDR 屏幕」这个问题

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| HDR 颜色纹理 | `GL_RGBA16F` / `GL_R11F_G11F_B10F` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `DXGI_FORMAT_R16G16B16A16_FLOAT` |
| 浮点 FBO | 普通 FBO + 浮点纹理附件 | `VkFramebuffer` + 浮点 `VkImage` | `D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET` |
| Tone Mapping | 全屏 PS / CS | 全屏 PS / CS | 全屏 PS / CS |
| Gamma 校正 | Shader 内 `pow(1/2.2)` 或 sRGB RT | `VK_FORMAT_B8G8R8A8_SRGB` 或 Shader | `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB` 或 Shader |
| 自动曝光 | Mipmap reduce / Compute reduce | Compute shader histogram/reduce | Compute shader |

> **个人项目推荐**：OpenGL 学习阶段用 `GL_RGBA16F` + Shader 内 `pow(1.0/2.2)`。向现代 API 迁移时，mentally map 为：「HDR SceneColor ≈ 浮点 RenderTarget，Tone Mapping ≈ 一个 Compute/PS Pass，Gamma ≈ sRGB 格式或 Shader 最后一步」。

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| HDR 纹理格式 | 至少 `RGBA16F`，不要用 8-bit LDR |
| 光照计算空间 | 全程线性空间，Tone Mapping 也在线性空间 |
| Tone Mapping 顺序 | 在 Bloom 之后、Gamma 之前 |
| Gamma 校正 | 最后一步，或交给 sRGB 帧缓冲 |
| 曝光参数 | 手动曝光或自动曝光， Clamp 到合理范围 |
| 颜色空间 | Albedo 纹理采样后转 Linear，输出前转 sRGB |
| 抗锯齿顺序 | FXAA/TAA 在 Tone Mapping 之后 |

---

## 常见陷阱

### 陷阱 1：在 Lighting Pass 里直接 clamp

```glsl
// ❌ 错误：Lighting Pass 里就 clamp
vec3 color = ambient + Lo;
color = clamp(color, 0.0, 1.0); // HDR 信息还没进入后处理就丢了
```

Lighting Pass 的输出必须是未截断的 HDR 值。

### 陷阱 2：Tone Mapping 前做了 Gamma

```glsl
// ❌ 错误
vec3 gamma = pow(hdrColor, vec3(1.0 / 2.2));
vec3 ldr = ACESFilm(gamma);
```

Gamma 必须放在 Tone Mapping 之后。

### 陷阱 3：对 RGB 分别做 Reinhard

```glsl
// ❌ 错误：容易导致色彩漂移
return hdr / (hdr + 1.0); // 三个通道独立
```

更正确的方式是转到亮度-色度空间，只对亮度做映射。

### 陷阱 4：曝光值没有范围限制

自动曝光如果碰到全白或全黑的场景，会算出极端曝光值。需要 Clamp：

```glsl
float exposure = clamp(0.18 / avgLum, minExposure, maxExposure);
```

### 陷阱 5：Bloom 放在 Tone Mapping 之后

```
// ❌ 错误顺序
Tone Mapping → Bloom → FXAA
```

Bloom 需要 HDR 输入。正确顺序：Bloom → Tone Mapping → FXAA。

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| HDR 浮点帧缓冲 | Bloom（泛光） |
| Reinhard / ACES Tone Mapping | Eye Adaptation / 自动曝光histogram |
| 曝光参数 | Color Grading / LUT |
| Gamma 校正位置 | TAA / FXAA 与 Tone Mapping 的顺序 |
| 后处理栈集成 | 跨帧历史缓冲管理 |

> **下一步**：回到 [[Notes/SelfGameEngine/0_RoadMap|SelfGameEngine 构建手册]] 阶段 6（动画/物理/Gameplay），让画面里的东西动起来。阶段九的现代渲染技术主线至此完成。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
