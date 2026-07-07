---
title: 软渲染器到 GPU 管线的映射
description: 把你在 CPU 上写的软渲染器代码，逐行、逐项地映射到 GPU 管线的各个阶段。这是从"我会写 for 循环"到"我会驱动 GPU"的桥梁。
date: 2026-07-07
tags:
  - graphics
  - graphics-pipeline
  - software-rasterizer
  - gpu
aliases:
  - 软渲染器映射
  - CPU 软渲染到 GPU 管线
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] — 你写过 Bresenham 直线、边缘函数、重心坐标
> - [[Notes/计算机图形学/软光栅化与3D数学/3D光栅化与深度缓冲|3D光栅化与深度缓冲]] — 你实现过 MVP 变换、视口变换、Z-Buffer
>
> **本模块增量**：你能画出一张完整的"软渲染器代码 → GPU 管线阶段"映射表，知道 CPU 上每一行 for 循环在 GPU 上被哪个机制替代，以及哪些是固定硬件、哪些是可编程 Shader。
>
> **下一步**：[[Notes/计算机图形学/GPU编程基础/CPU怎么向GPU发命令|CPU 怎么向 GPU 发命令]] — CPU 和 GPU 不共享内存/状态，程序怎么让 GPU 执行一段计算？

---

# 软渲染器到 GPU 管线的映射

## 问题 0：我写的软渲染器代码，在 GPU 上由谁执行？

回顾一下你的软渲染器核心骨架：

```cpp
// 1. 顶点处理：把模型空间顶点变换到屏幕空间
for (每个顶点) {
    vec4 clip = projection * view * model * vec4(vertex.pos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 screen = viewport_transform(ndc);
}

// 2. 遍历每个三角形
for (每个三角形) {
    // 3. 计算屏幕空间包围盒
    int minX = ..., maxX = ...;
    int minY = ..., maxY = ...;

    // 4. 逐像素遍历
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            // 5. 判断像素是否在三角形内
            if (!inside_triangle(x, y, tri)) continue;

            // 6. 计算屏幕空间重心坐标（注意：属性插值需要做透视校正）
            vec3 bary = compute_barycentric(x, y, tri);
            float z = interpolate_depth(bary, tri);     // 透视校正后的深度

            // 7. 深度测试
            if (z >= zbuffer[x, y]) continue;

            // 8. 透视校正后计算颜色
            vec3 color = compute_color(bary, tri, lights, material);

            // 9. 写入帧缓冲
            framebuffer[x, y] = color;
            zbuffer[x, y] = z;
        }
    }
}
```

这段代码全部在 CPU 上按顺序执行。每个像素、每个三角形、每个顶点都被你的 C++ 程序显式遍历。

现在的问题是：**同样的像素结果，GPU 不执行这段 C++ 代码，那它怎么得到？**

---

## 问题 1：软渲染器的外层循环 `for(每个三角形)` 在 GPU 上对应什么？

在软渲染器里，外层循环显式枚举所有要画的三角形。GPU 上，这个枚举由 **绘制命令（Draw Call）** 触发。

但更准确地说，GPU 不关心"有几个 for 循环"，它关心的是：**有一批顶点，按三角形方式组装，需要被处理**。

```
软渲染器：                    GPU 管线：
for(每个三角形) {              DrawCall(顶点数, 三角形数)
    ...                   ──►  顶点处理阶段
}                              图元装配阶段
                               光栅化阶段
                               片段处理阶段
                               输出合并阶段
```

**关键变化**：你的 `for(每个三角形)` 不是被 GPU"翻译"成另一个循环，而是被一条绘制命令取代。GPU 收到这条命令后，会启动一条固定的硬件流水线，自动完成从顶点到像素的全过程。

但这引出一个更深层的问题：GPU 为什么不需要你写循环？答案在接下来的问题里逐步展开。

---

## 问题 2：软渲染器的顶点处理对应 GPU 的什么？

软渲染器里的顶点处理：

```cpp
for (每个顶点) {
    vec4 clip = projection * view * model * vec4(vertex.pos, 1.0);
}
```

在 GPU 上，这段代码被放到 **顶点着色器（Vertex Shader）** 里。

**为什么必须拆出来？**

因为 GPU 要并行处理成千上万个顶点。顶点着色器是 GPU 上的一段小程序，每个顶点执行一次。你不再用 C++ `for` 循环遍历顶点，而是把这段计算写成 GLSL/HLSL，GPU 会自动为每个顶点启动一个实例。

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
}
```

**映射关系**：

| 软渲染器 | GPU 对应物 | 可编程？ |
|---|---|---|
| `for(每个顶点)` 的循环体 | 顶点着色器 | ✅ 可编程 |
| 顶点结构体 `Vertex { vec3 pos; vec3 color; }` | 顶点属性布局（Vertex Layout） | ❌ 格式声明 |
| 全局常量 `model/view/projection` | Uniform / 常量缓冲 | ❌ 只读绑定 |

---

## 问题 3：软渲染器的三角形覆盖判断 `inside_triangle()` 对应 GPU 的什么？

软渲染器里：

```cpp
for (y ...) {
    for (x ...) {
        if (!inside_triangle(x, y, tri)) continue;
    }
}
```

这个判断用的是 **边缘函数（Edge Function）**。在 GPU 上，它由 **光栅化器（Rasterizer）** 这个固定硬件完成。

**为什么是固定硬件，而不是 Shader？**

因为边缘函数、重心坐标、透视校正插值的算法对所有人都是一样的。GPU 厂商把它做成专用硬件，原因有三：
1. **速度**：专用硬件比通用 Shader 快一个数量级
2. **功耗**：固定电路比运行通用程序省电
3. **确定性**：所有 GPU 的光栅化规则相同，保证像素级一致性

```
软渲染器：                          GPU：
for(y) for(x) {                     Rasterizer
    if (edge0(x,y) > 0 &&        （固定硬件）
        edge1(x,y) > 0 &&
        edge2(x,y) > 0) {
        ...
    }
}
```

**关键认知**：光栅化器不是"执行你的 if 语句"，而是**并行地对一个三角形覆盖的所有像素生成片段（Fragment）**。每个片段带有一组插值后的属性（位置、颜色、UV、深度），等待片段着色器处理。

> [!important] 这里的三角形顶点已经是屏幕空间顶点
> `inside_triangle(x, y, tri)` 和 `compute_barycentric(x, y, tri)` 用的 `tri`，是顶点经过 `viewport_transform(ndc)` 之后得到的**屏幕空间顶点**。也就是说，三角形覆盖判断和重心坐标都是在二维屏幕空间进行的。
>
> 但注意：**屏幕空间重心坐标不能直接线性插值深度、颜色、UV**。因为透视投影后，属性在屏幕空间不再是线性变化。所以 `interpolate_depth(bary, tri)` 和 `compute_color(bary, tri, ...)` 内部要做**透视校正插值**。

### 什么是透视校正插值？

透视投影有个效果：同样长的边，离相机越远，在屏幕上看起来越短。于是世界空间里均匀分布的属性（UV、颜色、深度），投影到屏幕空间后就不再均匀了。如果你直接按屏幕空间重心坐标线性插值，结果会出错——远的部分被"压"得太小，纹理会歪。

**正确做法**：先对每个顶点把属性除以深度（或 `w`），在屏幕空间上对这些"除以深度后的值"做线性插值，最后再除以 `1/z` 的插值结果。

```cpp
// 以一维纹理坐标 u 为例
u_correct = [(1-t) * u0/z0 + t * u1/z1]
          / [(1-t) * 1/z0  + t * 1/z1];
```

为什么要这样？因为 `u / z` 和 `1 / z` 在屏幕空间是线性变化的，而 `u` 本身不是。这个修正就叫**透视校正插值**（Perspective-Correct Interpolation）。现代 GPU 的光栅化器会**自动**帮你做这个校正，所以 Shader 里拿到的 `varying`/`in` 变量默认就是透视校正后的值。详见 [[Notes/计算机图形学/软光栅化与3D数学/3D光栅化与深度缓冲#透视校正插值|3D 光栅化与深度缓冲]]。

---

## 问题 4：软渲染器的颜色计算 `compute_color()` 对应 GPU 的什么？

软渲染器里：

```cpp
vec3 color = compute_color(bary, tri, lights, material);
```

在 GPU 上，这段代码被放到 **片段着色器（Fragment Shader）** 里。

片段着色器对每个像素执行一次，输入是光栅化器插值后的属性（颜色、UV、法线、深度等），输出是最终像素颜色。

```glsl
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
```

**映射关系**：

| 软渲染器 | GPU 对应物 | 可编程？ |
|---|---|---|
| `compute_color()` 函数体 | 片段着色器 | ✅ 可编程 |
| 重心坐标插值颜色 | 光栅化器自动插值 `vColor` | ❌ 固定硬件 |
| 光源、材质参数 | Uniform / 常量缓冲 | ❌ 只读绑定 |
| 纹理采样 | 纹理单元 + Sampler | ❌ 专用硬件（采样器） |

---

## 问题 5：软渲染器的深度测试和帧缓冲写入对应 GPU 的什么？

软渲染器里：

```cpp
if (z >= zbuffer[x, y]) continue;
framebuffer[x, y] = color;
zbuffer[x, y] = z;
```

在 GPU 上，这两步由 **输出合并阶段（Output Merger / ROP）** 完成：

```
软渲染器：                          GPU：
if (z >= zbuffer[x,y]) continue;    Z-Test 单元（固定硬件）
framebuffer[x,y] = color;           ROP / Color Blend 单元（固定硬件）
zbuffer[x,y] = z;                   Depth/Stencil Write 单元（固定硬件）
```

**注意**：深度测试虽然由固定硬件执行，但你可以在片段着色器里用 `discard` 提前丢弃片段，或者在 API 层配置深度比较函数（`GL_LESS`、`GL_LEQUAL` 等）。

---

## 问题 6：固定硬件 vs 可编程 Shader 的分界线是怎么划出来的？

把上面的映射汇总，你会发现 GPU 管线被分成两半：

```
CPU 侧：
  顶点数据（在内存 / 显存里）
    │
    ▼
GPU 管线：
  ┌─────────────────────────────────────────────────┐
  │ 顶点着色器（Vertex Shader）    ← 可编程          │
  │   输入：顶点属性                                  │
  │   输出：裁剪空间位置 + 顶点属性                    │
  └─────────────────────────────────────────────────┘
    │
    ▼
  图元装配（Primitive Assembly）    ← 固定硬件
    │
    ▼
  光栅化器（Rasterizer）            ← 固定硬件
    │
    ▼
  ┌─────────────────────────────────────────────────┐
  │ 片段着色器（Fragment Shader）  ← 可编程          │
  │   输入：插值后的片段属性                          │
  │   输出：像素颜色 + 深度                           │
  └─────────────────────────────────────────────────┘
    │
    ▼
  输出合并（Output Merger / ROP）   ← 固定硬件（可配置）
    │
    ▼
  帧缓冲（Frame Buffer）
```

**分界线的逻辑**：

- **可编程**：每个应用都想做不同的事情（顶点变换方式、颜色计算方式、光照模型）
- **固定硬件**：所有应用都想做同样的事情（三角形覆盖判断、重心坐标插值、深度测试、颜色混合）

这个分界不是随意的。固定硬件的部分是 GPU 厂商用专用电路加速的"共同子程序"；可编程的部分是留给开发者写自定义逻辑的"钩子"。

---

## 问题 7：为什么 GPU 不能直接用我的 C++ 光栅化代码？

既然软渲染器已经能正确生成像素，为什么不能把它编译到 GPU 上跑？

**原因 A：GPU 和 CPU 是两种处理器**

CPU 擅长复杂控制流、分支预测、乱序执行；GPU 擅长大量线程执行相同指令。你的 C++ 代码里有 `if`、`continue`、函数调用，这些在 GPU 上效率很低。

**原因 B：GPU 需要显式拆分数据并行部分**

软渲染器里一个函数 `rasterizeTriangle()` 同时做了顶点处理、光栅化、着色、深度测试。GPU 要求你把"每个顶点都一样的计算"和"每个像素都一样的计算"分别拆成顶点着色器和片段着色器。

**原因 C：光栅化和深度测试等步骤在硬件里**

这些步骤被做成固定功能，是因为它们太快、太耗电、太需要像素级一致性。你不会想用 Shader 重新实现一个光栅化器。

---

## 汇总表：软渲染器 → GPU 管线逐项映射

| 软渲染器代码 | GPU 管线阶段 | 可编程？ | 说明 |
|---|---|---|---|
| `for(每个顶点) { MVP 变换 }` | **顶点着色器** | ✅ | 每个顶点并行执行 |
| 三角形组装、裁剪、透视除法 | **图元装配** | ❌ | 固定硬件 |
| `for(y) for(x) { 边缘函数判断 }` | **光栅化器** | ❌ | 固定硬件，并行生成片段 |
| 重心坐标插值颜色/UV/深度 | **插值器** | ❌ | 光栅化器的一部分 |
| `compute_color()` | **片段着色器** | ✅ | 每个像素并行执行 |
| `if (z >= zbuffer) continue;` | **深度测试（Z-Test）** | ⚠️ | 固定硬件，但比较函数可配置 |
| `framebuffer[x,y] = color;` | **ROP / 颜色混合** | ⚠️ | 固定硬件，混合模式可配置 |
| `zbuffer[x,y] = z;` | **深度写入** | ⚠️ | 固定硬件，开关可配置 |

> **符号说明**：✅ 可编程；❌ 固定硬件；⚠️ 固定硬件但可配置行为

---

## 本模块还缺什么？

| 已建立 | 待实践 |
|---|---|
| 软渲染器到 GPU 管线的全局映射 | CPU 怎么向 GPU 发命令 |
| 哪些步骤是 Shader、哪些是固定硬件 | GPU 具体怎么并行执行 Shader |
| 顶点/片段着色器的职责分界 | 顶点数据怎么放到 GPU 能访问的地方 |

---

> **下一步**：[[Notes/计算机图形学/GPU编程基础/CPU怎么向GPU发命令|CPU 怎么向 GPU 发命令]]
>
> 现在你知道了软渲染器的每一步对应 GPU 的哪个机制。但 CPU 和 GPU 是两个独立处理器，你的 C++ 程序怎么让 GPU 执行这些步骤？这需要一个"命令通道"。

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
