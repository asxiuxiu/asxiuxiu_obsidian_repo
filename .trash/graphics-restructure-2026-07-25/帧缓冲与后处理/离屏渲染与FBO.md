---
title: 离屏渲染与 FBO
description: 后处理需要先拿到渲染好的画面，但默认帧缓冲是只写的。理解 OpenGL 帧缓冲对象（FBO）、颜色附件/深度附件、Renderbuffer Object、完整性检查，以及引擎中 RenderTarget 抽象的最小原型。
date: 2026-06-25
tags:
  - graphics
  - fbo
  - framebuffer
  - offscreen-rendering
  - render-target
  - post-processing
  - opengl
aliases:
  - Offscreen Rendering and FBO
  - Framebuffer Object
  - 帧缓冲对象
  - Render Target
---

> **前置依赖**：[[Notes/计算机图形学/纹理系统/多重纹理与材质|多重纹理与材质]] — 你已经能同时绑定多张纹理，理解纹理单元和 Shader sampler 的映射
> **本模块增量**：学完这篇笔记后，你能创建 FBO 把场景渲染到纹理上，再切换回默认帧缓冲显示，理解颜色附件和深度附件的两种挂接方式，并能做完整性检查。
> **下一步**：[[Notes/计算机图形学/帧缓冲与后处理/后处理管线|后处理管线]] — FBO 里的颜色附件就是纹理，后处理 Shader 怎么读它？全屏四边形又是怎么回事？

---

# 离屏渲染与 FBO

## 问题 0：后处理为什么需要“先画到别的地方”？

在 [[Notes/计算机图形学/纹理系统/多重纹理与材质|多重纹理与材质]] 里，我们已经能让立方体带上 Diffuse + Normal 贴图。但画面仍然只是“场景直接画到屏幕上”。

如果想做后处理——比如把画面变成灰度、加模糊、做泛光（Bloom）——就需要**在片段着色器里读取已经渲染好的画面**。但这里有一个根本障碍：

> **默认帧缓冲（Default Framebuffer）是窗口系统管理的，Shader 不能直接采样它。**

你可以把它理解为：屏幕是一张“只写”的画布。GPU 可以把像素画上去，但 Shader 不能像读纹理一样读回这些像素。

所以后处理的第一步是：**不要把场景直接画到屏幕上，而是先画到一张“可读的纹理”上。** 这张纹理就是 FBO 的颜色附件。

---

## 问题 1：最 naive 的方案——从屏幕读像素回 CPU

既然 Shader 读不了默认帧缓冲，那能不能用 `glReadPixels` 把像素读到 CPU 内存，再上传成纹理？

```cpp
std::vector<unsigned char> pixels(width * height * 4);
glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

// 再把 pixels 上传到 GPU 纹理
GLuint texture;
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
             0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
```

**立刻发现的问题**：

- 每帧都要做 `GPU 显存 → CPU 内存 → GPU 显存` 的拷贝
- 一张 1920×1080 的 RGBA 画面就是 8MB，60fps 下每秒要搬运近 500MB
- 延迟高、带宽爆炸，完全不适合实时渲染

**结论**：后处理需要的“可读画面”必须一直留在 GPU 显存里，不能经过 CPU。

---

## 问题 2：能不能直接“渲染到纹理”？

可以。OpenGL 提供了 **FBO（Framebuffer Object，帧缓冲对象）**，它允许我们把渲染目标从“默认帧缓冲”切换成“一个由我们创建的、带纹理附件的离屏帧缓冲”。

FBO 本身**不存储像素**。它只是一个“连接器”，把各种可渲染图像（纹理或 Renderbuffer）挂接到一组附件点上：

```
FBO
├── GL_COLOR_ATTACHMENT0  ──→ 颜色纹理（可采样）
├── GL_COLOR_ATTACHMENT1  ──→ 另一张颜色纹理（可选）
├── GL_DEPTH_ATTACHMENT   ──→ 深度纹理 或 深度 Renderbuffer
└── GL_STENCIL_ATTACHMENT ──→ 模板纹理 或 模板 Renderbuffer
```

当 FBO 被绑定为当前渲染目标时，所有 `glDraw*`、`glClear` 的输出都会写到这些附件上，而不是屏幕上。

---

## 问题 3：创建一个最小 FBO 需要什么步骤？

一个最基础的 FBO 需要：

1. 创建一个 FBO 对象
2. 创建一张颜色纹理，挂接到 `GL_COLOR_ATTACHMENT0`
3. 创建深度附件（可以用 Renderbuffer 或深度纹理）
4. 检查 FBO 完整性
5. 渲染时绑定 FBO，渲染完切回默认帧缓冲

```cpp
// 1. 创建 FBO
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);

// 2. 创建颜色附件纹理
glGenTextures(1, &colorTexture);
glBindTexture(GL_TEXTURE_2D, colorTexture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

// 把纹理挂到 FBO 的颜色附件 0
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D, colorTexture, 0);

// 3. 创建深度附件（用 Renderbuffer，不需要后续采样）
glGenRenderbuffers(1, &depthRBO);
glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                          GL_RENDERBUFFER, depthRBO);

// 4. 完整性检查
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "FBO 不完整！" << std::endl;
}

// 5. 解绑，恢复默认帧缓冲
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

**状态变化图**：

```
创建前：
  默认帧缓冲 ← 当前渲染目标

创建并绑定 FBO 后：
  FBO ← 当前渲染目标
  ├── COLOR_ATTACHMENT0 → colorTexture
  └── DEPTH_STENCIL_ATTACHMENT → depthRBO

渲染时 glBindFramebuffer(GL_FRAMEBUFFER, fbo)：
  glDraw* 的输出 → colorTexture
  glClear 清除的是 colorTexture + depthRBO

渲染后 glBindFramebuffer(GL_FRAMEBUFFER, 0)：
  默认帧缓冲 ← 当前渲染目标
```

---

## 问题 4：为什么颜色附件用纹理，深度附件却常用 Renderbuffer？

FBO 的附件可以是两种东西：

| 类型 | 特点 | 典型用途 |
|------|------|----------|
| **纹理（Texture）** | 渲染完后可被 Shader 采样 | 颜色附件、阴影贴图深度附件 |
| **Renderbuffer** | 专为渲染优化，不能被 Shader 直接采样 | 深度/模板附件（不需要读取） |

**颜色附件用纹理**：因为后处理 Shader 需要读取它。

**深度附件常用 Renderbuffer**：因为普通后处理只需要深度测试能工作，不需要读取深度值。Renderbuffer 在内存布局和性能上通常比深度纹理更优化。

**什么时候深度附件也用纹理？**

- 阴影映射（Shadow Map）：光源视角渲染的深度图需要被场景 Shader 采样
- 软粒子、SSAO、屏幕空间反射：需要读取相机视角的深度

> 简单记忆：如果这个附件渲染完后还要被 Shader 读，就用纹理；如果只是临时存储、不再读取，就用 Renderbuffer。

---

## 问题 5：FBO 完整性检查到底在检查什么？

`glCheckFramebufferStatus` 是创建 FBO 时**必须**做的一步。它会检查当前 FBO 是否满足渲染条件。常见的不完整原因：

| 返回值 | 含义 | 常见原因 |
|--------|------|----------|
| `GL_FRAMEBUFFER_COMPLETE` | ✅ 完整，可以渲染 | — |
| `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT` | 某个附件有问题 | 纹理/RBO 未分配、ID 为 0、格式不支持 |
| `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT` | FBO 没有挂任何附件 | 忘了 `glFramebufferTexture2D` |
| `GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS` | 附件尺寸不匹配 | 颜色纹理 1024×768，深度 RBO 800×600 |
| `GL_FRAMEBUFFER_UNSUPPORTED` | 驱动不支持这种格式组合 | 某些内部格式组合不兼容 |

> **工业建议**：每次创建 FBO 后都检查完整性，并在出错时打印具体状态码。这是很多后处理黑屏/白屏问题的根源。

---

## 问题 6：渲染到 FBO 后，怎么把它显示到屏幕上？

FBO 渲染完成后，颜色附件 `colorTexture` 里就有了场景画面。接下来要做两件事：

1. 切回默认帧缓冲
2. 画一个覆盖全屏的四边形，把 `colorTexture` 当普通纹理贴上去

```cpp
// ===== 第一步：渲染场景到 FBO =====
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glViewport(0, 0, fboWidth, fboHeight);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// 画场景（立方体、模型等）
DrawScene();

// ===== 第二步：切回默认帧缓冲，画全屏四边形 =====
glBindFramebuffer(GL_FRAMEBUFFER, 0);
glViewport(0, 0, windowWidth, windowHeight);
glClear(GL_COLOR_BUFFER_BIT);

// 把 FBO 颜色纹理绑定到纹理单元 0
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, colorTexture);

// 使用一个简单的全屏着色器
glUseProgram(postProcessShader);
glUniform1i(glGetUniformLocation(postProcessShader, "uScreenTexture"), 0);

DrawFullscreenQuad();
```

**对应的极简后处理片段着色器**：

```glsl
#version 330 core
in vec2 vTexCoord;
uniform sampler2D uScreenTexture;
out vec4 FragColor;

void main() {
    vec3 color = texture(uScreenTexture, vTexCoord).rgb;
    // 例如：灰度化
    float gray = dot(color, vec3(0.299, 0.587, 0.114));
    FragColor = vec4(vec3(gray), 1.0);
}
```

**状态变化图**：

```
Pass 1（场景渲染）：
  当前目标 = FBO
  输出 → colorTexture

Pass 2（后处理）：
  当前目标 = 默认帧缓冲
  输入 → colorTexture（作为普通纹理采样）
  输出 → 屏幕
```

---

## 问题 7：FBO 和视口有什么关系？

很多人第一次用 FBO 时会遇到画面比例不对的问题，原因是**视口（Viewport）没有和 FBO 尺寸对齐**。

```cpp
// ❌ 错误：FBO 是 800×600，但视口还是窗口大小 1920×1080
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glViewport(0, 0, windowWidth, windowHeight);  // 画面被拉伸或只画了左上角
```

**正确做法**：每次绑定 FBO 或切换回默认帧缓冲时，都要同步设置视口。

```cpp
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glViewport(0, 0, fboWidth, fboHeight);
// ... 渲染 ...

glBindFramebuffer(GL_FRAMEBUFFER, 0);
glViewport(0, 0, windowWidth, windowHeight);
// ... 后处理 ...
```

> 引擎中通常把“绑定 FBO + 设置视口”封装成一个 `RenderTarget::Bind()` 操作，避免遗漏。

---

## 问题 8：FBO 的尺寸必须和窗口一样大吗？

不一定。FBO 的分辨率可以独立设置：

- 后处理通常和窗口同分辨率，避免缩放失真
- 阴影贴图可以是 1024×1024 或 2048×2048，和窗口无关
- 小尺寸 FBO 可用于降采样效果（如 Bloom 的亮度提取）
- 大尺寸 FBO 可用于超采样或离线渲染

**但要注意**：如果 FBO 分辨率不等于窗口分辨率，最后画全屏四边形时，纹理会被拉伸。需要在后处理 Shader 里正确处理 UV，或在采样时使用合适的过滤模式。

---

## 问题 9：引擎里该怎么抽象 FBO？

在自研引擎中，你不会希望到处写 `glGenFramebuffers` 和 `glFramebufferTexture2D`。最自然的抽象是 **RenderTarget（渲染目标）**：

```cpp
struct RenderTargetDesc {
    int width;
    int height;
    bool hasColor;       // 是否有颜色附件
    bool hasDepth;       // 是否有深度附件
    bool hasStencil;     // 是否有模板附件
    bool colorIsTexture; // 颜色附件是否可被 Shader 采样
    bool depthIsTexture; // 深度附件是否可被 Shader 采样
};

struct RenderTarget {
    GLuint fbo = 0;
    GLuint colorTexture = 0;    // 如果 colorIsTexture = true
    GLuint depthTexture = 0;    // 如果 depthIsTexture = true
    GLuint depthRBO = 0;        // 如果深度附件是 Renderbuffer
    GLuint stencilRBO = 0;
    int width = 0;
    int height = 0;

    void Bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void Unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};
```

**为什么需要这个抽象？**

- 后处理栈、阴影贴图、反射探针都会用到“渲染到纹理”
- 不同效果对附件的需求不同：有的只要颜色，有的只要深度，有的要 MRT
- 抽象后，上层代码只说“绑定这个 RenderTarget”，不用关心底层是 FBO、RBO 还是纹理

---

## 完整可运行示例：渲染到 FBO 再灰度化

```cpp
// flags: -O0 -g -lGL -lGLEW -lglfw
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

struct FBO {
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthRBO = 0;
    int width = 0, height = 0;

    bool Init(int w, int h) {
        width = w; height = h;

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // 颜色附件：纹理
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTexture, 0);

        // 深度/模板附件：Renderbuffer
        glGenRenderbuffers(1, &depthRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO init failed" << std::endl;
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void Bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void Unbind(int windowW, int windowH) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowW, windowH);
    }

    void Destroy() {
        if (colorTexture) glDeleteTextures(1, &colorTexture);
        if (depthRBO) glDeleteRenderbuffers(1, &depthRBO);
        if (fbo) glDeleteFramebuffers(1, &fbo);
    }
};

// 全屏四边形顶点
float quadVertices[] = {
    // 位置      // UV
    -1, -1,  0, 0,
     1, -1,  1, 0,
     1,  1,  1, 1,
    -1,  1,  0, 1
};
unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };

const char* vs = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* fs = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    FragColor = vec4(g, g, g, 1.0);
}
)";

int main() {
    // ... 初始化 GLFW、GLEW、窗口 ...

    FBO fbo;
    if (!fbo.Init(800, 600)) return -1;

    // ... 创建 Shader、VAO、场景 ...

    while (!glfwWindowShouldClose(window)) {
        // Pass 1：渲染到 FBO
        fbo.Bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        DrawScene();  // 你的场景绘制

        // Pass 2：切回屏幕，画全屏四边形
        fbo.Unbind(800, 600);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(postProcessProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo.colorTexture);
        glUniform1i(glGetUniformLocation(postProcessProgram, "uTex"), 0);
        DrawFullscreenQuad();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    fbo.Destroy();
    return 0;
}
```

---

## 常见问题诊断

| 现象 | 可能原因 | 解决方案 |
|------|---------|---------|
| 屏幕全黑 | FBO 不完整或没绑定 | 检查 `glCheckFramebufferStatus` |
| 画面比例拉伸 | 视口没和 FBO 尺寸对齐 | 绑定 FBO 后设置 `glViewport(0,0,fboW,fboH)` |
| 后处理没有效果 | 采样的是未初始化的纹理 | 确认先渲染到 FBO，再切回默认帧缓冲 |
| 深度测试失效 | FBO 没有深度附件 | 附加深度 RBO 或深度纹理 |
| 读取 FBO 深度纹理失败 | 深度附件是 Renderbuffer | Renderbuffer 不能被 Shader 采样，改用深度纹理 |
| 多个 FBO 切换时画面错乱 | 没正确解绑/重绑纹理 | 避免同一个纹理同时作为渲染目标和采样源 |

---

## 与 SelfGameEngine 的关系

这篇笔记对应引擎 **阶段 5.6 后处理与特效** 和 **阶段 5.7 RenderGraph 与多 Pass 资源管理** 的基础。

在引擎中，FBO 会被抽象成 `RenderTarget`：

```cpp
RenderTarget sceneColorRT = RenderDevice::CreateRenderTarget({
    .width = windowWidth,
    .height = windowHeight,
    .colorFormat = TextureFormat::RGBA8,
    .depthFormat = TextureFormat::Depth24Stencil8,
    .colorIsTexture = true,   // 后处理需要采样
    .depthIsTexture = false,  // 普通场景只需要深度测试
});
```

你现在写的：
- `glGenFramebuffers` → 未来 `RenderDevice::CreateRenderTarget()`
- `glFramebufferTexture2D` / `glFramebufferRenderbuffer` → 渲染设备内部的附件挂接逻辑
- `glBindFramebuffer + glViewport` → `RenderTarget::Bind()`
- `glCheckFramebufferStatus` → 资源创建时的合法性校验

理解了 FBO，你就理解了引擎里 **RenderGraph** 为什么能把 Shadow Pass、Scene Color Pass、PostProcess Pass 串联起来：每个 Pass 都输出到一个 RenderTarget，下一个 Pass 把这个 RenderTarget 当纹理读。

---

## API 对照：我们在解决“离屏渲染目标”这个问题

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| 渲染目标对象 | FBO | `VkFramebuffer` | `ID3D12Resource` + RTV/DSV |
| 颜色附件 | 纹理 / Renderbuffer | `VkImageView` + `VkFramebuffer` | `RenderTargetView` |
| 深度附件 | 纹理 / Renderbuffer | `VkImageView` + `VkFramebuffer` | `DepthStencilView` |
| 切换渲染目标 | `glBindFramebuffer` | `vkCmdBeginRenderPass` | `OMSetRenderTargets` |
| 完整性检查 | `glCheckFramebufferStatus` | `VkFramebuffer` 创建时校验 | PSO + RTV 格式匹配 |

**个人项目推荐**：
- 学习阶段：OpenGL FBO + 纹理颜色附件 + Renderbuffer 深度附件
- 引擎阶段：封装 `RenderTarget` / `RenderPass`，让上层只关心“输入/输出什么纹理”
- 向现代 API 迁移时：mentally map 为 `VkRenderPass + VkFramebuffer` 或 D3D12 的 RTV/DSV

---

## 本模块还缺什么？

| 已理解 | 待实践 |
|--------|--------|
| FBO 创建与附件挂接 | 全屏四边形与后处理 Shader |
| 颜色纹理附件 vs Renderbuffer 深度附件 | 多 Pass 后处理管线组织 |
| FBO 完整性检查 | 多渲染目标（MRT） |
| 渲染到纹理再显示 | 在引擎中实现 RenderTarget 抽象 |

> **下一步**：[[Notes/计算机图形学/帧缓冲与后处理/后处理管线|后处理管线]] — 一个 Pass 只能输出到一个目标，但模糊需要先水平再垂直。怎么组织多个 Pass？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
