---
title: 神经纹理压缩 NTC
description: PBR 材质需要 Albedo/Normal/Metallic/Roughness/AO 等多张贴图，显存压力巨大。理解 NVIDIA NTC 如何用特征张量 + 轻量 MLP 压缩整组材质纹理，支持随机访问，以及在引擎材质系统中的位置。
date: 2026-06-28
tags:
  - graphics
  - AI
  - neural-texture-compression
  - NTC
  - texture-compression
  - PBR
  - material
  - feature-tensor
  - mlp
aliases:
  - 神经纹理压缩
  - Neural Texture Compression
  - NTC
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/纹理系统/纹理对象与加载|纹理对象与加载]] — 你已经理解 PNG → GPU 纹理对象的上传链路
> - [[Notes/计算机图形学/纹理系统/多重纹理与材质|多重纹理与材质]] — 你已经理解 PBR 材质需要多张纹理同时采样
> - [[Notes/计算机图形学/现代渲染技术/PBR基础|PBR基础]] — 你已经理解 Albedo/Normal/ORM 这些通道的物理含义
>
> **本模块增量**：学完这篇笔记后，你能解释 NTC 与传统 BC7/ASTC 压缩的核心差异，能判断「样本时解码」「加载时解码」「按需反馈解码」三种模式的适用场景，并知道在引擎材质系统中需要新增哪些抽象。
>
> **下一步**：[[Notes/计算机图形学/AI驱动的现代渲染技术/神经网络降噪|神经网络降噪]] — 纹理存储被神经压缩革新后，渲染算法本身也能用神经网络加速，下一步看后处理栈里的神经降噪 Pass。

---

# 神经纹理压缩 NTC

## 问题 0：PBR 材质的贴图怎么这么多？

在 [[Notes/计算机图形学/现代渲染技术/PBR基础|PBR基础]] 里，一个标准 PBR 材质至少需要：

- Albedo（基础颜色）：3 通道
- Normal（法线）：3 通道
- Metallic（金属度）：1 通道
- Roughness（粗糙度）：1 通道
- AO（环境光遮蔽）：1 通道

加起来就是 9 个通道。如果是 4K 纹理，每个通道 16MB（4K × 4K × 1 byte），整组纹理就是 **144MB**。一个场景几百套材质，显存轻轻松松突破几十 GB。

**最 naive 的方案**：把所有纹理都按原始 PNG/TGA 上传 GPU。结果就是显存爆炸、加载慢、磁盘体积大。

**改进方案 A**：用传统块压缩格式（BC7、ASTC）。这是现代游戏的标准做法：

- BC7 固定 4:1 压缩，4K RGBA 纹理从 64MB 压到 16MB。
- ASTC 4×4 也是约 4:1，但块大小可调，最高能到 36:1（质量损失也大）。

**但块压缩的代价**：

- 压缩率是固定的，不能根据纹理内容自适应。
- 高频细节（毛发、文字、细密网格）容易产生块边界伪影。
- 不同通道之间各自独立压缩，浪费了 Albedo 和 Normal 之间的相关性。

> **核心问题**：有没有一种压缩方式，能利用 PBR 各通道之间的相关性，用更小的体积重建出同等甚至更好的质量？

---

## 问题 1：NTC 的基本思路是什么？

**NTC（Neural Texture Compression，神经纹理压缩）** 是 NVIDIA 提出的一种纹理压缩方案。它的核心思想不是「把每个 4×4 块单独压缩」，而是：

> **把整组 PBR 纹理看作一个联合信号，用神经网络学习它的紧凑表示。**

具体来说，NTC 把原始纹理编码成两部分：

1. **特征张量（Feature Grid / Latent Tensor）**：一组低分辨率的特征图，类似神经网络学到的“纹理 DNA”。
2. **小型解码器 MLP**：一个轻量神经网络（通常 2 层，16~64 神经元），负责把特征向量还原成原始颜色值。

运行时，Shader 里采样特征张量，过一次 MLP，得到 Albedo/Normal/Metallic/Roughness/AO 等通道的值。

```
原始纹理组（Albedo + Normal + ORM）
           │
           ▼
    离线训练 / 编码
           │
           ▼
    特征张量（小体积） + 解码器 MLP 权重
           │
           ▼
    运行时 Shader 采样特征 → MLP 推理 → 重建各通道
```

> **为什么叫“神经”压缩？** 因为解码过程不是固定函数（像 BC7 那样查表插值），而是神经网络推理。

---

## 问题 2：NTC 为什么能比 BC7 压缩率更高？

### 原因 1：利用通道间相关性

传统 BC7 对 Albedo、Normal、Roughness 分别独立压缩。但真实材质里，Albedo 里的划痕位置和 Normal 里的凹凸位置、Roughness 里的磨损位置往往是**同一块区域**。NTC 把它们一起编码，让网络学会共享这些信息。

### 原因 2：利用多分辨率结构

NTC 的特征张量通常是一个**特征金字塔**（多分辨率网格）。低频结构用低分辨率特征表示，高频细节用高分辨率特征补充。这比在每个 4×4 块里硬塞端点+权重更灵活。

### 原因 3：MLP 是“参数化解码器”

BC7 的解码规则是硬件写死的（端点插值）。NTC 的 MLP 权重是**为每套纹理训练出来的**，可以针对这套纹理的内容优化解码策略。

### 压缩率对比

| 配置 | 压缩比 | 质量 | 适用场景 |
|------|--------|------|---------|
| BC7 | 固定 4:1 | 好 | 当前工业标准 |
| ASTC 4×4 | ~4:1 | 很好 | 移动端/跨平台 |
| ASTC 6×6 | ~6.7:1 | 较好 | 对质量要求不极致的场景 |
| NTC-Tiny | 12:1 ~ 20:1 | 可见损失 | 远景、低优先级材质 |
| NTC-Small | 8:1 ~ 14:1 | 与 BC7 相当 | 推荐默认配置 |
| NTC-Large | 4:1 ~ 8:1 | 接近无损 | 主角、特写材质 |

> **注意**：网上常有“NTC 比 BC7 省 85% 显存”的说法，对应的是特定测试场景和配置，不是无条件保证。实际压缩率取决于纹理内容、网络大小和质量预设。

---

## 问题 3：运行时怎么解码？三种模式

NTC 最大的工程问题不是“能不能压缩”，而是“解码放在哪一步”。目前有三种主流策略：

### 模式 A：Inference on Sample（样本时解码）

**做法**：Shader 里直接采样特征张量，跑 MLP 推理，得到最终颜色。

```glsl
vec4 features = sampleFeaturePyramid(uv); // 采样多级特征
vec3 albedo;
vec3 normal;
float metallic, roughness, ao;
decodeMaterialMLP(features, uv, albedo, normal, metallic, roughness, ao);
```

**优点**：
- 显存占用最低，只存特征张量 + MLP 权重。
- 无需 Virtual Texture 或流送系统配合。

**缺点**：
- 每个像素都要跑 MLP 推理，ALU 开销大。
- 需要 GPU Tensor Core 或矩阵运算指令支持，否则性能不可接受。
- 需要 Stochastic Texture Filtering（STF，随机纹理过滤，用抖动采样近似双线性的技术）等技巧解决 MLP 输出不可线性插值的问题。

**适用场景**：高端 PC、最新 NVIDIA GPU、显存极度紧张的项目。

### 模式 B：Inference on Load（加载时解码）

**做法**：资源加载时，在 GPU 上把 NTC 数据解压成普通像素，再转码成 BC7/BC5 等块压缩格式。之后渲染流程和普通纹理完全一样。

```cpp
void LoadMaterialNTC(const std::string& path) {
    NTCData data = ParseNTCFile(path);           // 读特征张量 + MLP
    GPUDecompressToRGBA(data);                   // GPU 上推理出原始纹理
    TranscodeToBC7(bcnTextures);                 // 转码成 BCn
    UploadToTexturePool(bcnTextures);            // 当普通纹理用
}
```

**优点**：
- 兼容所有 GPU，不需要 Tensor Core。
- 渲染时零额外开销。
- 磁盘体积和传输带宽显著降低。

**缺点**：
- 加载时有几毫秒延迟（一套 4K 材质通常几 ms）。
- 解压后的 BCn 仍然占显存，只是磁盘/传输阶段省了空间。

**适用场景**：大多数现代游戏主机、PC、需要兼容旧硬件的项目。这是目前最实用的落地方式。

### 模式 C：Inference on Feedback（按需反馈解码）

**做法**：结合 Sampler Feedback / Virtual Texturing，只解码当前视野需要的 tile。NTC 数据以 tile 为单位流送，解压后存到稀疏 tiled texture 里。

**优点**：
- 同时享受低磁盘体积和低显存占用。
- 不需要每帧都跑 MLP 推理。

**缺点**：
- 实现复杂，需要完整的 VT / Sampler Feedback 系统。
- 对异步加载、内存池管理要求高。

**适用场景**：开放世界、大型场景、显存预算紧张的高端项目。

> **个人项目推荐**：默认选 **模式 B（加载时解码）**。它把 NTC 的“存储优势”和现有渲染管线的“零运行时开销”结合起来，工程风险最低。模式 A 只在你确定目标硬件有 Tensor Core 且显存是头号瓶颈时才考虑。

---

## 问题 4：随机访问为什么重要？

纹理压缩方案要用于实时渲染，必须支持**随机访问（Random Access）**：Shader 可以随时对任意 UV 坐标采样，不能要求先解码整张纹理。

NTC 满足这个要求，因为：

- 特征张量是普通 2D/3D 纹理，GPU 硬件采样器可以直接 sample。
- MLP 解码是逐 texel 的局部运算，不依赖周围像素。
- 多级特征金字塔配合 uv 坐标，可以支持不同 mipmap 层级。

> 这和视频/图像神经压缩（如 H.266）不同。那些方案通常依赖前后帧或整块信息，不适合直接做纹理采样。NTC 是专门为“任意位置实时采样”设计的。

---

## 问题 5：NTC 在引擎材质系统里应该放在哪一层？

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被抽象为 Template-Asset-Instance 三层。NTC 不会推翻这个结构，而是**新增一种资源类型和一种 Shader 变体**。

### 需要新增什么

1. **资源层：NeuralTextureAsset**

```cpp
struct NeuralTextureAsset {
    // 特征张量数据（可能已经过 ASTC/BC 二次压缩）
    TextureHandle featureGridLowRes;
    TextureHandle featureGridHighRes;

    // 解码器 MLP 权重（通常很小，几 KB 到几十 KB）
    BufferHandle mlpWeights;

    // 每个通道的缩放/偏移，用于恢复原始范围
    Vec4 channelScales;
    Vec4 channelOffsets;

    // 元数据：原始分辨率、通道数、mipmap 数
    int originalWidth, originalHeight;
    int numChannels;
};
```

2. **材质实例层：材质模板新增关键字**

```cpp
enum class TextureEncodingMode {
    TraditionalBC,   // 传统 BC7/ASTC
    NeuralOnLoad,    // NTC，加载时解码成 BCn
    NeuralOnSample   // NTC，Shader 内实时解码
};

struct MaterialParameter {
    // 原来只是 TextureHandle
    // 现在需要区分编码方式
    TextureHandle texture;
    TextureEncodingMode encodingMode;
};
```

3. **Shader 变体层：传统采样 vs 神经解码**

```glsl
// 变体 A：传统采样
layout(binding = 0) uniform sampler2D uAlbedo;
vec3 albedo = texture(uAlbedo, uv).rgb;

// 变体 B：NTC 实时解码
layout(binding = 0) uniform sampler2D uFeatureLow;
layout(binding = 1) uniform sampler2D uFeatureHigh;
layout(binding = 2) uniform samplerBuffer uMLPWeights;
vec3 albedo = DecodeNTC(uv, CHANNEL_ALBEDO);
```

### 对渲染队列的影响

- **模式 B（加载时解码）**：渲染队列完全无感知。加载管线多一步 GPU 解压 + 转码。
- **模式 A（样本时解码）**：需要把 NTC 材质分到一个单独的渲染批次，因为 Shader 不同、纹理绑定也不同。

---

## 问题 6：NTC 会替代 BC7/ASTC 吗？

**不会完全替代，而是共存。**

| 维度 | BC7/ASTC | NTC |
|------|---------|-----|
| 解码方式 | GPU 固定函数 | 神经网络推理 / 加载时转码 |
| 压缩率 | 固定 | 可调，通常更高 |
| 质量 | 高频细节有块伪影 | 整体更平滑，依赖训练 |
| 硬件要求 | 几乎所有 GPU | 实时推理需要 Tensor Core；加载转码可通用 |
| 随机访问 | 原生支持 | 原生支持 |
| 可编辑性 | 可解压、可修改 | 修改需重新训练 |
| 适用内容 | 所有纹理 | 最适合 PBR 材质套装 |

**共存策略**：

- **PBR 材质套装（Albedo/Normal/ORM）**：优先用 NTC，压缩率高、通道间相关性强。
- **UI、Sprite、文字纹理**：继续用 BC7/ASTC，需要精确像素、可编辑。
- **Lightmap、Shadowmap、后处理缓冲**：根据带宽和精度需求选择传统格式。

> **关键认知**：NTC 的“神经解码”是额外工具，不是银弹。它最适合的场景是“多通道、高相关性、海量材质”。

---

## 最小可运行验证路径

如果你想在引擎里验证 NTC，不要一上来就改材质系统。建议按这个顺序：

1. **用 NVIDIA RTX NTC SDK 或官方工具训练一套 PBR 纹理**，拿到 `.ntc` 文件。
2. **实现模式 B（加载时解码）**：
   - 解析 `.ntc` 文件，上传特征张量和 MLP 权重到 GPU。
   - 写一个 Compute Shader，输入特征张量，输出 RGBA 像素。
   - 把输出转码成 BC7，创建普通纹理对象。
   - 用现有 PBR Shader 采样，验证画质。
3. **再考虑模式 A（样本时解码）**：
   - 把 `DecodeNTC` 写进材质 Shader。
   - 测试 Tensor Core 加速后的性能。
   - 处理 mipmap 和纹理过滤问题。

> **不要一开始就实现完整的材质系统切换**。先让“一套 NTC 纹理能渲染出来”跑通，再考虑批量迁移。

---

## 常见陷阱

### 陷阱 1：以为 NTC 能无条件省 85% 显存

营销数字来自特定场景和配置。实际收益取决于：纹理内容、网络大小、质量预设、解码模式。模式 B 解压后还是 BCn 占显存，只是磁盘/传输省了。

### 陷阱 2：忽略 MLP 推理的 ALU 开销

模式 A 下，每个材质采样点都要跑 MLP。如果场景里有 10 张贴图、每个像素采样多次，ALU 开销会显著增加。必须做性能分析确认瓶颈在显存而不是 ALU。

### 陷阱 3：把 NTC 用于不适合的内容

NTC 对“高频细节+精确像素”不友好。UI、文字、图标、法线贴图里的硬边信息，用传统 BC7/ASTC 更可控。

### 陷阱 4：忘记 mipmap

特征金字塔不等于 mipmap。NTC 需要为不同 mipmap 层级单独训练或推导，否则远处纹理会闪烁或模糊。NVIDIA 的实现会一起压缩多个 mipmap 层级。

### 陷阱 5：忽略训练成本

NTC 不是通用编码器，而是**为每套纹理训练出来的**。导入新材质时需要离线训练，这增加了资产管线复杂度。需要把训练步骤集成到资源导入流程。

---

## 关键资源

- **NVIDIA RTX NTC SDK**：https://github.com/NVIDIA-RTX/RTXNTC
- **Random-Access Neural Compression of Material Textures**（NVIDIA Research，原始论文）
- **GDC 2024: Real-Time Neural Textures for Material Compression**（Ubisoft 在《刺客信条：幻景》里的技术验证）
- **Practical Neural Texture Compression**（Vulkanised 2025，NVIDIA 讲落地经验）

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| NTC 基本原理（特征张量 + MLP） | 具体训练流程和损失函数设计 |
| 三种解码模式 | Stochastic Texture Filtering（STF）细节 |
| 引擎材质系统集成点 | 与 Virtual Texturing / Sampler Feedback 结合 |
| 与 BC7/ASTC 的共存策略 | 移动端适配（FNTC 等简化方案） |

> **下一步**：[[Notes/计算机图形学/AI驱动的现代渲染技术/神经网络降噪|神经网络降噪]] — 纹理存储被神经压缩革新后，渲染算法本身也能用神经网络加速，下一步看后处理栈里的神经降噪 Pass。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
