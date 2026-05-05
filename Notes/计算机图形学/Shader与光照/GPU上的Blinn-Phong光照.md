---
title: 光照、着色与渲染管线
description: 像素应该是什么颜色？从Blinn-Phong到渲染管线，理解现代GPU的完整渲染流程。
date: 2026-03-29
tags:
  - graphics
  - shading
  - pipeline
  - blinn-phong
aliases:
  - Illumination
  - Shading
  - Graphics Pipeline
---

> **前置依赖**：[[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理映射与UV坐标]] — 你已经理解纹理采样
> **本模块增量**：你能推导 Blinn-Phong 光照模型，理解渲染管线的每个阶段，知道顶点/片段着色器各自的工作。
> **下一步**：[[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] — 理解模型数据怎么存储，或者进入 [[Notes/计算机图形学/Roadmap|Roadmap]] 的 GPU 编程阶段，开始 GPU 实践。

---

# 光照、着色与渲染管线

## 问题 0：光栅化告诉我们哪个像素被覆盖，但没有告诉我们像素应该是什么颜色

在 [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] 中，我们用边缘函数判断像素是否在三角形内部。但"在内"之后呢？像素应该是什么颜色？

**最 naive 的方案**：每个三角形一个固定颜色。

**发现的问题**：所有三角形都是纯色的，没有明暗、没有立体感、没有材质感。

**我们需要什么**：计算物体表面颜色的方法。这就是**着色（Shading）**——模拟光线如何与物体交互，确定每个像素的颜色。

---

## 问题 1：光线与物体表面怎么交互？

真实世界中，我们看到的颜色取决于三个因素：

| 光的分量 | 通俗解释 | 决定什么 |
|---------|---------|---------|
| **高光（Specular）** | 光滑表面的镜面反射 | 亮斑、反光 |
| **漫反射（Diffuse）** | 粗糙表面的均匀散射 | 物体基本颜色 |
| **环境光（Ambient）** | 间接光照的近似 | 避免纯黑区域 |

### 漫反射：兰伯特余弦定律

粗糙表面向各个方向均匀反射光线。反射强度与入射角余弦成正比：

```
L_diffuse = k_d × I × max(0, n · l)
```

- `k_d`：漫反射系数（物体颜色）
- `I`：光源强度
- `n`：表面法线（单位向量）
- `l`：指向光源的方向（单位向量）
- `n · l = cos(θ)`：光线垂直表面时最亮，平行时为零

> 为什么用 `max(0, ...)`？因为当光线从背面照射时（`n · l < 0`），表面不应该被照亮。

### 高光：Blinn-Phong 改进

光滑表面会像镜子一样反射光线。Phong 模型用**反射方向**计算高光，但计算反射方向比较麻烦。

**Blinn-Phong 改进**：用**半程向量（Halfway Vector）**代替反射方向。

```
h = normalize(v + l)    // v = 视线方向, l = 光源方向

L_specular = k_s × I × max(0, n · h)^p
```

- `k_s`：高光系数
- `p`：光泽度（shininess），越大高光越集中
- `p = 100~200` 模拟金属，`p = 10~50` 模拟塑料

### 环境光：近似间接光照

真实世界光线会经过多次反射到达物体。我们不计算全局光照，用一个常数近似：

```
L_ambient = k_a × I_a
```

### 完整 Blinn-Phong 模型

```
L = L_ambient + L_diffuse + L_specular
  = k_a × I_a + k_d × I × max(0, n·l) + k_s × I × max(0, n·h)^p
```

---

## 问题 2：在哪计算光照？——着色频率

光照公式应该在哪个阶段计算？

| 频率 | 计算位置 | 特点 |
|-----|---------|------|
| **Flat Shading** | 每个三角形一次 | 最快，有明显棱角 |
| **Gouraud Shading** | 每个顶点一次，内部插值 | 平衡，适合平滑表面 |
| **Phong Shading** | 每个像素一次 | 最慢，效果最好 |

### Phong Shading（像素着色）

对每个像素：
1. 插值得到像素法线
2. 用插值后的法线计算光照
3. 效果最精确，高光不会丢失

> 现代 GPU 几乎总是使用 Phong Shading（像素着色），因为 GPU 的并行计算能力足够强大。

---

## 问题 3：渲染管线是怎么组织的？

现代 GPU 的渲染管线是一个数据流处理过程：

```
顶点数据 ──→ 顶点着色器 ──→ 图元装配 ──→ 光栅化 ──→ 片段着色器 ──→ 输出合并
 (x,y,z)      (MVP变换)      (组装三角形)   (像素化)     (计算颜色)      (深度/混合)
```

### 各阶段详解

#### 1. 顶点处理阶段

**输入**：顶点数据（位置、法线、纹理坐标、颜色等）

**顶点着色器（Vertex Shader）**：
- 对每个顶点执行
- 主要任务：MVP 变换
- 输出：裁剪空间坐标

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
```

#### 2. 图元装配

- 将顶点组装成三角形
- 进行裁剪（去掉视锥体外的部分）
- 透视除法，转换到 NDC

#### 3. 光栅化

- 将三角形离散化为片段（Fragments）
- 属性插值（颜色、深度、纹理坐标等）
- 这里会用到 [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] 中的边缘函数和重心坐标

#### 4. 片段处理阶段

**片段着色器（Fragment Shader）**：
- 对每个片段（像素候选）执行
- 主要任务：计算最终颜色
- 可以访问纹理、执行光照计算等

```glsl
#version 330 core
in vec3 FragPos;
in vec3 Normal;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 objectColor;

out vec4 FragColor;

void main() {
    // 环境光
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;
    
    // 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // 高光
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;
    
    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
```

#### 5. 输出合并

- 深度测试（Z-Test）
- 模板测试（Stencil Test）
- 混合（Blending，处理透明）
- 写入帧缓冲

### 可编程 vs 固定管线

| 阶段 | 类型 | 说明 |
|-----|------|------|
| 顶点着色器 | 可编程 | 必须实现 |
| 几何着色器 | 可编程 | 可选 |
| 光栅化 | 固定 | 硬件实现 |
| 片段着色器 | 可编程 | 必须实现 |

---

## 本模块还缺什么？

| 已理解 | 待实践 |
|--------|--------|
| Blinn-Phong 公式推导 | 在 GLSL 中实现它 |
| 渲染管线概念 | 顶点/片段着色器的数据流 |
| 着色频率的区别 | Uniform vs Attribute 传数据 |

> **下一步**：
> - [[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] — 理解模型数据格式，为 GPU 加载做准备
> - 或进入 [[Notes/计算机图形学/Roadmap|Roadmap]] 的 GPU 编程阶段 — 开始 OpenGL GPU 实践
