---
title: 从 OBJ 文件到第一个模型
description: Graphics Journey 阶段二的综合实践。把阶段二所学的所有概念串成一条链：解析 OBJ 文件 → 构建交织顶点数据 → 生成索引 → 上传 VBO/EBO → 配置 VAO → 编写 Shader → 触发绘制命令 → 屏幕上出现第一个外部模型。
date: 2026-07-15
tags:
  - graphics
  - obj
  - model-loading
  - vbo
  - vao
  - ebo
  - gpu-memory
  - practice
aliases:
  - OBJ 模型加载
  - 第一个外部模型
  - GPU 模型渲染实践
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/GPU编程基础/软渲染器到GPU管线的映射|软渲染器到 GPU 管线的映射]] — 你知道软渲染器的每个步骤对应 GPU 管线的哪个环节
> - [[Notes/计算机图形学/GPU编程基础/CPU怎么向GPU发命令|CPU 怎么向 GPU 发命令]] — 你知道 CPU 通过命令通道驱动 GPU
> - [[Notes/计算机图形学/GPU编程基础/把几何数据放到GPU能访问的地方|把几何数据放到 GPU 能访问的地方]] — 你知道顶点数据需要常驻 GPU 显存
> - [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]] — 你知道 VAO 解释顶点格式、EBO 复用顶点
> - [[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算|用 Shader 表达逐像素计算]] — 你知道顶点/片段着色器的分工和数据流
> - [[Notes/计算机图形学/GPU编程基础/一条绘制命令触发整条流水线|一条绘制命令触发整条流水线]] — 你知道一条绘制命令如何触发 GPU 管线
> - [[Notes/计算机图形学/GPU编程基础/把画好的像素显示到屏幕上|把画好的像素显示到屏幕上]] — 你知道双缓冲/VSync 如何把像素显示到屏幕
>
> **本模块要解决的像素问题**：阶段二已经学会了 GPU 管线的每个环节，但这些都用独立的小例子演示。真实工程中，一个外部模型（OBJ）从文件到屏幕像素，要经过哪些步骤？每个步骤对应阶段二的哪篇笔记？
>
> **本模块增量**：你能把 OBJ 文件解析、顶点去重、交织布局、VBO/EBO 上传、VAO 配置、Shader 编写、绘制命令触发这一整条链路串起来，在窗口中渲染出第一个外部模型。
>
> **下一步**：[[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL 数据流与着色器编译]] — 模型能画了，但还只有纯色。怎么让 Shader 接收更多数据通道、实现光照？

---

# 从 OBJ 文件到第一个模型

这篇笔记不是讲新概念，而是把阶段二的所有概念串成一条可运行的链。读完这篇笔记，你应该能在窗口里看到一个从 OBJ 文件加载的模型。

## 完整链路概览

```
OBJ 文件
   │
   │ 文本解析
   ▼
v / vt / vn / f 原始数据
   │
   │ 顶点去重：(posIdx, uvIdx, normalIdx) → 唯一顶点索引
   ▼
交织顶点数组 vertices[] + 索引数组 indices[]
   │
   │ glBufferData
   ▼
VBO（顶点数据） + EBO（索引数据）
   │
   │ glVertexAttribPointer + VAO 记录
   ▼
VAO（格式说明 + EBO 绑定）
   │
   │ glUseProgram + glBindVertexArray
   ▼
glDrawElements
   │
   ▼
顶点着色器 → 光栅化 → 片段着色器 → 帧缓冲 → 交换链 → 屏幕
```

## 步骤 1：解析 OBJ 文件

OBJ 是纯文本格式，关键行：

```obj
# 顶点位置
v 1.0 0.0 0.0
v 0.0 1.0 0.0
v 0.0 0.0 1.0

# 纹理坐标
vt 0.0 0.0
vt 1.0 0.0
vt 0.5 1.0

# 顶点法线
vn 0.0 0.0 1.0

# 面片：位置索引/UV索引/法线索引
f 1/1/1 2/2/1 3/3/1
```

注意：OBJ 索引从 **1** 开始，解析后要先减 1 再使用。

## 步骤 2：顶点去重与交织布局

OBJ 的一个"面顶点"可能是 `(posIdx, uvIdx, normalIdx)` 的组合。同一个几何点在不同面上可能有不同的 UV 或法线，所以不能直接用位置索引当 GPU 顶点索引。

**核心操作**：把 `(posIdx, uvIdx, normalIdx)` 三元组作为键，建立到**新顶点索引**的映射。

```cpp
struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::unordered_map<std::tuple<int,int,int>, uint32_t, TupleHash> vertexCache;

for (auto& face : faces) {
    for (auto& corner : face.corners) {
        auto key = std::make_tuple(corner.posIdx, corner.uvIdx, corner.normalIdx);
        auto it = vertexCache.find(key);
        if (it == vertexCache.end()) {
            Vertex v;
            v.pos = positions[corner.posIdx];
            v.uv = uvs[corner.uvIdx];
            v.normal = normals[corner.normalIdx];
            uint32_t newIdx = vertices.size();
            vertices.push_back(v);
            vertexCache[key] = newIdx;
            indices.push_back(newIdx);
        } else {
            indices.push_back(it->second);
        }
    }
}
```

这一步的输出是：
- `vertices[]`：去重后的交织顶点数组（位置 + 法线 + UV）
- `indices[]`：面片索引数组

## 步骤 3：上传到 GPU

### 创建 VBO

```cpp
GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER,
             vertices.size() * sizeof(Vertex),
             vertices.data(),
             GL_STATIC_DRAW);
```

### 创建 EBO

```cpp
GLuint ebo;
glGenBuffers(1, &ebo);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
glBufferData(GL_ELEMENT_ARRAY_BUFFER,
             indices.size() * sizeof(uint32_t),
             indices.data(),
             GL_STATIC_DRAW);
```

## 步骤 4：配置 VAO

在 VAO 作用域内同时配置顶点属性和 EBO 绑定：

```cpp
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);

glBindBuffer(GL_ARRAY_BUFFER, vbo);

// 位置：location 0
 glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                       (void*)offsetof(Vertex, pos));
glEnableVertexAttribArray(0);

// 法线：location 1
 glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                       (void*)offsetof(Vertex, normal));
glEnableVertexAttribArray(1);

// UV：location 2
 glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                       (void*)offsetof(Vertex, uv));
glEnableVertexAttribArray(2);

// EBO 绑定会被记录到 VAO
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

glBindVertexArray(0);
```

## 步骤 5：编写最小 Shader

```glsl
// 顶点着色器
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec2 vUV;

uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
    vUV = aUV;
}
```

```glsl
// 片段着色器
#version 330 core
in vec2 vUV;
out vec4 FragColor;

void main() {
    // 暂时不用纹理，用 UV 的棋盘格验证数据正确
    float checker = step(0.5, fract(vUV.x * 4.0)) * step(0.5, fract(vUV.y * 4.0));
    FragColor = vec4(vec3(0.8 + 0.2 * checker), 1.0);
}
```

## 步骤 6：绘制

```cpp
glUseProgram(program);
glBindVertexArray(vao);
glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
```

## 阶段二概念对照表

| 步骤 | 对应阶段二笔记 | 解决的像素问题 |
|---|---|---|
| OBJ 文本解析 | 无（文件 IO） | 模型数据在磁盘上怎么存 |
| 顶点去重 | [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样#问题%208%3A%20顶点可以复用时，怎么告诉%20GPU%20哪些顶点组成三角形？\|顶点复用]] | 同一个几何点在不同面上 UV/法线不同时怎么办 |
| VBO 上传 | [[Notes/计算机图形学/GPU编程基础/把几何数据放到GPU能访问的地方\|把几何数据放到 GPU 能访问的地方]] | 怎么避免每帧从 CPU 拷贝顶点 |
| VAO 配置 | [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样\|告诉 GPU 顶点数据长什么样]] | GPU 怎么知道"前 3 个 float 是位置" |
| EBO 绑定 | [[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样#问题%208%3A%20顶点可以复用时，怎么告诉%20GPU%20哪些顶点组成三角形？\|EBO]] | 怎么让 8 个顶点画出 12 个三角形 |
| Shader | [[Notes/计算机图形学/GPU编程基础/用Shader表达逐像素计算\|用 Shader 表达逐像素计算]] | 顶点数据怎么变成像素颜色 |
| 绘制命令 | [[Notes/计算机图形学/GPU编程基础/一条绘制命令触发整条流水线\|一条绘制命令触发整条流水线]] | 怎么触发整条 GPU 管线 |
| 显示到屏幕 | [[Notes/计算机图形学/GPU编程基础/把画好的像素显示到屏幕上\|把画好的像素显示到屏幕上]] | 怎么避免画面撕裂 |

## 常见错误

1. **OBJ 索引从 1 开始**：忘记减 1 会导致索引越界或读取到错误顶点。
2. **去重键只用位置索引**：同一个位置在不同面上可能有不同 UV/法线，必须包含三个索引。
3. **EBO 没绑进 VAO**：`glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo)` 必须在 `glBindVertexArray(vao)` 和 `glBindVertexArray(0)` 之间。
4. **VAO 配置时没绑 VBO**：`glVertexAttribPointer` 会捕获当前 `GL_ARRAY_BUFFER` 绑定，必须在绑定 VBO 之后调用。
5. **绘制时 count 单位错误**：`glDrawElements` 的 `count` 是**索引个数**，不是三角形个数。

## 下一步

模型能画了，但还只有纯色或棋盘格。下一步进入阶段四 [[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL 数据流与着色器编译]]，学习怎么让 Shader 接收更多数据通道，最终实现光照。

---

> **下一步**：[[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL 数据流与着色器编译]]
>
> 模型能画了，但还只有纯色。怎么让 Shader 接收更多数据通道、实现光照？

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
