---
title: Mipmap 与纹理过滤
description: 纹理能采样了，但远处闪烁、近处锯齿怎么办？理解 Mipmap 金字塔、纹理过滤模式、三线性过滤与各向异性过滤，掌握 OpenGL 中 glGenerateMipmap 与 glTexParameteri 的精确语义。
date: 2026-06-25
tags:
  - graphics
  - texture
  - mipmap
  - filtering
  - opengl
aliases:
  - Mipmap and Texture Filtering
  - Texture Filtering
  - 纹理过滤
---

> **前置依赖**：[[Notes/计算机图形学/纹理系统/纹理对象与加载|纹理对象与加载]] — 你已经会把 PNG 上传到 GPU 纹理对象；[[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理映射与UV坐标]] — 你已经理解 UV 插值和走样概念
> **本模块增量**：学完这篇笔记后，你能解释远处纹理闪烁的成因，能用 `glGenerateMipmap` 生成 Mipmap，能根据场景需求选择合理的过滤模式，并知道各向异性过滤该在什么时候打开。
> **下一步**：[[Notes/计算机图形学/纹理系统/纹理寻址与采样|纹理寻址与采样]] — UV 超出 `[0,1]` 怎么办？Wrap/Clamp/Mirror 到底影响什么？

---

# Mipmap 与纹理过滤

## 问题 0：纹理能采样了，但画面不对劲

在 [[Notes/计算机图形学/纹理系统/纹理对象与加载|纹理对象与加载]] 里，我们终于让 Shader 里的 `sampler2D` 读到了 PNG 图片。但如果你把那个带纹理的立方体推远，或者让地面纹理无限延伸，很快会观察到两种现象：

- **远处纹理疯狂闪烁**（shimmering）：稍微动一下相机，远处的像素颜色就乱跳
- **近处纹理出现锯齿/摩尔纹**（moiré）：密集的条纹或格子变成奇怪的波纹

这两件事听起来像不同的问题，但根源是同一个：**一个屏幕像素对应的纹理区域，和纹理像素（texel）的大小关系不对**。

---

## 问题 1：一个屏幕像素到底对应多少 texel？

想象你有一张贴满细密格子图案的地面纹理，分辨率是 `1024×1024`。纹理上的每个最小颜色格子叫做一个 **texel（纹理像素，texture element 的缩写）**。当相机靠近地面时，一个屏幕像素可能只对应纹理上的 **不到一个 texel**；而当相机拉远或看向地平线时，一个屏幕像素可能对应纹理上的 **几十个甚至上百个 texel**。

这就像你拿着一张高分辨率照片：凑近看时，眼睛（像素）能分辨照片上的每个细节；拿远看时，眼睛实际看到的是一大片区域的**平均颜色**，而不是某个单独像素的颜色。但 GPU 如果只采样一个点，就相当于远看照片时还盯着某一个像素看——稍微一晃，看到的像素就变了。

```
靠近地面：                    远离地面：
屏幕像素                      屏幕像素
┌───┐                         ┌───────────────┐
│ ● │  →  纹理中不到1个texel  │       ●       │  →  纹理中覆盖16×16个texel
└───┘                         └───────────────┘
```

在 [[Notes/计算机图形学/软光栅化与3D数学/3D光栅化与深度缓冲|3D光栅化与深度缓冲]] 里我们学过：**走样（Aliasing）的本质是采样率低于信号变化频率**。纹理采样也不例外——

- 如果一个像素覆盖了 16×16 个 texel，你却只采样其中 **1 个点**，这个点的颜色完全不能代表这 256 个 texel 的平均颜色。相机稍微一动，采样点落到另一个 texel 上，颜色就剧烈跳变 → **闪烁**。
- 如果纹理有规律的细密图案（如栅栏、条纹），采样点位置的微小变化会和图案频率产生干涉 → **摩尔纹**。

所以核心问题变成：**怎么根据像素覆盖的纹理区域大小，合理地取平均颜色？**

---

## 问题 2：最 naive 的方案——只用 GL_NEAREST 或 GL_LINEAR

OpenGL 默认的纹理过滤参数是 `GL_NEAREST`。我们来看看只用它会发生什么。

### 方案 A：GL_NEAREST（最近点采样）

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
```

GPU 只取距离采样点最近的那个 texel 颜色。

**近距离效果**：还凑合，但边缘会有明显锯齿。

**远距离效果**：灾难。一个像素覆盖 16×16 texel，却只取 1 个点的颜色，稍微一动就跳到另一个 texel → **疯狂闪烁**。

### 方案 B：GL_LINEAR（双线性插值）

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

GPU 取采样点周围 2×2 共 4 个 texel，按距离加权平均。

**近距离效果**：比 NEAREST 平滑很多。

**远距离效果**：仍然不够好。一个像素覆盖 16×16 texel，只平均 4 个点，和只采 1 个点没有本质区别。该闪烁还是闪烁，只是稍微柔化了一点。

> **关键发现**：`GL_LINEAR` 只能解决"一个像素略大于 1 个 texel"的情况。当"一个像素远大于 1 个 texel"时，4 个样本远远不够。

---

## 问题 3：那能不能实时对大片 texel 做平均？

理论上可以。对于每个像素，根据它在纹理空间中的 footprint（覆盖区域），把覆盖到的所有 texel 加权平均。这就是**理想滤波**。

**问题**：
- 实时计算积分或大量采样太贵
- footprint 的形状还随视角变化（斜着看地面时 footprint 是长条形）

所以我们需要一个**预计算**的方案：把不同尺度的平均结果提前算好，存在 GPU 里，运行时直接查表。

---

## 问题 4：Mipmap——预计算的纹理金字塔

**Mipmap 的核心思想**：为同一张纹理预存多级分辨率版本，形成一个金字塔。

```
Level 0: 512 × 512  （原始分辨率）
Level 1: 256 × 256  （每 2×2 平均成 1 个）
Level 2: 128 × 128
Level 3:  64 ×  64
Level 4:  32 ×  32
Level 5:  16 ×  16
Level 6:   8 ×   8
Level 7:   4 ×   4
Level 8:   2 ×   2
Level 9:   1 ×   1  （整张纹理的平均颜色）
```

每一级都是下一级的低通滤波+下采样版本。远处像素覆盖 texel 多时，直接采样低级 Mipmap；近处覆盖少时，采样高级 Mipmap。

**为什么这能消除闪烁？**

因为低级 Mipmap 的每个 texel 本身就是原始纹理一大片区域的平均。一个像素覆盖 16×16 原始 texel 时，去 Level 4（32×32）采样，一个像素只覆盖约 0.5 个 texel，再稍微动一点也不会跳出完全不同的颜色。

> Mipmap 这个名字来自拉丁语 *multum in parvo*，意思是"小地方装很多东西"。

---

## 问题 5：OpenGL 里怎么生成 Mipmap？

最简单的方式是让 GPU 自动生成：

```cpp
// 上传完 level 0 后
 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
              0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

// 自动生成全部 Mipmap 层级
 glGenerateMipmap(GL_TEXTURE_2D);
```

**状态变化图**：

```
glTexImage2D 后：
  纹理对象
  └── Level 0: 512×512  ✓
  └── Level 1~N: 未定义

glGenerateMipmap 后：
  纹理对象
  ├── Level 0: 512×512  ✓
  ├── Level 1: 256×256  ✓
  ├── Level 2: 128×128  ✓
  ├── ...
  └── Level N: 1×1      ✓
```

> **重要**：如果你把 `GL_TEXTURE_MIN_FILTER` 设为 `GL_NEAREST` 或 `GL_LINEAR`（不带 `_MIPMAP_`），OpenGL **不会使用 Mipmap**，就算生成了也会忽略。

---

## 问题 6：GPU 怎么决定采样哪一级 Mipmap？

GPU 会根据当前像素在纹理空间中的 footprint 大小，计算一个 **LOD（Level of Detail）** 值：

```
LOD = log2(像素在纹理空间中的 footprint 边长)
```

- footprint 小（近处）→ LOD 接近 0 → 用 Level 0
- footprint 大（远处）→ LOD 增大 → 用 Level 4、Level 5 ...

这个计算是硬件自动完成的，基于顶点 Shader 输出的 `vTexCoord` 在屏幕空间中的导数（`ddx`, `ddy`）。你不需要在 Shader 里写代码，但要知道：**Mipmap 选择依赖相邻像素在纹理空间中的变化率**。

这也意味着：
- 如果相邻像素 UV 变化剧烈（远处/斜视角），LOD 就高
- 如果 UV 几乎不变（正对近处），LOD 就低

---

## 问题 7：过滤模式怎么选？

OpenGL 把过滤分成两个独立参数：

| 参数 | 控制场景 | 可选值 |
|------|---------|--------|
| `GL_TEXTURE_MAG_FILTER` | 放大：一个像素 < 一个 texel | `GL_NEAREST`, `GL_LINEAR` |
| `GL_TEXTURE_MIN_FILTER` | 缩小：一个像素 > 一个 texel | `GL_NEAREST`, `GL_LINEAR`, `GL_NEAREST_MIPMAP_NEAREST`, `GL_LINEAR_MIPMAP_NEAREST`, `GL_NEAREST_MIPMAP_LINEAR`, `GL_LINEAR_MIPMAP_LINEAR` |

### MAG_FILTER 永远只有两种选择

放大的场景下，最高级 Mipmap（Level 0）已经足够精细，不需要切层级：

- `GL_NEAREST`：像素风/复古效果
- `GL_LINEAR`：默认推荐，边缘平滑

### MIN_FILTER 的六种模式

命名规则是 `[采样方式]_MIPMAP_[层级混合方式]`：

```
GL_LINEAR_MIPMAP_LINEAR
   │         │
   │         └── 在相邻两个 Mipmap 层级之间做线性插值
   └──────────── 在每个层级内部做双线性插值
```

| 模式 | 层级内 | 层级间 | 效果 | 性能 |
|------|--------|--------|------|------|
| `GL_NEAREST` | 最近点 | 无 | 像素风，有锯齿 | 最快 |
| `GL_LINEAR` | 双线性 | 无 | 远处会闪烁 | 快 |
| `GL_NEAREST_MIPMAP_NEAREST` | 最近点 | 选最近层级 | 远处不闪，但层级切换明显 | 较快 |
| `GL_LINEAR_MIPMAP_NEAREST` | 双线性 | 选最近层级 | 远处不闪，层级内平滑 | 中等 |
| `GL_NEAREST_MIPMAP_LINEAR` | 最近点 | 层级间线性插 | 少见，质量一般 | 中等 |
| `GL_LINEAR_MIPMAP_LINEAR` | 双线性 | 层级间线性插 | **三线性过滤**，最平滑 | 较慢 |

> **工业默认值**：`GL_LINEAR_MIPMAP_LINEAR` 是 3D 场景纹理最常用的选择，质量最好，性能代价在大多数场景可接受。

---

## 问题 8：层级切换时为什么会跳变？三线性过滤怎么解决？

如果选 `GL_LINEAR_MIPMAP_NEAREST`，GPU 会选一个最接近的 Mipmap 层级，然后在该层级内做双线性插值。

**问题**：当物体从远到近移动时，LOD 值会跨过整数边界（比如从 2.99 跳到 3.01），导致 GPU 突然切换到另一个 Mipmap 层级。这个切换可能在视觉上造成轻微跳变。

**三线性过滤（`GL_LINEAR_MIPMAP_LINEAR`）** 的解决方式是：

1. 对相邻两个层级分别做双线性采样（Level N 和 Level N+1）
2. 按 LOD 的小数部分对两个结果做线性插值

```
LOD = 3.3

结果 = 0.7 × (Level 3 双线性采样) + 0.3 × (Level 4 双线性采样)
```

这样层级切换是渐变的，几乎不可见。

> **代价**：三线性过滤每像素需要 8 个 texel 采样（每个层级 4 个，共 2 个层级），带宽翻倍。现代 GPU 通常能轻松承受，但移动端需要谨慎。

---

## 问题 9：为什么开了 Mipmap + 三线性，斜视角地面还是糊？

Mipmap 有一个隐含假设：**像素在纹理空间中的 footprint 是近似正方形的**。

但当你斜着看地面时，像素投影到纹理空间是一个**长条形**：

```
屏幕像素投影到纹理空间：

近处地面：        远处/斜视角地面：
┌───┐             ┌───────────────┐
│   │             │               │
│ ● │             │       ●       │   ← 长条形 footprint
│   │             │               │
└───┘             └───────────────┘
```

Mipmap 只能按 footprint 的**短边**选择 LOD（否则长边方向会走样），这意味着短边方向被正确滤波了，但长边方向实际上被过度模糊。

### 各向异性过滤（Anisotropic Filtering）

**核心思想**：不假设 footprint 是正方形，而是沿着 footprint 的长轴方向多采几个样本，再平均。

```
普通三线性：      各向异性过滤：
  ┌───┐           ┌───────────────┐
  │●●●│           │● ● ● ● ● ● ● ●│   ← 沿长轴采样多个点
  │●●●│           └───────────────┘
  └───┘
```

在 OpenGL 中启用：

```cpp
// 查询硬件支持的最大各向异性
float maxAniso = 1.0f;
glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);

// 设置该纹理使用最大各向异性（通常 4x 或 8x 就够了）
glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, maxAniso));
```

> **注意**：OpenGL 4.6 核心已纳入 `ARB_texture_filter_anisotropic`，旧设备可能需要检查扩展字符串 `GL_EXT_texture_filter_anisotropic`。

**效果**：
- 斜视角地面的纹理清晰度显著提升
- 地平线附近的细节保留更多
- 代价是额外的纹理采样和带宽

---

## 问题 10：Mipmap 有什么代价？

Mipmap 不是免费的，诚实地说出它的成本：

| 收益 | 代价 |
|------|------|
| 消除远处闪烁 | 显存增加约 33%（1 + 1/4 + 1/16 + ... ≈ 1.33） |
| 减少纹理采样数量 | 生成 Mipmap 有 upfront 成本 |
| 三线性过滤画面平滑 | 每像素 8 个 texel 采样，带宽翻倍 |
| 各向异性过滤更清晰 | 采样数随各向异性等级增加，性能下降明显 |

**常见误区**：

- ❌ "Mipmap 让纹理变模糊" → ✅ Mipmap 只在需要时选择低分辨率层级。近处仍然用 Level 0，不会变糊。
- ❌ "各向异性过滤比 Mipmap 更好，可以替代 Mipmap" → ✅ 各向异性过滤**依赖 Mipmap**，最佳实践是 `GL_LINEAR_MIPMAP_LINEAR` + 合适的 MaxAnisotropy。
- ❌ "2D UI 纹理也要开 Mipmap" → ✅ UI 通常不需要 Mipmap，因为 UI 元素总是以接近原始分辨率显示。开了反而可能变糊，还浪费显存。

---

## 问题 11：引擎里该怎么抽象这些参数？

在自研引擎中，你不会希望每个材质都直接写 `glTexParameteri`。更合理的做法是把采样参数抽象成**采样器描述（SamplerDesc）**：

```cpp
struct SamplerDesc {
    enum class Filter {
        Nearest,
        Linear,
        Bilinear,        // GL_LINEAR_MIPMAP_NEAREST
        Trilinear,       // GL_LINEAR_MIPMAP_LINEAR
        Anisotropic4x,
        Anisotropic8x,
        Anisotropic16x,
    };

    Filter minFilter = Filter::Trilinear;
    Filter magFilter = Filter::Linear;
    // Wrap 模式在下一篇笔记展开
};
```

现代图形 API 的对应关系：

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| 纹理对象 | `GLuint texture` | `VkImage` + `VkImageView` | `ID3D12Resource` + `SRV` |
| 采样参数 | 内嵌在纹理对象或 Sampler Object | `VkSampler` | `D3D12_SAMPLER_DESC` |
| Mipmap 生成 | `glGenerateMipmap` | `vkCmdBlitImage` / Compute | 手动 Compute Shader / Copy Queue |
| 各向异性 | `GL_TEXTURE_MAX_ANISOTROPY` | `maxAnisotropy` in `VkSamplerCreateInfo` | `MaxAnisotropy` in `D3D12_SAMPLER_DESC` |

**个人项目推荐**：
- 默认用 OpenGL + `glGenerateMipmap` + `GL_LINEAR_MIPMAP_LINEAR`
- 地形/地面等斜视角纹理开启 4x 或 8x 各向异性
- UI 和 2D 精灵关闭 Mipmap
- 向现代 API 迁移时，把采样器描述 mentally map 为 `VkSampler` / `D3D12_SAMPLER_DESC`

---

## 完整可运行示例

```cpp
// flags: -O0 -g -lGL -lGLEW -lglfw
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLuint loadTextureWithMipmap(const char* path, bool enableAniso = true) {
    stbi_set_flip_vertically_on_load(true);

    int width, height, channels;
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 0);
    if (!pixels) {
        std::cerr << "Failed to load: " << path << std::endl;
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // 设置寻址模式（下一篇详细展开）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // 关键：三线性过滤 + Mipmap
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 各向异性过滤（可选）
    if (enableAniso) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                        std::min(8.0f, maxAniso));
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLenum internalFormat = (channels == 4) ? GL_RGBA : GL_RGB;
    GLenum sourceFormat   = (channels == 4) ? GL_RGBA : GL_RGB;

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height,
                 0, sourceFormat, GL_UNSIGNED_BYTE, pixels);

    // 自动生成 Mipmap 金字塔
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(pixels);
    return texture;
}
```

---

## 调试技巧：可视化 Mipmap 层级

如果你怀疑 Mipmap 没生效，可以在 Shader 里用 `textureQueryLod` 查看当前像素选择的 LOD：

```glsl
#version 430 core
in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 FragColor;

void main() {
    vec2 lod = textureQueryLod(uTexture, vTexCoord);
    float level = lod.x; // 请求的 LOD

    // 用颜色可视化：红色 = 低层级（近处），蓝色 = 高层级（远处）
    FragColor = vec4(level / 8.0, 0.0, 1.0 - level / 8.0, 1.0);
}
```

> 注意：`textureQueryLod` 需要 OpenGL 4.0+。

---

## 与 SelfGameEngine 的关系

这篇笔记对应引擎 **阶段 5.2 资源管理** 和 **阶段 5.3 材质系统** 中的纹理资源配置。

在引擎中，纹理对象通常不会直接暴露 `glTexParameteri`，而是：

```
TextureAsset
├── GpuTexture (id, width, height, format, mipCount)
├── SamplerDesc
│   ├── minFilter: Trilinear
│   ├── magFilter: Linear
│   ├── maxAnisotropy: 8
│   └── wrapS/wrapT: Repeat  ← 下一篇展开
└── ImportSettings
    └── generateMipmaps: true
```

材质实例引用 `TextureAsset`，渲染时根据 `SamplerDesc` 配置采样状态。你现在写的 `glGenerateMipmap` + `glTexParameteri` 就是未来 `RenderDevice::CreateTexture` 和 `Material::ApplySamplerState` 的底层实现原型。

---

## 本模块还缺什么？

| 已理解 | 待实践 |
|--------|--------|
| Mipmap 金字塔原理 | 纹理寻址模式（Wrap/Clamp/Mirror） |
| `glGenerateMipmap` 生成 | 多纹理绑定与材质组织 |
| 六种过滤模式语义 | 压缩纹理格式（DXT/BC/ASTC） |
| 三线性过滤与各向异性过滤 | 运行时 Mipmap 调试 |

> **下一步**：[[Notes/计算机图形学/纹理系统/纹理寻址与采样|纹理寻址与采样]] — UV 坐标超出 `[0,1]` 时会发生什么？Repeat、Clamp、Mirror 分别适合什么场景？
