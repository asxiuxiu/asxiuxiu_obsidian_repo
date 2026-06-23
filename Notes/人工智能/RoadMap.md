---
title: AI × 引擎融合构建手册
date: 2026-06-03
tags:
  - ai
  - self-game-engine
  - roadmap
  - index
aliases:
  - AI引擎融合路线图
  - 双螺旋构建手册
---

> [[Notes/人工智能/索引|← 返回 人工智能索引]]

# AI × 引擎融合构建手册

> 本手册的核心理念是**双螺旋构建**：不是先学完 AI 再回头补引擎，也不是先造完引擎再嫁接 AI。而是让**引擎成为 AI 的实验台**，让**AI 成为引擎的智能层**——两条线互相缠绕、互相喂养，每完成一个阶段，屏幕上多出的东西和脑子里多出的直觉是同步发生的。
>
> **为什么你能深入这个领域？** AI 兴起这几年，所有人的起跑线确实在拉近。但你有别人没有的东西：**一个正在自研的游戏引擎**。这意味着你不需要等别人提供工具，你可以在自己的引擎里验证任何 AI 想法。这是顶尖 AI 研究者的基本素养——自己造实验环境。
>
> **构建哲学**：每个阶段必须同时回答两个问题：**引擎侧多了什么可见的东西？** **AI 侧多了什么可验证的直觉？**
>
> **支线深化**：每个阶段末尾的"支线深化"区块，收录了**不与引擎强制交汇**但对你的 AI 能力至关重要的内容。它们可以独立推进，也可以在主线卡壳时穿插进行。技多不压身。

---

## 快速导航

- [[#阶段 0：根基双线]]
- [[#阶段 1：第一帧与第一个模型]]
- [[#阶段 2：世界骨架与视觉感知]]
- [[#阶段 3：基础设施与注意力机制]]
- [[#阶段 4：运行时闭环与模型加载]]
- [[#阶段 5：画面与生成式AI]]
- [[#阶段 6：编辑器界面与智能体]]
- [[#阶段 7：角色行为与语言模型]]
- [[#阶段 8：AI桥接与推理引擎]]
- [[#阶段 9：发布部署与跨域整合]]
- [[#融合路线图总览]]
- [[#依赖关系与双螺旋衔接]]
- [[#附录 A：时间规划与里程碑]]
- [[#附录 B：学习资源清单]]
- [[#附录 C：面试准备与个人品牌]]

---

## 融合路线图总览

```
阶段 0：根基双线（数学 + Python + 引擎基础类型）
    │
    ├── 引擎线 ──► 阶段 1：Hello Window + 深度学习基础
    │                  │
    ├── AI线 ────►     ├── 引擎：窗口/ImGui/日志面板
    │                  └── AI：手写MLP → MNIST 95%+
    │                       │
    │                       ▼
    │                  阶段 2：最小ECS + 卷积视觉
    │                  │
    ├── 引擎线 ──►     ├── 引擎：ECS Inspector可视化
    │                  └── AI：PyTorch CNN → CIFAR-10 85%+
    │                       │
    │                       ▼
    │                  阶段 3：基础工具层 + Transformer
    │                  │
    ├── 引擎线 ──►     ├── 引擎：数学库/字符串/容器/VFS
    │                  └── AI：手写Attention + Transformer
    │                       │
    │                       ▼
    │                  阶段 4：核心运行时 + 预训练模型加载
    │                  │
    ├── 引擎线 ──►     ├── 引擎：反射/场景图/事件/Prefab
    │                  └── AI：加载BERT/GPT-2做推理
    │                       │
    │                       ▼
    │                  阶段 5：渲染管线 + 生成式AI
    │                  │
    ├── 引擎线 ──►     ├── 引擎：RHI/2D渲染/材质/后处理
    │                  └── AI：Stable Diffusion生成纹理
    │                       │
    │                       ▼
    │                  阶段 6：自研UI + Agentic AI
    │                  │
    ├── 引擎线 ──►     ├── 引擎：Retained UI/布局/事件/Docking
    │                  └── AI：LLM驱动编辑器命令
    │                       │
    │                       ▼
    │                  阶段 7：动画Gameplay + LLM NPC
    │                  │
    ├── 引擎线 ──►     ├── 引擎：动画/Motor/物理/导航
    │                  └── AI：LLM驱动NPC行为与对话
    │                       │
    │                       ▼
    │                  阶段 8：AI桥接 + 推理引擎
    │                  │
    ├── 引擎线 ──►     ├── 引擎：MCP/多Agent/Schema/脚本
    │                  └── AI：llama.cpp集成/推理优化
    │                       │
    │                       ▼
    │                  阶段 9：发布部署 + 跨域整合
    │                  │
    ├── 引擎线 ──►     ├── 引擎：CI-CD/Pak/性能分析
    │                  └── AI：AI+图形/AI+游戏/AI+机器人
```

**每个阶段的双验收标准**：
- **引擎侧**：屏幕上必须出现新的、可交互的、可见的功能
- **AI 侧**：必须产出可运行的代码、可测量的指标、或可验证的直觉
- **交汇点**：两条线在本阶段最好发生一次**数据交换**，但不做强制要求

---

## 阶段 0：根基双线

**引擎里程碑**：ECS 引擎的地基和 AI 实验的数学工具同时就位。所有后续阶段共享这一层。
**AI 里程碑**：能用 NumPy 手动推导任何公式的代码版本；理解为什么张量是引擎和 AI 的共同语言。

**交汇点**：引擎的数学库（Vec3/Mat4）与 AI 的张量运算共享同一套线性代数直觉。你在引擎里写矩阵变换时，脑子里同时能画出神经网络的权重矩阵。

**验收标准（双线可验证）**：
- [ ] **引擎**：搭建好 [[Notes/SelfGameEngine/基础工具层/引擎基础类型与平台抽象|引擎基础类型]]，能用自定义类型编译通过 Hello World
- [ ] **AI**：用 NumPy 手写矩阵乘法（不 `@`），理解广播；解释梯度为什么指向上升最快的方向
- [ ] **交汇**：对比 `Mat4 * Vec4` 和神经网络前向传播中的 `W @ x + b`——它们在数学上是同一件事，只是语义不同

### 0.1 引擎基础类型与平台抽象

- [ ] [[Notes/SelfGameEngine/基础工具层/引擎基础类型与平台抽象|引擎基础类型与平台抽象]] — 固定宽度整数、断言宏、平台检测

> **下一步**：引擎需要数学运算，AI 也需要——共享同一套根基。

### 0.2 数学基础（引擎+AI 共享）

- [ ] [[Notes/人工智能/根基层/线性代数与矩阵运算|线性代数与矩阵运算]] — 向量空间、SVD 低秩直觉、特征值几何意义
- [ ] [[Notes/人工智能/根基层/微积分与优化基础|微积分与优化基础]] — 梯度、链式法则、凸函数
- [ ] [[Notes/人工智能/根基层/概率与统计直觉|概率与统计直觉]] — 贝叶斯、MLE、KL 散度

> **引擎映射**：引擎的 `Mat4` 和 AI 的权重矩阵是同一套运算；物理模拟和优化算法都在解微分方程。
> **下一步**：把数学转化为代码——引擎写 C++ 数学库，AI 写 Python 张量操作。

### 0.3 工具链并行

- [ ] [[Notes/SelfGameEngine/基础工具层/数学基础|引擎数学基础]] — Vec3/Mat4/Quat、SIMD 抽象、确定性随机
- [ ] [[Notes/人工智能/根基层/Python科学计算工具链|Python科学计算工具链]] — NumPy 广播、matplotlib、PyTorch 张量

> **本阶段增量**：你拥有了两套"数学→代码"的翻译器（C++ 和 Python），并且知道什么时候该用哪一套。
> **下一步**：让引擎弹出窗口，让 AI 跑通第一个模型。

### 0.4 支线深化：Python 与 PyTorch 快速上手

> [!info] 支线说明
> 以下内容不直接与引擎交汇，但它们是后续所有 AI 实验的基础设施。利用你的 C++ 基础，重点理解动态类型、装饰器、上下文管理器与 C++ RAII 的对比。

- **Python 快速上手**：动态类型、列表/字典推导式、装饰器、`with` 上下文管理器、迭代器协议
- **PyTorch 核心概念**：
  - 张量操作：类比游戏引擎的 Vector/Matrix 类，理解 `tensor.view`、`permute`、`broadcast`
  - 自动微分（Autograd）：计算图概念、`backward()` 的底层是链式法则的自动化
  - DataLoader 与数据流水线：类比游戏资源加载系统，理解 `Dataset` + `DataLoader` + `collate_fn`
  - GPU 张量：`.to('cuda')` 的本质是内存迁移，理解 Host ↔ Device 的数据流

> **关键资源**：PyTorch 官方 60 分钟入门、《动手学深度学习》前 3 章

---

## 阶段 1：第一帧与第一个模型

**引擎里程碑**：弹出一个窗口，ImGui 日志面板实时滚动，FPS 计数器工作。这是你第一个"可视化调试工具"。
**AI 里程碑**：从零手写 MLP 的前向+反向传播，在 MNIST 上达到 95%+。你亲手摸到了"学习"的数学本质。

**交汇点**：在 ImGui 面板里实时绘制 MNIST 训练 loss 曲线和权重分布直方图。引擎的 2D 绘制能力第一次服务于 AI 实验。

**验收标准（双线可验证）**：
- [ ] **引擎**：窗口弹出，ImGui 日志三级过滤，FPS 显示，ESC 干净退出
- [ ] **AI**：不调用 `autograd`，手写 MLP + SGD，MNIST 95%+
- [ ] **交汇**：ImGui 面板里显示实时 loss 曲线、权重热力图、当前 batch 的预测结果

### 1.1 引擎：Hello Window

- [ ] [[Notes/SelfGameEngine/Hello-Engine-Window/窗口与输入系统|窗口与输入系统]] — 窗口创建、消息泵、DeltaTime
- [ ] [[Notes/SelfGameEngine/Hello-Engine-Window/最简图形后端|最简图形后端]] — OpenGL/bgfx 初始化、清屏
- [ ] [[Notes/SelfGameEngine/Hello-Engine-Window/ImGui接入与基础面板|ImGui接入与基础面板]] — 图形后端绑定、字体、Docking
- [ ] [[Notes/SelfGameEngine/Hello-Engine-Window/可视化日志系统|可视化日志系统]] — 多 Sink、级别过滤

> **下一步**：用 ImGui 画 AI 的训练面板。

### 1.2 AI：深度学习基础

- [ ] [[Notes/人工智能/深度学习基础/从线性回归到多层感知机|从线性回归到多层感知机]] — 单层→多层、激活函数、计算图
- [ ] [[Notes/人工智能/深度学习基础/反向传播与计算图|反向传播与计算图]] — 链式法则、手动实现 BP
- [ ] [[Notes/人工智能/深度学习基础/优化算法|优化算法]] — SGD/Momentum/Adam
- [ ] [[Notes/人工智能/深度学习基础/正则化与泛化|正则化与泛化]] — Dropout/L2/EarlyStopping

> **交汇实验**：在 ImGui 里添加一个"MNIST 训练器"面板——显示当前 epoch、loss、准确率，以及一个 28×28 的手写数字输入区（用 ImGui 的按钮网格模拟），实时预测输出。
> **本阶段增量**：引擎不再只是黑窗口，它成为了你的第一个 AI 实验可视化平台。
> **下一步**：让引擎拥有 ECS 世界，让 AI 拥有视觉。

### 1.3 支线深化：PyTorch 官方路径与更多项目

> [!info] 支线说明
> 手写 MLP 让你理解原理，但工业界用 PyTorch。两条路都要走。

- **PyTorch 版 MLP**：用 `nn.Module` 重写同样的网络，对比手写与框架版的速度和精度差异
- **DataLoader 深度**：自定义 `Dataset`、理解 `batch_size` 对显存和收敛的影响、`pin_memory` 和 `num_workers` 的优化意义
- **Autograd 深度**：用 `register_hook` 观察梯度、理解 `grad_fn` 计算图、尝试手写一个自定义 `Function`
- **更多实战项目**：
  1. **图像分类器**：用 PyTorch 官方 ResNet-18 分类游戏截图（角色/场景/道具）
  2. **风格迁移**：将游戏画面转换为水墨/赛博朋克风格（类比后期处理 Shader，理解 Gram 矩阵与内容/风格损失）
  3. **简单 GAN**：生成游戏纹理贴图（理解生成器/判别器的博弈）

> **关键资源**：Fast.ai Practical Deep Learning（工程导向）、《动手学深度学习》第 4-7 章

---

## 阶段 2：世界骨架与视觉感知

**引擎里程碑**：ECS 骨架搭好，Inspector 能实时查看实体和组件。3D 空间里出现会动的几何体。
**AI 里程碑**：用 PyTorch 搭建 CNN，在 CIFAR-10 上训练，理解"卷积"就是"局部感受野+参数共享"。

**交汇点**：把 CNN 学习到的特征图（feature map）作为纹理上传到引擎，在 ImGui 或 3D 立方体上可视化——**AI 的"眼睛"长什么样，你在引擎里直接看见**。

**验收标准（双线可验证）**：
- [ ] **引擎**：ECS 实体列表+Inspector 组件编辑、System Tick 驱动立方体旋转
- [ ] **AI**：ResNet-18 风格网络，CIFAR-10 85%+，可视化每层特征图
- [ ] **交汇**：引擎内显示 CNN 第一层的边缘检测卷积核（32 个小图像排成阵列）

### 2.1 引擎：最小 ECS 骨架

- [ ] [[Notes/SelfGameEngine/最小ECS骨架/最小ECS数据层|最小ECS数据层]] — Entity句柄、ComponentArray、World
- [ ] [[Notes/SelfGameEngine/最小ECS骨架/极简Inspector与ECS可视化|极简Inspector与ECS可视化]] — 实体列表、字段遍历、运行时增删

> **下一步**：基础工具层要替换临时实现，同时 CNN 需要更高效的底层。

### 2.2 AI：卷积与视觉

- [ ] [[Notes/人工智能/卷积与视觉/卷积神经网络原理|卷积神经网络原理]] — 卷积/池化/感受野
- [ ] [[Notes/人工智能/卷积与视觉/现代CNN架构演进|现代CNN架构演进]] — BatchNorm、ResNet、EfficientNet
- [ ] [[Notes/人工智能/卷积与视觉/视觉Transformer|视觉Transformer]] — ViT、Patch Embedding

> **交汇实验**：写一个"神经可视化"System——把 PyTorch 训练好的 CNN 权重导出为 PNG，作为纹理贴到 ECS 场景的立方体面上。旋转立方体时，你能"走进"神经网络的内部。
> **本阶段增量**：引擎的 3D 能力第一次用于理解 AI 内部结构。
> **下一步**：引擎升级基础工具层，AI 升级序列建模。

### 2.3 支线深化：计算机视觉高级主题

> [!info] 支线说明
> 分类只是 CV 的入门。目标检测和分割是游戏引擎更常遇到的任务（角色定位、场景解析、NPC 视野）。

- **目标检测基础**：理解 YOLO/Faster R-CNN 的 Anchor 机制、IoU、NMS。用预训练 YOLOv8 检测游戏截图中的角色和道具。
- **语义分割简介**：理解 FCN/U-Net 的编码器-解码器结构。用预训练模型分割游戏场景的前景/背景。
- **CNN 可视化技术**：
  - 特征图可视化（已交汇）
  - Grad-CAM：理解模型"在看哪里"
  - 卷积核可视化：第一层通常是边缘检测，深层是纹理/部件
- **数据增强**：理解 RandomCrop、ColorJitter、Mixup/CutMix 对泛化的提升

> **关键资源**：《动手学深度学习》第 6-7 章（CNN）、Ultralytics YOLOv8 文档

---

## 阶段 3：基础设施与注意力机制

**引擎里程碑**：数学库、字符串系统、容器/分配器、文件 IO、线程池全部就位，回探替换阶段 1/2 的临时实现。
**AI 里程碑**：手写缩放点积注意力，实现一个最小 Transformer，理解 Q/K/V 为什么这样设计。

**交汇点**：引擎的字符串系统（StringId/InternPool）处理大量实体标签，而 Transformer 处理 token 序列——两者都是"离散符号的高效编码问题"。你在优化引擎字符串哈希时，对 Embedding 层会有更深的共鸣。

**验收标准（双线可验证）**：
- [ ] **引擎**：自定义容器替换 `std::vector`，自定义字符串替换 `std::string`，零回归验证
- [ ] **AI**：手写 Attention + 2-layer Transformer，在小数据集上做文本分类
- [ ] **交汇**：用引擎的 HashMap 实现一个"极简 Tokenizer"——把字符串切分为 subword，映射为整数 ID（模拟 BPE 的最小版本）

### 3.1 引擎：基础工具层

- [ ] [[Notes/SelfGameEngine/基础工具层/字符串系统|字符串系统]] — StringId/StringView/String 契约
- [ ] [[Notes/SelfGameEngine/基础工具层/内存分配器|内存分配器]] — FrameArena、ObjectPool
- [ ] [[Notes/SelfGameEngine/基础工具层/容器系统|容器系统]] — Array、HashMap、SparseSet
- [ ] [[Notes/SelfGameEngine/基础工具层/文件IO与虚拟文件系统|文件IO与虚拟文件系统]] — VFS、Pak、热重载
- [ ] [[Notes/SelfGameEngine/基础工具层/线程池与任务系统|线程池与任务系统]] — ThreadPool、并行 For

> **下一步**：ECS 世界真正运转起来，同时 Transformer 需要被加载进引擎。

### 3.2 AI：序列与注意力

- [ ] [[Notes/人工智能/序列与语言/循环神经网络与门控机制|循环神经网络与门控机制]] — LSTM/GRU 门控直觉
- [ ] [[Notes/人工智能/序列与语言/注意力机制|注意力机制]] — Q/K/V、Self-Attention
- [ ] [[Notes/人工智能/序列与语言/Transformer架构|Transformer架构]] — 多头注意力、位置编码

> **交汇实验**：用引擎的字符串基础设施实现一个"实体描述生成器"——给 ECS 实体一组组件标签（Position/Mesh/Physics），用最小 Transformer 生成一句人类可读的描述（如"一个位于(3,2,1)的带物理属性的网格实体"）。
> **本阶段增量**：AI 开始理解引擎内部的数据结构。
> **下一步**：让引擎能加载外部模型，让 AI 能使用预训练权重。

### 3.3 支线深化：Transformer 深度拆解

> [!info] 支线说明
> Transformer 是现代 AI 的通用架构。以下每一块都值得独立深入，不急于与引擎交汇。

- **自注意力机制深度**：
  - 手写 Q/K/V 矩阵运算，理解 `Attention(Q,K,V) = softmax(QK^T/√d_k)V`
  - 理解 `√d_k` 缩放的数学必要性（防止点积过大导致 softmax 梯度消失）
  - 理解 Masked Self-Attention（因果掩码）在生成模型中的作用
- **位置编码（Positional Encoding）**：
  - 正弦/余弦位置编码的数学设计（不同频率的波长覆盖不同距离）
  - 可学习位置编码 vs. 固定位置编码的优劣
  - RoPE（旋转位置编码）和 ALiBi（线性偏差注意力）——长上下文的关键
- **多头注意力（Multi-Head Attention）**：
  - 为什么拆分多个头？每个头学习不同子空间的依赖关系
  - 手写多头注意力的拼接和投影
- **前馈网络与层归一化**：
  - FFN 的 `Linear→ReLU→Linear` 为什么是 `4d→d` 的瓶颈结构
  - LayerNorm vs. BatchNorm：序列数据为什么用 LayerNorm
  - Pre-Norm vs. Post-Norm 对训练稳定性的影响
- **Transformer 变体**：
  - Encoder-Only（BERT）：双向掩码，适合理解任务
  - Decoder-Only（GPT）：自回归生成，适合续写任务
  - Encoder-Decoder（T5/BART）：翻译/摘要的 seq2seq 结构

> **关键资源**：论文《Attention Is All You Need》（必读，逐段推导）、Andrej Karpathy 的 minGPT（从零手写 Transformer 的最佳参考）

---

## 阶段 4：运行时闭环与模型加载

**引擎里程碑**：ECS 查询/存储、反射、场景图、事件总线、Prefab 全部连通。窗口里有会动的彩色立方体，Inspector 实时修改立即生效，场景可保存加载。
**AI 里程碑**：加载预训练 BERT/GPT-2，理解预训练与微调的数据流差异。能在一个小数据集上做微调实验。

**交汇点**：引擎的反射系统能自动识别新组件字段，而 AI 的模型权重需要序列化格式。你在设计引擎 Prefab/场景序列化时，同时思考"模型 checkpoint 的加载与版本兼容"——两者是同一类工程问题。

**验收标准（双线可验证）**：
- [ ] **引擎**：3~5 个彩色立方体旋转、父子层级、Inspector 改 Transform 立即生效、场景保存/加载、Plugin 加载新 System
- [ ] **AI**：加载 HuggingFace 的 `bert-base-uncased` 做文本分类微调；加载 GPT-2 做文本续写
- [ ] **交汇**：把微调好的 BERT 模型权重导出为自定义二进制格式，用引擎的 VFS 加载，在引擎内做推理（C++ 侧或绑定 Python）

### 4.1 引擎：核心运行时闭环

- [ ] [[Notes/SelfGameEngine/核心运行时闭环/组件系统架构|组件系统架构]] — Sparse Set、Query、Archetype
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/场景图与变换|场景图与变换]] — Transform、父子层级、脏标记
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/反射系统|反射系统]] — 类型注册表、Inspector 自动适配
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/系统调度与确定性|系统调度与确定性]] — Tick 阶段、依赖图
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/事件总线|事件总线]] — Pub-Sub、延迟队列
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/Prefab与数据层|Prefab与数据层]] — 序列化、反序列化
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/Plugin与模块系统|Plugin与模块系统]] — 模块生命周期、热插拔

> **下一步**：引擎需要画出真正的画面，AI 需要生成内容。

### 4.2 AI：预训练与微调范式

- [ ] [[Notes/人工智能/序列与语言/预训练与微调范式|预训练与微调范式]] — GPT/BERT/T5 目标设计
- [ ] [[Notes/人工智能/生成式AI与大模型/大规模语言模型|大规模语言模型]] — Scaling Laws、涌现能力

> **交汇实验**：写一个"智能 Inspector"Plugin——选中 ECS 实体后，LLM 根据组件数据自动生成一段描述性文字（如"这是一个红色金属立方体，正在以每秒 30 度旋转"）。这要求引擎把组件数据序列化为文本 Prompt，调用 LLM 推理，再把结果渲染到 Inspector 面板。
> **本阶段增量**：AI 第一次成为引擎的"协作者"，而不仅是外部工具。
> **下一步**：引擎画出带纹理的立方体，AI 生成那些纹理。

### 4.3 支线深化：大模型工程化基础

> [!info] 支线说明
> 理解模型只是第一步，能把模型用起来才是工程能力。以下内容是大模型应用开发的核心技能。

- **预训练目标深度**：
  - GPT 系列：自回归语言建模（Next Token Prediction），理解 `P(x_i | x_{<i})`
  - BERT 系列：掩码语言模型（Masked LM）+ 下一句预测（NSP，虽然后续被证明不重要）
  - T5 系列：Text-to-Text Transfer，所有任务统一为文本生成
  - 指令微调（Instruction Tuning / SFT）：让模型学会"听从指令"而非仅仅"续写文本"
  - RLHF（人类反馈强化学习）：Reward Model + PPO，让模型输出符合人类偏好（了解概念即可，阶段 7 再深入）
- **高效微调技术**：
  - **LoRA / QLoRA**：低秩适配，冻结原权重，只训练低秩矩阵 `W = W_0 + BA`。理解为什么能降低显存占用（不存储优化器状态）。
  - **Prompt Engineering**：Zero-shot / Few-shot / Chain-of-Thought / ReAct 提示模板设计
  - **上下文学习（In-Context Learning）**：为什么大模型能"看几个例子就学会"？理解隐式梯度下降的类比
- **RAG 系统基础**：
  - 向量数据库：Milvus / Pinecone / Weaviate / Chroma 的基本使用
  - 嵌入模型（Embedding Models）：Sentence-BERT、OpenAI Embedding、理解 `text → dense vector` 的映射
  - 检索策略：Dense Retrieval（向量相似度）vs. Sparse Retrieval（BM25）vs. Hybrid Search
  - 重排序（Rerank）：用交叉编码器提升检索精度
- **实战项目**：
  1. **对话机器人微调**：用 LoRA 微调 Llama-2-7B 在特定游戏剧情数据上，实现角色扮演对话
  2. **代码生成助手**：微调 StarCoder / CodeLlama 在 C++ 游戏引擎 API 上，实现代码补全
  3. **RAG 系统**：构建游戏知识库问答（向量数据库 + LLM），支持查询游戏设定、角色背景、任务流程

> **关键资源**：Hugging Face Transformers 库、PEFT 库（LoRA 实现）、DeepLearning.AI 的《LangChain 实战》、论文《LoRA: Low-Rank Adaptation》

---

## 阶段 5：画面与生成式AI

**引擎里程碑**：RHI 抽象、GPU 资源管理、材质系统、2D 渲染、后处理全部就位。屏幕中央出现带纹理的旋转立方体，Bloom 光晕生效。
**AI 里程碑**：理解扩散模型原理，能用 Stable Diffusion 生成图像，理解噪声调度与去噪过程。

**交汇点**：**用扩散模型生成引擎所需的纹理和材质**。这是生成式 AI 最直接的游戏引擎应用——美术资源瓶颈的突破口。

**验收标准（双线可验证）**：
- [ ] **引擎**：带纹理立方体、2D UI 文字/按钮可点击、材质切换、Bloom 生效
- [ ] **AI**：运行 Stable Diffusion 生成一张 512×512 纹理，理解 UNet / VAE / CLIP 的分工
- [ ] **交汇**：把生成的纹理通过引擎的异步加载管线导入，实时贴到立方体上；后处理栈添加一个"AI 生成预览"Pass

### 5.1 引擎：渲染管线与画面

- [ ] [[Notes/SelfGameEngine/渲染管线与画面/RHI抽象层与命令模型|RHI抽象层与命令模型]]
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/GPU资源生命周期管理|GPU资源生命周期管理]]
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]]
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/2D渲染基础与批次合批|2D渲染基础与批次合批]]
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/后处理栈架构|后处理栈架构]]
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/异步加载管线|异步加载管线]]

> **下一步**：UI 框架替代 ImGui，AI 替代部分 UI 交互逻辑。

### 5.2 AI：扩散模型与概率生成

- [ ] [[Notes/人工智能/生成式AI与大模型/扩散模型与概率生成|扩散模型与概率生成]] — DDPM/DDIM、CFG、Latent Diffusion
- [ ] [[Notes/人工智能/生成式AI与大模型/多模态模型|多模态模型]] — CLIP、视觉-语言对齐

> **交汇实验**：在引擎编辑器里加一个"AI 纹理生成"面板——输入 prompt（如"赛博朋克金属地板"），调用本地 Stable Diffusion 生成纹理，自动导入为材质，并热重载到选中物体上。
> **本阶段增量**：AI 成为美术管线的一环。
> **下一步**：把 ImGui 替换为自研 UI，同时让 LLM 能操控编辑器。

### 5.3 支线深化：生成式 AI 全景

> [!info] 支线说明
> 扩散模型是当下的主流，但 GAN 和 VAE 是理解生成模型的必要历史上下文。风格迁移则是游戏后期处理的经典 AI 应用。

- **GAN（生成对抗网络）**：
  - 理解生成器/判别器的 minimax 博弈
  - DCGAN、StyleGAN 的渐进式生成思想
  - GAN 的缺陷：模式崩溃、训练不稳定（这也是扩散模型崛起的原因）
- **VAE（变分自编码器）**：
  - 理解编码器 `q(z|x)` 和解码器 `p(x|z)`
  - KL 散度作为正则项，让隐空间连续可插值
  - VAE 与扩散模型的联系：DDPM 可以看作一种特殊的 VAE
- **扩散模型深度**：
  - 前向过程：逐步加噪，数学上是马尔可夫链
  - 反向过程：神经网络预测噪声，逐步去噪
  - DDIM：加速采样，从 1000 步降到 50 步
  - Classifier-Free Guidance（CFG）：用无条件生成引导有条件生成，控制创造性与保真度的权衡
  - Latent Diffusion（Stable Diffusion）：在 VAE 潜空间而非像素空间扩散，大幅降低计算量
- **多模态对齐**：
  - CLIP：对比学习让图像和文本共享嵌入空间
  - 理解 `image_encoder` 和 `text_encoder` 的联合训练
- **实战项目**：
  1. **风格迁移重制**：用引擎后处理栈实现实时 Neural Style Transfer（加载预训练 VGG，每帧对 backbuffer 做风格化）
  2. **GAN 纹理生成**：训练一个 DCGAN 生成无缝 tileable 纹理（草地/砖墙/金属）
  3. **ControlNet 实验**：用 ControlNet + Stable Diffusion 根据引擎的深度图/法线图生成可控的概念美术

> **关键资源**：《动手学深度学习》生成模型章节、Stable Diffusion 官方论文、《Diffusion Models Beat GANs》

---

## 阶段 6：编辑器界面与智能体

**引擎里程碑**：拥有自研 Retained-Mode UI 框架，替代 ImGui。文本清晰、布局自动、面板可拖拽。编辑器核心面板重写完成。
**AI 里程碑**：理解 Agentic AI 范式——ReAct、Tool Use、记忆架构。能构建一个最小 Agent 闭环。

**交汇点**：**LLM 作为编辑器的智能助手**。通过自然语言命令操控引擎场景——"创建一个红色立方体，放到场景中央，给它添加旋转动画"。

**验收标准（双线可验证）**：
- [ ] **引擎**：自研 UI 文本标签（SDF 字体）、Flex 布局、按钮/输入框交互、Docking 初版
- [ ] **AI**：实现一个 ReAct Agent，能接收指令→推理→调用工具→观察结果→继续推理
- [ ] **交汇**：在编辑器顶部加一个"AI 命令栏"，输入自然语言，LLM 调用引擎 API（创建实体/修改组件/加载资源）并执行

### 6.1 引擎：自研 UI 框架

- [ ] [[Notes/SelfGameEngine/渲染管线与画面/字体渲染系统|字体渲染系统]] — SDF/MSDF
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/UI画布与场景叠加|UI画布与场景叠加]]
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/反射系统|反射系统]]（回探深化）— 自动反射驱动 Inspector

> **UI 框架后续**：布局系统、事件路由、样式系统、Docking（可在阶段 8 之后继续深化）

### 6.2 AI：Agentic AI 与自主系统

- [ ] [[Notes/人工智能/前沿探索/AgenticAI与自主系统|AgenticAI与自主系统]] — ReAct/ToolUse/记忆

> **交汇实验**：把 Agent 的工具集绑定到引擎的反射 API 上——工具包括：`create_entity(type)`、`set_component(entity, component, field, value)`、`load_asset(path)`。LLM 通过 JSON 格式的 function calling 操控引擎世界。
> **本阶段增量**：AI 从"协作者"升级为"操作员"。
> **下一步**：让角色动起来，让 NPC 有大脑。

### 6.3 支线深化：Agent 系统与 RAG 深度架构

> [!info] 支线说明
> Agent 是大模型应用的下一个范式。以下内容帮你从"调用 API"进化到"构建自主系统"。

- **工具调用（Tool Use / Function Calling）**：
  - 理解 OpenAI/Hugging Face 的 Function Calling 协议：模型输出 JSON 格式的工具调用请求
  - 手写一个最小 Function Calling 解析器：从 LLM 输出中提取工具名和参数
  - 工具描述（Tool Description）的设计：如何让模型理解每个工具的用途和参数约束
- **多 Agent 协作**：
  - **ReAct**：Reasoning + Acting 的交替循环，让模型"先想后做"
  - **Reflexion**：Agent 自我反思，从失败中学习
  - **Multi-Agent**：多个角色分工（Planner + Coder + Reviewer），理解通信拓扑（星型/链型/全连接）
- **长期记忆架构**：
  - 短期记忆：对话历史的滑动窗口管理
  - 长期记忆：向量存储（Vector Store）+ 知识图谱（Knowledge Graph）
  - 记忆检索：根据当前上下文从向量库中召回相关历史
- **RAG 系统深度架构**：
  - 文档加载与分块（Chunking）：按字符/句子/语义分块的策略
  - 嵌入模型选择：OpenAI `text-embedding-3`、Sentence-BERT、BGE
  - 向量数据库对比：Milvus（分布式）、Weaviate（GraphQL）、Chroma（本地轻量）、Pinecone（托管）
  - 检索增强策略：Hybrid Search（BM25 + 向量）、Rerank（交叉编码器）、Query 重写/扩展
  - RAG 评估：检索精度（Recall@K）、生成忠实度（Faithfulness）、答案相关性
- **实战项目**：
  1. **AI 命令行工具**：构建一个能调用 5+ 个工具的 ReAct Agent（文件读写、网络搜索、代码执行、计算器、日志记录）
  2. **多 Agent 编程助手**：Planner Agent 写设计文档 → Coder Agent 写代码 → Reviewer Agent 审代码
  3. **引擎知识库 RAG**：把引擎源码文档、API 手册、设计笔记灌入向量库，实现"问 AI 引擎内部架构"的问答系统

> **关键资源**：论文《ReAct: Synergizing Reasoning and Acting in Language Models》、LangChain/AutoGPT 源码、向量数据库官方文档

---

## 阶段 7：角色行为与语言模型

**引擎里程碑**：动画系统、Motor、物理、导航、输入映射就位。窗口里有能行走/跳跃/播放动画的角色，能碰撞和寻路。
**AI 里程碑**：理解 LLM 的推理与生成机制，能在特定领域微调模型。掌握 RAG 基础。

**交汇点**：**LLM 驱动 NPC 的行为和对话**。不是预写脚本树，而是让 NPC 根据游戏世界状态实时决策和对话。

**验收标准（双线可验证）**：
- [ ] **引擎**：角色 WASD 移动、空格跳跃、播放 idle/walk/run 动画、NavMesh 寻路、碰撞不穿透
- [ ] **AI**：用 LoRA 在游戏剧情数据上微调 7B 模型，构建游戏 Wiki 的 RAG 知识库
- [ ] **交汇**：NPC 实体挂载一个"AI Brain"组件，包含：世界状态观察（附近实体/事件）、LLM 推理生成行为指令（移动/对话/使用技能）、对话历史记忆

### 7.1 引擎：动画与 Gameplay 运行时

- [ ] [[Notes/SelfGameEngine/核心运行时闭环/组件系统架构|组件系统架构]]（深化）
- [ ] [[Notes/SelfGameEngine/渲染管线与画面/基础粒子系统|基础粒子系统]] — 火焰/烟雾/火花

> Gameplay 系统（动画/Motor/物理/导航/GAS）参考 SelfGameEngine 阶段 7。

### 7.2 AI：LLM 应用与微调

- [ ] [[Notes/人工智能/生成式AI与大模型/大规模语言模型|大规模语言模型]]（深化）— RLHF/DPO、长上下文
- [ ] [[Notes/人工智能/前沿探索/WorldModels与具身智能|WorldModels与具身智能]] — 世界模型、Sim-to-Real

> **交汇实验**：构建一个"AI NPC"——玩家靠近时，NPC 观察周围 ECS 组件（玩家装备、天气、时间），生成符合世界观的对话；玩家下达指令时，NPC 调用导航 System 移动到目标点。
> **本阶段增量**：AI 成为游戏世界的一等公民。
> **下一步**：外部 AI 通过标准化接口观察并操作引擎。

### 7.3 支线深化：LLM 推理机制与对齐技术

> [!info] 支线说明
> 让模型"说对话"比让模型"说话"难得多。对齐技术（RLHF/DPO）是现代 LLM 的核心竞争力。

- **RLHF（人类反馈强化学习）**：
  - 三阶段流程：SFT（监督微调）→ Reward Model（奖励模型训练）→ PPO（强化学习优化）
  - 理解 Reward Model 的本质：一个给"回答质量"打分的回归模型
  - PPO 的 KL 散度约束：防止模型为了高分而输出乱码（Reward Hacking）
  - RLHF 的局限性：对齐税（Alignment Tax）、模式崩溃、标注成本高
- **DPO（直接偏好优化）**：
  - 理解 DPO 的核心洞察：不需要显式训练 Reward Model，直接从偏好数据优化
  - DPO vs. PPO：更简单、更稳定、效果相当，工业界正在大量采用
- **长上下文技术**：
  - 位置编码外推：RoPE 的 NTK-aware 缩放、YaRN、PI（Positional Interpolation）
  - 稀疏注意力：Longformer（滑动窗口+全局）、BigBird（随机+窗口+全局）
  - Ring Attention / Striped Attention：分布式长上下文训练
  - 理解"长上下文"为什么难：O(n²) 的注意力复杂度、KV Cache 显存爆炸
- **Test-time Compute（测试时计算）**：
  - Chain-of-Thought：让模型"一步步想"
  - Self-Consistency：多次采样取多数投票
  - Tree of Thoughts：在推理树上搜索最优路径
  - 理解"用更多推理时间换准确率"的新范式
- **实战项目**：
  1. **DPO 微调实验**：收集 100 条游戏对话的偏好数据（A 回答 vs. B 回答），用 DPO 让模型学会"更像某个角色的说话风格"
  2. **长上下文 RAG**：用支持 128K 上下文的模型（如 Llama-3.1-8B）直接吞入整本游戏设定集，对比"长上下文直接读"vs."RAG 检索"的问答质量
  3. **NPC 人格一致性测试**：设计 20 个情境题，测试微调后的模型是否保持角色设定不崩坏

> **关键资源**：论文《Training language models to follow instructions with human feedback》（InstructGPT）、《Direct Preference Optimization》

---

## 阶段 8：AI桥接与推理引擎

**引擎里程碑**：MCP/AgentBridge 连通，外部 AI 能观察引擎状态并发送命令。多 Agent 沙箱、Undo/Redo、脚本热重载就位。
**AI 里程碑**：理解推理优化的工业级实践。在引擎中集成 llama.cpp 或 vLLM，完成本地化 LLM 推理。

**交汇点**：**引擎成为一个可观测、可操作、可推理的智能环境**。AI 不再只是运行在外部的服务，而是内嵌到引擎运行时的基础设施中。

**验收标准（双线可验证）**：
- [ ] **引擎**：外部 AI 通过 MCP 发送 `query_entities`、`set_component`、`step_frame`；Inspector 实时同步；Undo/Redo 完整
- [ ] **AI**：在引擎内嵌入 llama.cpp（C++ 库），加载 GGUF 模型，实现本地化推理（延迟 < 100ms/token）
- [ ] **交汇**：引擎的 AI 桥接层直接调用内嵌 LLM 推理——不需要外部网络，完全本地化 Agent 闭环

### 8.1 引擎：工具链与 AI 桥接

- [ ] [[Notes/SelfGameEngine/核心运行时闭环/反射系统|反射系统]]（回探深化）— AI Schema 导出
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/Plugin与模块系统|Plugin与模块系统]]（深化）— 脚本热重载
- [ ] [[Notes/SelfGameEngine/核心运行时闭环/Prefab与数据层|Prefab与数据层]]（深化）

> MCP/Agent 桥接层参考 SelfGameEngine 阶段 8。

### 8.2 AI：推理优化与本地化部署

- [ ] [[Notes/人工智能/AI系统与工程/推理优化与服务化|推理优化与服务化]] — KV Cache、PagedAttention、量化
- [ ] [[Notes/人工智能/前沿探索/高效架构与模型压缩|高效架构与模型压缩]] — MoE、Mamba、量化

> **交汇实验**：把 llama.cpp 编译为引擎 Plugin，通过引擎的模块系统加载。在编辑器里加一个"本地模型管理器"面板——选择模型文件、设置线程数/上下文长度、实时显示 tok/s 和内存占用。
> **本阶段增量**：AI 推理成为引擎的内置能力，而非外部依赖。
> **下一步**：引擎从玩具变成可发布的产品，AI 从实验走向跨界应用。

### 8.3 支线深化：C++ AI 工程化与推理优化（你的差异化优势）

> [!info] 支线说明
> 这是你最硬核的差异化能力区。游戏开发的实时系统思维、GPU 编程经验、内存优化本能，直接迁移到大模型推理优化。以下内容原版是独立的"阶段三"，现在作为阶段 8 的支线，但重要性丝毫不减。

- **CUDA 编程**：
  - CUDA 基础：Kernel 编写、线程网格（Grid/Block/Thread）、内存层次（Global/Shared/Local/Register）
  - 内存优化：合并访问（Coalesced Access）、Bank Conflict 避免、Shared Memory 的利用
  - Warp 级优化：理解 Warp Divergence、Warp Shuffle、Tensor Core（WMMA）
  - cuBLAS / cuDNN：不要重复造轮子，理解如何调用高性能库
  - 实战：手写一个自定义 CUDA 算子（如特定激活函数或自定义注意力变体），集成到 PyTorch（`torch.utils.cpp_extension.load`）
- **模型推理优化**：
  - **量化技术**：INT8 / INT4 / FP16 / BF16，理解精度与速度的 trade-off。QAT（训练后量化）vs. PTQ（感知量化训练）。
  - **KV Cache 优化**：理解为什么自回归生成需要 KV Cache、如何压缩（MQA / GQA / KV Cache 量化）
  - **投机解码（Speculative Decoding）**：用草稿模型（小模型）生成候选 token，大模型一次验证多个，理解"接受率"的数学期望
  - **连续批处理（Continuous Batching / Inflight Batching）**：对比静态批处理，理解请求到达时间不均匀时的吞吐提升
  - **算子融合（Kernel Fusion）**：将多个小 Kernel 合并为一个大 Kernel，减少 launch overhead 和内存往返
- **推理框架深度使用**：
  - **ONNX Runtime**：跨平台部署，C++ API 实战。理解 Execution Provider（CUDA/TensorRT/DirectML）的切换
  - **TensorRT**：NVIDIA GPU 极致优化，理解网络定义、Builder Config、序列化 Engine
  - **vLLM**：PagedAttention 技术（把 KV Cache 分页管理，类比操作系统的虚拟内存），高吞吐服务架构
  - **llama.cpp**（重点研究）：纯 C++ 实现 LLM 推理、GGUF 格式解析、量化实现（Q4_0/Q5_K_M）、CPU/GPU 混合调度、多线程并行
- **高性能服务架构**：
  - gRPC / HTTP2 服务化：理解 Protocol Buffers、流式传输（Server-Sent Events for LLM）
  - 模型并行：张量并行（Tensor Parallelism，层内切分）vs. 流水线并行（Pipeline Parallelism，层间切分）
  - 动态批处理与请求调度：理解 max_batch_size、max_tokens、超时淘汰策略
- **实战项目**：
  1. **C++ 重写推理引擎**：用 ONNX Runtime C++ API 实现 BERT 推理服务，对比 Python 版本延迟（目标：提升 5-10 倍）
  2. **llama.cpp 深度优化**：为特定游戏 NPC 对话场景优化 7B 模型推理，实现单卡实时对话（< 100ms/token）
  3. **多模态推理服务**：用 C++ 构建 CLIP 图像编码 + LLM 文本生成的联合服务
  4. **CUDA Kernel 优化**：手写自定义 CUDA 算子（如特定激活函数或量化/反量化 Kernel），集成到 PyTorch

> **关键资源**：
> - 项目：llama.cpp（GitHub，纯 C++，必读源码）、vLLM（Python+C++ 混合）、TensorRT-LLM
> - 书籍：《CUDA 编程：基础与实践》《高性能深度学习推理》
> - 文档：NVIDIA TensorRT 文档、ONNX Runtime C++ API 文档

---

## 阶段 9：发布部署与跨域整合

**引擎里程碑**：CI-CD、Pak 打包、性能分析、内存管理全链路就位。引擎具备单机发布的完整能力。
**AI 里程碑**：理解分布式训练、MLOps、AI+科学计算。能把引擎+AI 的组合打包为可交付的产物。

**交汇点**：**引擎作为 AI 研究的实验平台，AI 作为引擎的智能核心**。你拥有的是一个"可编程、可观测、可推理"的智能世界模拟器。

**验收标准（双线可验证）**：
- [ ] **引擎**：CI 自动编译打包，性能面板显示各 System 耗时，内存分析无泄漏
- [ ] **AI**：完成一个跨域 Demo——例如"AI 生成 NeRF 场景并实时在引擎中渲染"或"AI 物理模拟替代传统刚体求解"
- [ ] **交汇**：发布一个可运行的 Demo 包，包含引擎可执行文件 + 内嵌 LLM + AI 驱动 NPC 场景

### 9.1 引擎：扩展与部署

- [ ] [[Notes/SelfGameEngine/扩展与部署/内存管理全链路|内存管理全链路]]
- [ ] [[Notes/SelfGameEngine/扩展与部署/Pak系统与资源打包|Pak系统与资源打包]]
- [ ] [[Notes/SelfGameEngine/扩展与部署/构建系统与CI-CD|构建系统与CI-CD]]
- [ ] [[Notes/SelfGameEngine/扩展与部署/性能分析与调优|性能分析与调优]]

### 9.2 AI：系统与跨界

- [ ] [[Notes/人工智能/AI系统与工程/训练系统与分布式训练|训练系统与分布式训练]]
- [ ] [[Notes/人工智能/AI系统与工程/MLOps与模型生命周期|MLOps与模型生命周期]]
- [x] [[Notes/人工智能/跨域整合/AI与计算机图形学|AI与计算机图形学]] — NeRF / 3DGS
- [ ] [[Notes/人工智能/跨域整合/AI与游戏引擎|AI与游戏引擎]]
- [ ] [[Notes/人工智能/跨域整合/AI与科学计算|AI与科学计算]]

> **本阶段增量**：你拥有了一个"自研 ECS 游戏引擎 + 内嵌 LLM 推理 + AI 驱动内容生成"的完整技术栈。这不仅仅是"学了 AI"或"造了引擎"，而是创造了一个**智能世界模拟器**。

### 9.3 支线深化：AI 基础设施、Rust 与跨界视野

> [!info] 支线说明
> 以下内容让你的技术栈从"能跑"进化到"能规模化交付"。Rust 是 Moonshot AI 等公司的偏好语言，AI 基础设施是生产环境的必修课。

- **分布式训练**：
  - 数据并行（Data Parallelism）：DDP（DistributedDataParallel）、ZeRO（Zero Redundancy Optimizer）
  - 模型并行：张量并行（Megatron-LM）、流水线并行（GPipe/PipeDream）
  - 理解通信瓶颈：All-Reduce 的带宽需求、NVLink vs. PCIe 的差异
- **MLOps 与模型生命周期**：
  - 实验管理：Weights & Biases、MLflow、TensorBoard
  - 模型版本：DVC（Data Version Control）、模型注册表（Model Registry）
  - 部署流水线：训练 → 评估 → 打包 → 金丝雀发布 → A/B 测试 → 回滚
  - 监控：模型漂移（Data Drift）、概念漂移（Concept Drift）、延迟/吞吐/错误率监控
- **AI 基础设施**：
  - **Kubernetes**：模型服务编排、HPA（Horizontal Pod Autoscaler）、GPU 资源调度
  - **Ray**：分布式 AI 计算框架，理解 Ray Train / Ray Serve / Ray RLlib
  - **监控与可观测**：Prometheus + Grafana 监控推理服务、分布式追踪（Jaeger/Zipkin）
- **Rust 基础**（Moonshot AI 偏好）：
  - 所有权与生命周期：类比 C++ RAII，但编译器强制检查
  - 借用检查器：理解 `&T`、`&mut T`、`Box<T>`、`Rc<T>`、`Arc<T>` 的语义
  - 并发安全：`Send` / `Sync` trait，对比 C++ 的 `std::thread` + 锁
  - 在 AI 服务中的应用：Tonic gRPC 服务、使用 `candle`（Rust 的轻量 ML 框架）
- **跨界整合方向**：
  - **AI + 图形学**：NeRF（神经辐射场）原理、3D Gaussian Splatting（3DGS）实时渲染、AI 辅助光照烘焙
  - **AI + 游戏**：PCG（程序化内容生成）+ AI、AI 驱动动画（Motion Matching → 生成式动画）、AI 游戏测试（自动化 QA）
  - **AI + 机器人**：Sim-to-Real（引擎物理仿真 → 真实机器人）、强化学习训练环境（Isaac Gym / MuJoCo 的替代思路）
  - **AI + 科学计算**：AI 求解偏微分方程（PINNs）、分子模拟、天气预报

> **关键资源**：
> - 《The Rust Programming Language》（官方免费）、《Rust for Rustaceans》（进阶）
> - Ray 官方文档、Kubernetes 官方教程
> - NeRF 论文、3D Gaussian Splatting 论文

---

## 依赖关系与双螺旋衔接

```
数学基础 + Python工具链 + 引擎基础类型
    │
    ├──► 窗口与输入系统 ──► 最简图形后端 ──► ImGui ──► 日志面板
    │                                                              │
    ├──► MLP手写训练 ──► MNIST ──► ImGui loss曲线可视化 ◄─────────┘
    │
    ├──► 最小ECS ──► Inspector可视化
    │                      │
    ├──► CNN训练 ──► 特征图导出为纹理 ──► ECS立方体贴图可视化 ◄────┘
    │
    ├──► 基础工具层（数学/字符串/容器/VFS/线程池）
    │                      │
    ├──► 手写Attention ──► 最小Tokenizer（HashMap版）◄────────────┘
    │
    ├──► 核心运行时闭环（反射/场景图/Prefab/Plugin）
    │                      │
    ├──► 加载BERT/GPT-2 ──► 智能Inspector（LLM描述组件）◄─────────┘
    │
    ├──► RHI/材质/2D渲染/异步加载
    │                      │
    ├──► StableDiffusion ──► AI纹理生成面板 ──► 热重载材质 ◄──────┘
    │
    ├──► 自研UI框架
    │                      │
    ├──► ReAct Agent ──► AI命令栏 ──► 自然语言操控引擎 ◄──────────┘
    │
    ├──► 动画/Motor/物理/导航
    │                      │
    ├──► LoRA微调 ──► RAG ──► AI NPC Brain组件 ◄──────────────────┘
    │
    ├──► MCP/AgentBridge/脚本系统
    │                      │
    ├──► llama.cpp嵌入 ──► 本地LLM推理Plugin ◄─────────────────────┘
    │
    └──► CI-CD/性能分析/跨域Demo发布
```

### 双螺旋的三大铁律

1. **引擎永远是 AI 的实验台**：任何 AI 概念学完，立刻追问——"我能在引擎里把它可视化吗？"
2. **AI 永远是引擎的增强层**：任何引擎功能做完，立刻追问——"这里能插入一个 AI 模块吗？"
3. **支线深化不阻塞主线**：交汇点很美好，但不必强求。支线内容可以独立推进，技多不压身。

---

## 附录 A：时间规划与里程碑

### 6 个月速成计划（全职学习）

> [!warning] 速成计划的前提是每天能投入 6-8 小时，且有一定编程基础。支线深化内容可以延后。

| 月份 | 主线重点 | 支线穿插 | 里程碑 |
|:---:|:---|:---|:---|
| 1 | 阶段 0-1：引擎基础类型 + Hello Window + 手写 MLP | Python/PyTorch 快速上手 | 窗口弹出，ImGui 训练面板实时显示 MNIST loss |
| 2 | 阶段 2-3：最小 ECS + CNN + Transformer 基础 | 目标检测基础、Transformer 深度拆解 | ECS Inspector 可视化 CNN 特征图，手写 Attention 跑通 |
| 3 | 阶段 4：核心运行时 + 预训练模型加载 | LoRA 微调、RAG 基础 | BERT/GPT-2 在引擎内推理，智能 Inspector 生效 |
| 4 | 阶段 5-6：渲染管线 + 生成式 AI + 自研 UI | GAN/VAE、Agent 系统 | AI 纹理生成面板、自然语言操控编辑器 |
| 5 | 阶段 7-8：动画 Gameplay + LLM NPC + 推理优化 | CUDA 编程、llama.cpp 源码 | AI NPC 实时对话，本地 LLM < 100ms/token |
| 6 | 阶段 9：发布部署 + 跨域 Demo | Rust 基础、MLOps | 可运行 Demo 包，含引擎 + 内嵌 LLM + AI NPC |

### 12 个月稳健计划（在职学习）

> [!tip] 推荐模式
> 你有全职工作，建议按此节奏稳步推进。引擎和 AI 各占约 50% 精力，周末集中攻坚。

| 季度 | 主线目标 | 支线目标 |
|:---:|:---|:---|
| Q1 | 阶段 0-2：根基 + Hello Window + ECS + CNN | Python/PyTorch、目标检测、风格迁移 |
| Q2 | 阶段 3-4：基础工具层 + Transformer + 核心运行时 + 预训练模型 | Transformer 深度拆解、LoRA 微调、RAG 基础 |
| Q3 | 阶段 5-6：渲染管线 + 扩散模型 + 自研 UI + Agent | GAN/VAE、Agent 深度、多模态 |
| Q4 | 阶段 7-9：动画 + LLM NPC + 推理优化 + 发布部署 | CUDA、llama.cpp 源码、Rust、MLOps、跨界 Demo |

**每日时间分配建议**：
- **工作日晚上**：2 小时理论学习（书/论文/课程）+ 1 小时代码（引擎或 AI 二选一）
- **周末**：8 小时实战（项目/代码/优化），其中 4 小时引擎 + 4 小时 AI
- **每两周**：必须产生一次"引擎↔AI"的交汇尝试，哪怕只是一张纹理或一个 CSV

---

## 附录 B：学习资源清单

### 书籍

#### AI 基础

- 《动手学深度学习》（李沐）— 免费在线，工程导向，**你的第一本书**
- 《深度学习》（Goodfellow）— 你已有的书，重点读第 6-9 章（CNN、RNN、优化）
- 《Understanding Deep Learning》（Simon J.D. Prince，2023）— 现代视角，免费 PDF，概念讲解极佳

#### 工程优化

- 《CUDA 编程：基础与实践》— GPU 编程入门
- 《高性能深度学习推理》— NVIDIA 工程师经验，推理优化必读
- 《大规模分布式系统架构》— 系统设计面试与工程实践

#### Rust

- 《The Rust Programming Language》（官方免费）— 所有权与生命周期的最佳入门
- 《Rust for Rustaceans》（进阶）— 高级模式与并发

### 在线课程

| 课程 | 平台 | 重点 |
|-----|------|------|
| Stanford CS25: Transformers United | Stanford | Transformer 专题，必看 |
| Fast.ai: Practical Deep Learning for Coders | Fast.ai | 工程实战，快速上手 |
| DeepLearning.AI: MLOps Specialization | Coursera | AI 系统与部署 |
| Udacity: CUDA Programming | Udacity | GPU 优化 |
| Andrej Karpathy: Neural Networks from Zero to Hero | YouTube | 从零手写 GPT，最佳原理课 |

### 关键开源项目（精读源码）

> [!warning] 源码阅读建议
> 按优先级从高到低阅读，先深入理解 1-2 个项目，再拓展到其他。不要贪多。

| 项目                            | 语言         | 学习重点                                        |  优先级  |
| ----------------------------- | ---------- | ------------------------------------------- | :---: |
| **llama.cpp**                 | C++        | 纯 C++ 如何实现 LLM 推理、量化实现、GGUF 格式、CPU/GPU 混合调度 | ⭐⭐⭐⭐⭐ |
| **vLLM**                      | Python+C++ | PagedAttention、Continuous Batching 服务架构     | ⭐⭐⭐⭐⭐ |
| **TensorRT-LLM**              | C++        | NVIDIA 极致优化、Kernel 融合、多卡并行                  | ⭐⭐⭐⭐  |
| **ONNX Runtime**              | C++        | 跨平台部署、图优化、Execution Providers               | ⭐⭐⭐⭐  |
| **Hugging Face Transformers** | Python     | 模型架构实现、Tokenizer、Generation 策略              |  ⭐⭐⭐  |
| **minGPT**                    | Python     | Andrej Karpathy 从零手写 GPT，最佳教学代码             | ⭐⭐⭐⭐⭐ |
| **LangChain**                 | Python     | Agent 框架、RAG 编排、工具调用                        |  ⭐⭐⭐  |
| **stable-diffusion.cpp**      | C++        | llama.cpp 作者的扩散模型 C++ 实现                    |  ⭐⭐⭐  |

---

## 附录 C：面试准备与个人品牌

### 目标岗位分析

虽然你短期内没有换工作的打算，但明确目标能让学习更有方向感。根据 Moonshot AI、DeepSeek、字节跳动等公司的招聘趋势，以下岗位最匹配你的背景：

#### AI Infra 工程师（C++ 方向）

> 要求：C++/Go/Rust，熟悉 Linux，有高性能系统经验
> 你的优势：游戏引擎优化经验直接迁移到推理优化

#### 全栈极客工程师（AI-Native 方向）

> 要求：全栈交付能力，AI 驱动研发，能深入系统底层（C++/JNI）
> 你的优势：游戏开发本身就是全栈（图形、物理、网络、脚本）

#### 大模型算法工程师（工程偏重型）

> 要求：深度学习基础，但强调工程实现与系统优化
> 你的优势：C++ 工程能力 + 快速学习的 AI 基础

### 技能树优先级

```
AI 工程核心技能
    ├── 推理优化（最高优先级）
    │   ├── 量化技术（INT8/INT4/FP16）
    │   ├── CUDA 编程与 Kernel 优化
    │   └── KV Cache / PagedAttention
    ├── 系统架构（高优先级）
    │   ├── 分布式服务（gRPC/HTTP2）
    │   └── 推理框架（vLLM/llama.cpp/TensorRT）
    └── 模型基础（中优先级）
        ├── Transformer 深度理解
        ├── 微调技术（LoRA/DPO）
        └── RAG / Agent 系统
```

### 技术面试重点

#### C++ 基础

- 智能指针、内存模型、并发编程（锁、原子操作、无锁队列）
- 模板元编程、CRTP、SFINAE（现代 C++ 特性）
- 性能优化：缓存友好性、分支预测、SIMD

#### 系统设计

- 设计一个高并发 LLM 推理服务（类似 vLLM 架构）
- 设计一个 RAG 系统（延迟与准确性权衡）
- 设计一个游戏 AI Agent 系统（实时性要求）

#### AI 基础

- 解释 Transformer 的注意力机制，手写 Attention 公式
- 解释 LoRA 原理，为什么能降低显存占用
- 解释 KV Cache，如何优化长文本生成
- 解释扩散模型的前向/反向过程

#### 工程优化

- 如何降低 LLM 推理延迟？（量化、投机解码、算子融合）
- 如何提升 LLM 服务吞吐？（Continuous Batching、动态批处理）
- 多卡并行策略选择？（张量并行 vs. 流水线并行）

### 项目展示建议

准备 3 个项目深度讲解，每个项目都要能讲清楚：

1. **C++ 推理优化项目**：展示性能数据（延迟、吞吐、内存占用对比）
2. **完整 AI 应用**：展示系统设计图、技术选型理由、遇到的挑战
3. **开源贡献**：展示 PR 内容、代码 Review 过程、性能提升数据

### GitHub 项目建议

> [!success] 项目方向
>
> #### cpp-ai-toolkit
> 类似 lite.ai.toolkit 的 C++ 推理工具集
> - 包含：ONNX/TensorRT 封装、常用模型 C++ 实现、性能 Benchmark
>
> #### game-llm-optimizer
> 针对游戏场景的 LLM 优化方案
> - NPC 实时对话系统（< 50ms 延迟）
> - 游戏剧情生成器
> - C++ 插件集成 Unity/Unreal Demo
>
> #### cuda-kernel-collection
> 手写 CUDA 算子集合
> - 自定义 Attention Kernel
> - 量化/反量化 Kernel
> - 与 PyTorch 集成示例
>
> #### your-engine-name（你的自研引擎）
> 最核心的项目。包含：
> - ECS 运行时
> - AI 桥接层（MCP/Agent）
> - 内嵌 LLM 推理
> - AI 驱动内容生成

### 技术博客主题

- "从游戏引擎优化到 LLM 推理优化：我的性能优化方法论"
- "用 C++ 重写 PyTorch 推理：性能对比与优化技巧"
- "llama.cpp 源码解析：纯 C++ 如何实现大模型推理"
- "CUDA 优化实战：将游戏 Shader 经验应用于 AI 计算"
- "自研 ECS 游戏引擎 + 内嵌 LLM：构建智能世界模拟器"

---

> **这不是一份学习路线图，这是一份建造蓝图。** 你建造的不仅是引擎，也不仅是 AI 能力——而是一个**属于你自己的、可扩展的、智能化的世界模拟器**。在这个模拟器里，游戏是应用形态之一，科学研究、机器人训练、可视化教育都是可能的出口。
> 
> **主线保证你始终有可见的进度，支线保证你的技术栈没有短板。** 两条路一起走，技多不压身。
