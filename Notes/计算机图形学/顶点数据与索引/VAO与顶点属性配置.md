---
title: VAO与顶点属性配置
description: VBO把顶点数据存到了GPU，但GPU怎么知道“前3个float是位置，后2个是UV”？从每次绘制都重复配置的灾难出发，理解VAO如何打包顶点属性状态。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - vao
  - vertex-layout
  - gpu-memory
aliases:
  - Vertex Array Object
  - VAO
  - 顶点数组对象
  - 顶点属性配置
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：[[Notes/计算机图形学/顶点数据与索引/从顶点数组到VBO|从顶点数组到VBO]] — 你已经能把顶点数据上传到VBO并常驻显存
>
> **本模块增量**：你能解释VAO解决了什么问题，能用`glVertexAttribPointer`配置位置/颜色/UV多个属性，能画出带UV的三角形，并理解VAO里“存了什么、没存什么”。
>
> **下一步**：[[Notes/计算机图形学/顶点数据与索引/索引缓冲EBO|索引缓冲EBO]] — 立方体只有8个顶点，但12个三角形需要36个顶点。怎么复用？

---

# VAO 与顶点属性配置

## 问题 0：VBO 里只是一堆字节，GPU 怎么知道它们代表什么？

在 [[Notes/计算机图形学/顶点数据与索引/从顶点数组到VBO|从顶点数组到VBO]] 里，你已经把三角形的顶点数据传到了 GPU 显存：

```cpp
struct Vertex {
    float x, y, z;
    float r, g, b;
};

std::vector<Vertex> triangle = {
    {-0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f},
    { 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f},
    { 0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f}
};
```

上传到 VBO 后，GPU 看到的不是“三个顶点，每个顶点有位置和颜色”，而是一段连续的字节：

```
显存中的 VBO（GPU 视角）：
┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
│-0.5│-0.5│ 0.0│ 1.0│ 0.0│ 0.0│ 0.5f│-0.5│ 0.0│ 0.0│ 1.0│ 0.0│ ...
└────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
  ←──────── 顶点 0 ────────→  ←──────── 顶点 1 ────────→
```

GPU 不知道这些字节代表什么。它需要你告诉它：

- **属性 0（位置）**：从每个顶点的第 0 字节开始，读 3 个 `float`
- **属性 1（颜色）**：从每个顶点的第 12 字节开始，读 3 个 `float`
- **顶点间距（stride）**：每个顶点占 24 字节（6 个 float × 4 字节）

这就是 **顶点属性配置** 要解决的问题。

---

## 问题 1：最 naive 的方案——每次绘制前都重新配置

既然 GPU 不知道数据格式，那我们每次调用 `glDrawArrays` 之前都告诉它一遍：

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>

struct Vertex { float x, y, z, r, g, b; };

void drawTriangle(GLuint vbo, GLuint shaderProgram) {
    glUseProgram(shaderProgram);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // 每次绘制都重新配置属性 0：位置
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);

    // 每次绘制都重新配置属性 1：颜色
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, 3);
}
```

**立刻发现的问题**：

1. **重复劳动**：如果场景里有 100 个物体，这套配置就要重复 100 次。每个物体可能只有顶点数据不同，但解析格式完全一样。
2. **容易遗漏**：忘记 `glEnableVertexAttribArray(1)`，颜色属性就不会被读取，画面变成单色；忘记 `glBindBuffer(GL_ARRAY_BUFFER, vbo)`，`glVertexAttribPointer` 就不知道数据从哪来。
3. **stride 一算错就全错**：如果 `sizeof(Vertex)` 写成了 `3 * sizeof(float)`，GPU 会以为每个顶点只有位置，颜色会读到下一个顶点的位置数据。

> 这就像是：你每次进厨房都要重新告诉别人“盐在左边第二个柜子、糖在右边第三个柜子”。明明厨房布局没变，却每次都要复述一遍。

---

## 问题 2：能不能把属性配置也“打包”起来，配置一次重复使用？

当然可以。这就是 **VAO（Vertex Array Object，顶点数组对象）** 的核心思想：

> **把顶点属性配置打包成一个状态对象，配置一次，绘制时只要绑定这个对象。**

```cpp
// 1. 生成 VAO
GLuint vao;
glGenVertexArrays(1, &vao);

// 2. 绑定 VAO：后续所有顶点属性配置都会记录到这个 VAO 中
glBindVertexArray(vao);

// 3. 绑定 VBO（VAO 会通过 glVertexAttribPointer 隐式记录这个关联）
glBindBuffer(GL_ARRAY_BUFFER, vbo);

// 4. 配置属性 0：位置
//    index=0, size=3, type=GL_FLOAT, normalized=GL_FALSE,
//    stride=sizeof(Vertex), offset=(void*)0
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
glEnableVertexAttribArray(0);

// 5. 配置属性 1：颜色
//    offset = 3个float = 12字节
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
glEnableVertexAttribArray(1);

// 6. 解绑 VAO（可选但推荐，防止后续操作污染配置）
glBindVertexArray(0);
```

**绘制时只需要两行**：

```cpp
glUseProgram(shaderProgram);
glBindVertexArray(vao);  // 自动恢复所有属性配置和 VBO 关联
glDrawArrays(GL_TRIANGLES, 0, 3);
```

---

## 问题 3：`glVertexAttribPointer` 的每个参数到底在说什么？

这是 OpenGL 里最容易写错、也最值得逐字理解的函数之一：

```cpp
void glVertexAttribPointer(
    GLuint index,      // 顶点属性索引，对应 shader 里的 layout(location = index)
    GLint size,        // 每个属性有几个分量（1/2/3/4）
    GLenum type,       // 分量数据类型（GL_FLOAT / GL_UNSIGNED_BYTE 等）
    GLboolean normalized, // 整数类型是否归一化到 [-1,1] 或 [0,1]
    GLsizei stride,    // 从一个顶点到下一个顶点，相同属性之间隔多少字节
    const void* offset // 该属性在当前顶点内的字节偏移量
);
```

### 参数逐行解释

| 参数 | 作用 | 常见错误 |
|------|------|---------|
| `index` | 顶点属性位置索引 | 必须和 GLSL 中 `layout(location = N) in ...` 的 `N` 一致 |
| `size` | 每个属性有几个分量 | 位置是 3（xyz），UV 是 2（st），颜色 RGBA 是 4 |
| `type` | 分量数据类型 | 位置通常是 `GL_FLOAT`；颜色如果存为 4 字节 RGBA，可能是 `GL_UNSIGNED_BYTE` |
| `normalized` | 整数类型是否需要归一化 | 对 `GL_FLOAT` 必须为 `GL_FALSE`；对颜色字节通常设为 `GL_TRUE` |
| `stride` | 相邻两个顶点之间，该属性相隔多少字节 | **最容易错**。不是“该属性自身占多少字节”，而是“跨过一个顶点的距离” |
| `offset` | 该属性在顶点结构体内的字节偏移 | 对位置通常是 0；对颜色用 `offsetof(Vertex, color)` 更清晰 |

### 为什么 `stride` 这么容易错？

`stride` 表示的是：**从第 i 个顶点的某个属性，到第 i+1 个顶点的同一个属性，中间隔多少字节。**

对于交织布局（interleaved），一个顶点包含位置 + 颜色 + UV：

```cpp
struct Vertex {
    float pos[3];   // 12 bytes
    float color[3]; // 12 bytes
    float uv[2];    // 8 bytes
};                  // 总共 32 bytes
```

那么：
- 位置的 stride = 32（跳过整个顶点）
- 颜色的 stride = 32（跳过整个顶点）
- UV 的 stride = 32（跳过整个顶点）

**常见误解**：有人以为 stride 是“这个属性占多少字节”，所以写成 `3 * sizeof(float)`。这是错的。`stride = 0` 在 OpenGL 中有特殊含义——它表示“该属性在数组中紧密排列”，也就是 stride = size × sizeof(type)。只有当属性单独存放在一个数组里时，才能用 0。

---

## 问题 4：VAO 到底存了什么？不存什么？

这是理解 OpenGL 状态机的关键问题。

```
VAO 存储的内容：
┌─────────────────────────────────────┐
│ ✓ glEnableVertexAttribArray 的状态   │
│ ✓ glVertexAttribPointer 的配置       │
│ ✓ 每个属性关联的 VBO（通过 glVertexAttribPointer 隐式记录）│
│ ✓ 当前绑定的 GL_ELEMENT_ARRAY_BUFFER │
│                                     │
│ ✗ 不存顶点数据本身（数据在 VBO 里）   │
│ ✗ 不存 GL_ARRAY_BUFFER 的当前绑定（意外！）│
└─────────────────────────────────────┘
```

> ⚠️ **一个常见的坑**：很多人以为 `glBindBuffer(GL_ARRAY_BUFFER, vbo)` 的状态被 VAO 保存了。实际上 VAO **不保存** `GL_ARRAY_BUFFER` 的绑定。它保存的是 `glVertexAttribPointer` 中**隐含记录**的 VBO 关联——也就是说，VAO 记住的是“属性 0 的数据来自哪个 VBO”，而不是“当前 `GL_ARRAY_BUFFER` 绑定了谁”。
>
> 这意味着：解绑 VAO 后再绑回来，`GL_ARRAY_BUFFER` 可能是未定义的。但这没关系，因为绘制时只需要 VAO，不需要单独绑 VBO。

为什么 `GL_ARRAY_BUFFER` 不存，而 `GL_ELEMENT_ARRAY_BUFFER` 存？因为：

- `GL_ARRAY_BUFFER` 只是 `glVertexAttribPointer` 的**临时输入**。调用完成后，真正的关联被记录到属性状态里，全局绑定就没用了。
- `GL_ELEMENT_ARRAY_BUFFER` 没有对应的“格式配置函数”，所以它的绑定只能直接作为 VAO 状态保存。

---

## 问题 5：多个属性怎么组织？——三种典型布局

真实模型通常有位置、法线、UV、颜色、切线等多个属性。把它们存进 VBO 有三种常见方式。

### 布局 A：每个属性一个独立的 VBO（Planar）

```cpp
// 位置数组
float positions[] = { -0.5f,-0.5f,0.0f,  0.5f,-0.5f,0.0f,  0.0f,0.5f,0.0f };
// 颜色数组
float colors[]    = { 1.0f,0.0f,0.0f,  0.0f,1.0f,0.0f,  0.0f,0.0f,1.0f };
// UV 数组
float uvs[]       = { 0.0f,0.0f,  1.0f,0.0f,  0.5f,1.0f };
```

配置时：

```cpp
glBindVertexArray(vao);

// 位置 VBO
glBindBuffer(GL_ARRAY_BUFFER, vboPos);
glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
glEnableVertexAttribArray(0);

// 颜色 VBO
glBindBuffer(GL_ARRAY_BUFFER, vboColor);
glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
glEnableVertexAttribArray(1);

// UV VBO
glBindBuffer(GL_ARRAY_BUFFER, vboUV);
glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
glEnableVertexAttribArray(2);
```

**优点**：更新单个属性（比如只改颜色）不需要动其他属性。
**缺点**：VBO 数量多，缓存局部性较差，现代 GPU 通常更喜欢交织布局。

---

### 布局 B：所有属性顺序存放在一个 VBO（Sequential）

```cpp
float vertices[] = {
    // 所有位置先放
    -0.5f,-0.5f,0.0f,  0.5f,-0.5f,0.0f,  0.0f,0.5f,0.0f,
    // 所有颜色后放
    1.0f,0.0f,0.0f,  0.0f,1.0f,0.0f,  0.0f,0.0f,1.0f,
    // 所有 UV 最后放
    0.0f,0.0f,  1.0f,0.0f,  0.5f,1.0f
};
```

配置时：

```cpp
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

// 位置：从 0 开始，每 3 个 float 一个位置，但下一个位置隔 0 字节？不对。
// 实际上这种布局下，位置数组内部是紧密的，所以 stride = 0（表示紧密）
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
glEnableVertexAttribArray(0);

// 颜色：从第 9 个 float 开始
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)(9 * sizeof(float)));
glEnableVertexAttribArray(1);

// UV：从第 18 个 float 开始
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)(18 * sizeof(float)));
glEnableVertexAttribArray(2);
```

**优点**：只有一个 VBO，绑定切换少。
**缺点**：不同属性的长度不同，动态修改某一类属性时需要小心偏移计算；缓存局部性不如交织布局。

---

### 布局 C：交织布局（Interleaved，推荐）

```cpp
struct Vertex {
    float pos[3];   // 位置
    float color[3]; // 颜色
    float uv[2];    // UV
};

Vertex vertices[] = {
    { {-0.5f,-0.5f,0.0f}, {1.0f,0.0f,0.0f}, {0.0f,0.0f} },
    { { 0.5f,-0.5f,0.0f}, {0.0f,1.0f,0.0f}, {1.0f,0.0f} },
    { { 0.0f, 0.5f,0.0f}, {0.0f,0.0f,1.0f}, {0.5f,1.0f} }
};
```

配置时：

```cpp
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

// 位置：offset = 0，stride = sizeof(Vertex)
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
glEnableVertexAttribArray(0);

// 颜色：offset = offsetof(Vertex, color)，stride = sizeof(Vertex)
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
glEnableVertexAttribArray(1);

// UV：offset = offsetof(Vertex, uv)，stride = sizeof(Vertex)
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
glEnableVertexAttribArray(2);
```

**为什么推荐交织布局？**

- **缓存友好**：GPU 读取一个顶点时，位置和颜色等属性在物理上相邻，能命中同一条缓存行。
- **一个 VBO**：绑定切换少，代码清晰。
- **stride 一致**：所有属性共用 `sizeof(Vertex)`，不容易算错。

> 用 `offsetof(Vertex, field)` 而不是手算字节偏移，是避免错误的最佳实践。如果你后来给 `Vertex` 加了一个 `float padding` 或调整字段顺序，`offsetof` 会自动跟上。

---

## 问题 6：完整的最小示例——带 UV 的彩色三角形

下面是一个把 VAO、VBO、多属性交织布局串起来的完整示例。

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstddef>

struct Vertex {
    float pos[3];
    float color[3];
    float uv[2];
};

const char* vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aUV;

out vec3 vColor;
out vec2 vUV;

void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
    vUV = aUV;
}
)";

const char* fsSrc = R"(
#version 330 core
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;

void main() {
    // 暂时不用纹理，只用 UV 的棋盘格颜色验证数据是否正确
    float checker = step(0.5, fract(vUV.x * 4.0)) * step(0.5, fract(vUV.y * 4.0));
    FragColor = vec4(vColor * (0.8 + 0.2 * checker), 1.0);
}
)";

GLuint compileShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "VAO 多属性示例", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // ===== 1. 准备交织顶点数据 =====
    Vertex vertices[] = {
        { {-0.5f,-0.5f,0.0f}, {1.0f,0.0f,0.0f}, {0.0f,0.0f} },
        { { 0.5f,-0.5f,0.0f}, {0.0f,1.0f,0.0f}, {1.0f,0.0f} },
        { { 0.0f, 0.5f,0.0f}, {0.0f,0.0f,1.0f}, {0.5f,1.0f} }
    };

    // ===== 2. 创建 VBO 并上传数据 =====
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // ===== 3. 创建 VAO 并配置属性 =====
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // ===== 4. 编译 Shader =====
    GLuint program = glCreateProgram();
    glAttachShader(program, compileShader(vsSrc, GL_VERTEX_SHADER));
    glAttachShader(program, compileShader(fsSrc, GL_FRAGMENT_SHADER));
    glLinkProgram(program);

    // ===== 5. 渲染循环 =====
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);  // 一次绑定，恢复所有属性配置
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}
```

> 这个示例里，渲染循环只有三行绘制相关代码：`glUseProgram`、`glBindVertexArray`、`glDrawArrays`。所有属性配置都在初始化阶段被 VAO 打包好了。

---

## 状态变化图：VAO、VBO 与属性配置

```
初始化阶段：

┌─────────────┐     glBindBuffer      ┌─────────────┐
│   VBO #1    │ ◄──────────────────── │  CPU 数组   │
│  ┌─────┐    │     glBufferData      │  vertices[] │
│  │顶点 │    │                       └─────────────┘
│  │数据 │    │
│  └─────┘    │
└──────┬──────┘
       │
       │ glBindBuffer(GL_ARRAY_BUFFER, vbo)
       │
       ▼
┌─────────────────────────────────────────┐
│                 VAO #1                  │
│  ┌─────────────────────────────────┐    │
│  │ 属性 0：pos，  3×float，stride=32 │    │
│  │ 属性 1：color，3×float，stride=32 │    │
│  │ 属性 2：uv，   2×float，stride=32 │    │
│  │ 数据来源：VBO #1                  │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘

绘制阶段：

  glUseProgram(program)
       │
       ▼
  glBindVertexArray(vao)  ──► 自动恢复所有属性和 VBO 关联
       │
       ▼
  glDrawArrays(GL_TRIANGLES, 0, 3)
       │
       ▼
顶点着色器 ◄── 属性 0/1/2 ◄── VAO ◄── VBO #1
```

---

## 问题 7：VAO 真的“免费”吗？

不是。VAO 减少的是 **CPU 端的函数调用开销** 和 **开发者的心智负担**，但它引入了自己的成本：

| 维度 | 不用 VAO | 用 VAO |
|------|---------|--------|
| 绘制前配置 | 每次都要 `glVertexAttribPointer` + `glEnableVertexAttribArray` | 只需 `glBindVertexArray` |
| 切换物体 | 重复配置或手动管理状态 | 切换 VAO，但 VAO 切换本身也有驱动开销 |
| 内存占用 | 无额外对象 | 驱动为每个 VAO 维护状态表 |
| 多线程 | 仍然受限于 OpenGL 单 Context | 仍然受限于 OpenGL 单 Context |

> **诚实边界**：VAO 没有把 GPU 绘制变快，它只是把“绘制前的准备工作”从运行时挪到了初始化时。真正的顶点处理速度取决于顶点数量、Shader 复杂度和 GPU 带宽，而不是你有没有用 VAO。

在工业引擎中，VAO 级别的状态切换仍然太细粒度。现代引擎通常会在更高层做 **状态排序** 和 **合批**，把相同 VAO/Shader/材质的物体集中绘制，进一步减少状态切换。

---

## 问题 8：最常见的三个坑

### 坑 1：`GL_ARRAY_BUFFER` 绑定没存进 VAO

```cpp
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);  // 这只影响全局 GL_ARRAY_BUFFER 绑定
glBindVertexArray(0);

// 之后再绑回 vao，GL_ARRAY_BUFFER 可能是 0
// 但因为 glVertexAttribPointer 已经记录了 VBO 关联，绘制仍然正确
```

**结论**：配置 VAO 时必须先 `glBindBuffer(GL_ARRAY_BUFFER, vbo)`，再 `glVertexAttribPointer`。但绘制时不需要再绑 VBO。

---

### 坑 2：`stride` 写成属性自身大小

```cpp
// ❌ 错误：stride 不是位置属性自身的大小
 glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

// ✅ 正确：stride 是整个顶点的大小
 glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
```

如果你用了交织布局但把 stride 写成 `3 * sizeof(float)`，GPU 会以为顶点只包含位置，颜色和 UV 都会读到错误的数据。

---

### 坑 3：忘记 `glEnableVertexAttribArray`

```cpp
// ❌ 错误：只配置了格式，但没启用属性
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
// 忘记 glEnableVertexAttribArray(0);
```

未启用的属性不会被顶点着色器读取。如果 Shader 里 `layout(location = 0) in vec3 aPos;` 是启用的，但你没启用属性 0，绘制结果通常是黑屏或位置全 0。

---

## 与现代 API 的对照

我们在解决的是「**如何描述顶点数据的格式和来源**」这个问题。

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| 顶点格式描述 | `glVertexAttribPointer` | `VkVertexInputAttributeDescription` + `VkVertexInputBindingDescription` | `D3D12_INPUT_ELEMENT_DESC` |
| 状态打包对象 | VAO | `VkPipeline` 中的 vertex input state | `ID3D12PipelineState` |
| 绑定源缓冲 | 通过 `GL_ARRAY_BUFFER` 隐式关联 | `vkCmdBindVertexBuffers` 显式绑定 | `IASetVertexBuffers` |
| 索引缓冲 | `GL_ELEMENT_ARRAY_BUFFER`（存在 VAO 中） | `vkCmdBindIndexBuffer` | `IASetIndexBuffer` |

> **个人项目推荐**：学习阶段用 OpenGL 的 VAO 完全够用。向现代 API 迁移时，mentally map 为："VAO ≈ Pipeline 中的 Vertex Input State + 当前绑定的顶点缓冲"。

现代 API 之所以把顶点格式放进 Pipeline State Object（PSO），是因为它们希望把“顶点长什么样”和“顶点数据在哪里”完全分离：

- **Vertex Format**：Pipeline 创建时固定（类似 VAO 的配置）。
- **Vertex Buffer**：绘制前动态绑定（类似 VBO）。

这样你可以用同一个 Pipeline 渲染多个共享相同顶点格式的物体，只需要切换 VBO 和偏移量。

---

## 与 SelfGameEngine 的关系

VAO 就是引擎 RHI 层里 **VertexLayout** 概念的原型。

你在写引擎时不会直接调用 `glVertexAttribPointer`，而是会封装一个这样的结构：

```cpp
struct VertexAttributeDesc {
    uint32_t location;      // 对应 shader layout(location = N)
    uint32_t components;    // 1/2/3/4
    DataType type;          // Float / UByte / UShort ...
    uint32_t offset;        // 在顶点结构体内的字节偏移
};

struct VertexLayoutDesc {
    uint32_t stride;        // 每个顶点总字节数
    std::vector<VertexAttributeDesc> attributes;
};

// RHI 创建 VertexLayout（对应 OpenGL 的 VAO）
RHIVertexLayoutRef layout = device->createVertexLayout({
    .stride = sizeof(Vertex),
    .attributes = {
        {0, 3, DataType::Float, offsetof(Vertex, pos)},
        {1, 3, DataType::Float, offsetof(Vertex, color)},
        {2, 2, DataType::Float, offsetof(Vertex, uv)}
    }
});
```

OpenGL 后端会把 `VertexLayoutDesc` 翻译成一个 VAO；Vulkan 后端会把它翻译成一个 `VkPipelineVertexInputStateCreateInfo`。上层代码完全不需要知道底层是 VAO 还是 PSO。

> 这就是阶段八 [[Notes/计算机图形学/引擎渲染架构/为什么需要渲染抽象|为什么需要渲染抽象]] 要深入的内容。现在你只需要记住：VAO 是 RHI 中 VertexLayout 的 OpenGL 原语。

---

## 设计 checklist：什么时候用什么布局？

| 场景 | 推荐布局 | 原因 |
|------|---------|------|
| 静态模型（OBJ 加载） | 交织布局，一个 VBO | 缓存局部性最好，代码最清晰 |
| 骨骼动画，位置每帧更新但 UV 不变 | 位置单独一个 VBO，其他属性交织 | 只更新变化的属性，减少上传带宽 |
| 粒子系统，位置/颜色都每帧变 | 动态 VBO + 交织布局 | 一个 `glBufferSubData` 更新所有属性 |
| 调试线框/Gizmo | 简单位置 VBO 即可 | 不需要颜色/UV，最小化状态 |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| VAO 解决了重复配置属性指针的问题 | EBO 索引缓冲如何复用顶点 |
| `glVertexAttribPointer` 的 stride/offset | 真实模型（OBJ）的顶点布局去重与上传 |
| 三种顶点布局的 trade-off | Uniform 与 VertexAttribute 的数据通道差异 |
| VAO 存储/不存储的状态 | 多个物体的绘制状态管理 |

> **下一步**：[[Notes/计算机图形学/顶点数据与索引/索引缓冲EBO|索引缓冲EBO]] — 立方体只有 8 个顶点，但 12 个三角形需要 36 个顶点。怎么用 EBO 只存 8 个顶点 + 36 个索引来画？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
