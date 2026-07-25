---
title: 数据通道与更新频率
description: MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform 传？理解 Shader 数据通道的核心不是 API 调用，而是数据更新频率：每顶点、每 Draw Call、每像素插值。API 细节交给 AI，人只负责判断频率。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - glsl
  - uniform
  - vertex-attribute
  - varying
  - constant-buffer
  - material
aliases:
  - Uniform vs Attribute
  - 着色器数据通道
  - 材质参数上传
  - 数据通道与更新频率
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/Shader与光照/Shader程序与跨阶段数据约定|Shader程序与跨阶段数据约定]] — 你已经理解 Shader 作为跨阶段数据约定的本质
> - [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]] — 你已经能把顶点属性从 VBO 配置进顶点着色器
>
> **本模块要解决的像素问题**：Shader 里需要多种数据：每个顶点不同的位置/法线、一次 Draw Call 内共享的 MVP 矩阵、顶点着色器算出来要插值给片段着色器的颜色。这些数据更新频率不同，如果放错通道，要么显存爆炸，要么像素颜色全错。
>
> **本模块增量**：你能按「数据更新频率」正确选择 Attribute / Uniform / Varying / Constant Buffer，能解释为什么 MVP 不能走 Attribute，能描述现代 API 如何表达同一个频率分层。
>
> **下一步**：[[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|光照在管线中的位置]] — 数据通道懂了，用它解决第一个实例：光照。

---

# 数据通道与更新频率

## 问题 0：MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？

你已经见过 Attribute 和 Uniform，但它们的本质区别不是"语法不同"，而是**数据多久变一次**。

```glsl
layout (location = 0) in vec3 aPos;     // Attribute：每个顶点不同
uniform mat4 uMVP;                       // Uniform：一次 Draw Call 内不变
```

MVP 矩阵描述的是"这个物体相对于相机的变换"，同一物体的所有顶点共享同一个矩阵。所以它应该走 **Uniform**。

如果硬塞进 Attribute，每个顶点都存一份 4×4 矩阵，显存浪费 10000 倍，更新时还要重写整个 VBO——这是把"每物体一份"的数据错放到"每顶点一份"的通道里。

---

## 问题 1：Shader 里到底有几条数据通道？

图形 API 不管叫 Attribute、Uniform、Varying 还是 Constant Buffer，本质上只分三类频率：

| 通道 | 频率 | 来源 | 典型用途 |
|------|------|------|---------|
| **Attribute / Vertex Input** | 每个顶点不同 | VBO / Vertex Buffer | 位置、法线、UV、顶点颜色、骨骼权重 |
| **Uniform / Constant Buffer** | 一次 Draw Call / Pass 内不变 | CPU 设置 / Buffer | MVP 矩阵、光源、相机、材质参数 |
| **Varying / Stage Input** | 每个像素不同（由硬件插值） | 上一阶段输出 | 顶点着色器算好的世界坐标、法线、颜色 |

> 表格只用于总结。核心就一句话：**选通道不看数据类型，看数据多久变一次**。

---

## 问题 2：决策规则——这个数据该走哪条通道？

判断方法只有一条：**在一次 Draw Call 中，这个值是否对所有顶点/片段都一样？**

### 走 Attribute 的场景

- 顶点位置、法线、UV、顶点颜色、骨骼权重
- 一句话：跟着顶点走的数据

### 走 Uniform / Constant Buffer 的场景

- MVP / Model / View / Projection 矩阵
- 光源位置、颜色、强度
- 相机位置
- 材质参数（基础色、粗糙度、金属度）
- 时间、全局开关、后处理参数
- 一句话：一次绘制中全局共享的数据

### 走 Varying / Stage Input 的场景

- 顶点着色器算出来的世界空间位置
- 顶点着色器变换后的法线
- 顶点颜色插值到像素
- 一句话：顶点阶段产出、需要按像素频率消费的插值数据

### 一个容易混淆的例子：100 个立方体，每个颜色不同

颜色应该走 Attribute 还是 Uniform？

答案是 **Uniform**。因为颜色是"每个物体"变化的，不是"每个顶点"变化的。每个 Draw Call 设置一次颜色，比给 36 个顶点各存一份颜色高效得多。只有当立方体本身需要渐变时，才走 Attribute。

```cpp
for (const auto& obj : objects) {
    glUniform3f(colorLoc, obj.color.r, obj.color.g, obj.color.b);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}
```

---

## 问题 3：Uniform 太多了怎么办？——Constant Buffer / UBO

单个 Shader 的 Uniform 数量有限，而且每个 Draw Call 都 `glUniform*` 几十次会变成 CPU 开销。工程上的标准解法是 **Constant Buffer / UBO**：把一组 Uniform 打包到一个 Buffer 里，按更新频率分层绑定。

```glsl
layout(std140, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
};

layout(std140, binding = 1) uniform PerObject {
    mat4 model;
    vec4 baseColor;
};
```

| 层级 | 更新频率 | 典型内容 |
|------|---------|---------|
| PerFrame / PerView | 每帧一次 | View、Projection、Camera、全局光源 |
| PerMaterial | 每材质一次 | baseColor、roughness、metallic、贴图引用 |
| PerObject / PerDraw | 每物体一次 | Model 矩阵、自定义实例数据 |

> **为什么这个分层重要？** 因为它让你一次更新、多次复用。相机参数每帧上传一次，所有物体共享；材质参数切换材质时上传一次；Model 矩阵每个物体上传一次。这正是引擎材质系统的底层逻辑。
>
> `std140` 具体对齐规则属于"了解即可"——知道它有严格对齐、会让 C++ 结构体布局错位即可，具体偏移让 AI 或文档处理。

---

## 问题 4：Uniform 的状态跟着谁走？

OpenGL 里，Uniform 是 **Program 对象的状态**，不是 VAO 的状态。所以必须先 `glUseProgram(program)`，再 `glUniform*`。

```cpp
// ❌ 错误：先设置 Uniform，再 Use Program
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
glUseProgram(program);

// ✅ 正确
glUseProgram(program);
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
```

切换 VAO 不会重置 Uniform；切换 Program 才会。现代 API（Vulkan/D3D12）把这个状态绑定做得更显式：Constant Buffer 的绑定跟着命令列表或 DescriptorSet，而不是隐式地挂在 Program 上。

---

## 问题 5：与现代 API 的对照

我们在解决的是「**如何把 CPU 数据按正确频率传给 GPU Shader**」这个具体问题。

| 频率 | OpenGL (GLSL) | Vulkan | D3D12 (HLSL) | Metal |
|------|---------------|--------|--------------|-------|
| 每顶点 | Attribute + VAO/VBO | `VkVertexInputAttributeDescription` + Vertex Buffer | `D3D12_INPUT_ELEMENT_DESC` + Vertex Buffer | `[[attribute(N)]]` + Buffer |
| 每 Draw Call | `uniform` / UBO | Push Constants / Uniform Buffer + DescriptorSet | `cbuffer` / Root Constants / Constant Buffer | `[[buffer(N)]]` constant |
| 每像素插值 | `out` / `in` | SPIR-V Location | Semantic | `vertex` / `fragment` struct |

**关键洞察**：不同 API 的命名和绑定机制不同，但"按频率分层"这个设计原则完全一致。理解了频率，迁移到任何 API 都只是在查对应的对象名。

---

## 与 SelfGameEngine 的关系

### 数据频率 = 引擎材质参数组织的底层逻辑

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被拆成 Template-Asset-Instance 三层。Asset 层定义的颜色、粗糙度、贴图引用，最终都要在 Instance 层变成 GPU 能读的数据。

这些数据进入 GPU 的方式，正是本笔记讲的 Uniform / Constant Buffer / Binding：

```cpp
// 引擎上层：美术定义的材质资产
struct StandardMaterial {
    Vec4 baseColor;
    float roughness;
    float metallic;
    Handle<Texture> albedoMap;
};

// RHI 层：把参数组织成 GPU 绑定
// OpenGL 后端：UBO / glUniform*
// Vulkan 后端：PushConstant + DescriptorSet
```

[[Notes/SelfGameEngine/渲染管线与画面/材质参数绑定与GPU上传|材质参数绑定与GPU上传]] 会展开工业引擎如何按 PerFrame / PerMaterial / PerObject 分层，减少 Draw Call 开销。理解本笔记的"频率分层"是理解那一层的前提。

---

## 常见陷阱

| 陷阱 | 表现 | 原因 |
|------|------|------|
| MVP 走 Attribute | 显存爆炸、更新困难 | 把每物体数据错放到每顶点通道 |
| 颜色走 Attribute | 每个顶点颜色不同才能走 Attribute，否则浪费 | 没按频率判断 |
| 在 `glUseProgram` 前设置 Uniform | 设置无效或设置到错误 Program | Uniform 是 Program 的状态 |
| UBO 数据错位 | 画面参数乱变 | `std140` 对齐规则严格，C++ 结构体布局不匹配 |
| 以为 Uniform 全局共享 | 切换 Program 后参数丢失 | Uniform 是 Program 私有状态 |

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| 数据频率判断 | 每顶点 → Attribute；每 Draw Call → Uniform/Constant Buffer；插值 → Varying |
| Uniform 设置顺序 | 必须先 `glUseProgram`，再 `glUniform*` |
| 大量参数 | 用 UBO / Constant Buffer 按 PerFrame / PerMaterial / PerObject 分层 |
| 跨 Program 共享 | 用 UBO / Constant Buffer，不要靠 `glUniform*` 重复设置 |
| 现代 API 迁移 | 记住频率分层原则，具体对象名查文档 |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| 按频率选择数据通道 | 多纹理材质的纹理单元绑定 |
| Uniform / Attribute / Varying 的区别 | 纹理对象创建与采样 |
| Constant Buffer 的分层思想 | 引擎中的 BindGroup / DescriptorSet 抽象 |
| | 现代 API 的 Push Constants 和 Bindless |

> **下一步**：[[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|光照在管线中的位置]] — 数据通道懂了，用它实现第一个可运行的像素效果：光照。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
