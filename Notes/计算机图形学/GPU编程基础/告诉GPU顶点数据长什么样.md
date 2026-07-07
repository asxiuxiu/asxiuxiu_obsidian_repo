---
title: 告诉 GPU 顶点数据长什么样
description: GPU 看到顶点缓冲区只是一块字节流。必须告诉它每个顶点包含哪些属性、每个属性多长、从哪开始。理解顶点属性布局的本质。
date: 2026-07-07
tags:
  - graphics
  - vao
  - vertex-layout
  - vertex-attribute
  - stride
  - offset
aliases:
  - 顶点属性布局
  - 顶点格式解释
  - Vertex Layout
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/GPU编程基础/把几何数据放到GPU能访问的地方|把几何数据放到 GPU 能访问的地方]] — 你知道顶点数据需要常驻 GPU 显存
>
> **本模块增量**：你能解释为什么 GPU 需要一份"格式说明书"来解读顶点字节流，能设计顶点属性布局（stride/offset/interleaving），能对比不同 API 如何表达"格式解释"这个概念。
>
> **下一步**：[[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算|用 Shader 表达逐像素计算]] — 顶点数据准备好了，但原来在 CPU `computeColor()` 里写的逐像素计算怎么放到 GPU 上？

---

# 告诉 GPU 顶点数据长什么样

## 问题 0：GPU 看到的顶点缓冲区是什么？

上一篇你把顶点数据上传到 GPU 显存。现在 GPU 看到的东西类似这样：

```
地址：  0     12    24    36    48    60    72    84
字节： [■■■■■■■■■■■■][■■■■■■■■■■■■][■■■■■■■■■■■■]...
```

对 GPU 来说，这就是一块连续的字节流。它不知道：
- 一个顶点占多少字节
- 一个顶点里有几个属性
- 每个属性从哪个字节开始、占多少字节、是什么类型

这些信息必须由你显式声明。

---

## 问题 1：软渲染器里怎么知道顶点格式的？

在软渲染器里，顶点格式由 C++ 结构体定义：

```cpp
struct Vertex {
    Vec3 position;   // 12 字节，偏移 0
    Vec3 color;      // 12 字节，偏移 12
    Vec2 uv;         //  8 字节，偏移 24
};                   // 一个顶点共 32 字节

Vertex v = vertices[i];
// 编译器知道 v.position 在偏移 0，v.color 在偏移 12
```

编译器根据结构体布局生成内存访问代码。CPU 读取 `v.color` 时，会自动从正确偏移取值。

GPU 没有 C++ 结构体定义。你必须用 API 调用"手动"告诉它：
- 每个顶点多大：**stride**
- 每个属性从哪开始：**offset**
- 每个属性是什么类型：float / int / normalized 等
- 每个属性有几个分量：vec2 / vec3 / vec4

---

## 问题 2：最 naive 的想法：每次绘制前都配置一遍

```cpp
// 假设顶点缓冲区已经绑定
glBindBuffer(GL_ARRAY_BUFFER, vbo);

// 位置属性：location 0，vec3，float，偏移 0
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                      8 * sizeof(float),  // stride = 一个顶点总字节
                      (void*)0);          // offset = 0

// 颜色属性：location 1，vec3，float，偏移 12
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                      8 * sizeof(float),
                      (void*)(3 * sizeof(float)));

// UV 属性：location 2，vec2，float，偏移 24
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                      8 * sizeof(float),
                      (void*)(6 * sizeof(float)));

glDrawArrays(GL_TRIANGLES, 0, 3);
```

**问题**：如果有 100 个物体，这套配置就要重复 100 次，且容易遗漏。

---

## 问题 3：更好的想法：把格式解释也打包成状态

OpenGL 的解决方案是 **VAO（Vertex Array Object）**：把顶点属性配置打包成一个状态对象，配置一次，之后绑定 VAO 即可恢复。

```cpp
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);          // 开始记录配置

glBindBuffer(GL_ARRAY_BUFFER, vbo);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
glEnableVertexAttribArray(1);
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
glEnableVertexAttribArray(2);

glBindVertexArray(0);            // 结束记录
```

绘制时只需：
```cpp
glBindVertexArray(vao);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

**关键**：VAO 记住的是"属性 0 从 VBO 的偏移 0 读 3 个 float"这种格式解释，而不是 `GL_ARRAY_BUFFER` 的当前绑定。这是状态机里最常踩的坑之一。

---

## 问题 4：Vulkan / D3D12 / Metal 怎么表达同一件事？

现代 API 不把"格式解释"打包成 VAO 这样的对象，而是把它作为**管线状态**的一部分。

### Vulkan

```cpp
VkVertexInputBindingDescription binding{};
binding.binding = 0;
binding.stride = 8 * sizeof(float);
binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

std::array<VkVertexInputAttributeDescription, 3> attrs;
attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};                    // 位置
attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)};    // 颜色
attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    6 * sizeof(float)};    // UV

VkPipelineVertexInputStateCreateInfo info{};
info.vertexBindingDescriptionCount = 1;
info.pVertexBindingDescriptions = &binding;
info.vertexAttributeDescriptionCount = attrs.size();
info.pVertexAttributeDescriptions = attrs.data();
```

注意：Vulkan 把"数据绑定"和"属性解释"都作为 `VkPipeline` 的一部分。换一套顶点格式就要换一个管线。

### D3D12

```cpp
D3D12_INPUT_ELEMENT_DESC layout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
```

### Metal

```cpp
MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
vd.attributes[0].format = MTLVertexFormatFloat3;
vd.attributes[0].offset = 0;
vd.attributes[0].bufferIndex = 0;
vd.attributes[1].format = MTLVertexFormatFloat3;
vd.attributes[1].offset = 12;
vd.attributes[1].bufferIndex = 0;
vd.layouts[0].stride = 32;
```

---

## 问题 5：为什么"数据"和"格式解释"是两个独立决策？

| 决策 | 含义 | 为什么独立 |
|---|---|---|
| **数据在哪** | 顶点缓冲区绑定到哪个槽位 | 同一套格式可以指向不同的缓冲区 |
| **格式是什么** | 每个属性的 stride/offset/type | 同一批数据可以用不同方式解释 |

这种分离的意义：
1. **复用格式**：多个模型共享同一套顶点布局时，只需切换缓冲区绑定，不用重新配置格式
2. **复用缓冲区**：同一批顶点数据可以用不同格式解释（比如只读位置做阴影 Pass）
3. **现代 API 的管线状态**：Vulkan/D3D12 把格式解释放进 PSO，让驱动在创建管线时就能验证格式是否合法

---

## 问题 6：交织布局（Interleaved）vs 分离布局（SoA）

### 交织布局（Interleaved / AoS）

```cpp
struct Vertex {
    Vec3 pos;    // 0
    Vec3 color;  // 12
    Vec2 uv;     // 24
};               // stride = 32
```

```
[pos0 color0 uv0][pos1 color1 uv1][pos2 color2 uv2]...
```

优点：一个顶点所有属性连续，缓存友好（读一个顶点时一次性把相关属性都读进来）。
缺点：如果某个 Pass 只需要位置，其他属性也会被一起读入缓存，浪费带宽。

### 分离布局（SoA / Structure of Arrays）

```
[pos0 pos1 pos2 ...][color0 color1 color2 ...][uv0 uv1 uv2 ...]
```

优点：位置 Pass 只读位置缓冲区，节省带宽。
缺点：需要多个 VBO/缓冲区绑定；读一个完整顶点时要访问多个内存位置。

---

## 本模块还缺什么？

| 已建立 | 待实践 |
|---|---|
| 顶点属性布局的概念 | 顶点着色器怎么写 |
| stride/offset/interleaving 的设计 | 片段着色器怎么拿到插值后的数据 |
| 各 API 的格式表达方式 | Shader 编译和链接 |

---

> **下一步**：[[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算|用 Shader 表达逐像素计算]]
>
> 顶点数据已经在 GPU 上并且格式也解释清楚了。但原来在 CPU `computeColor()` 里写的逐像素计算，怎么放到 GPU 上？这就是 Shader 要解决的问题。

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
