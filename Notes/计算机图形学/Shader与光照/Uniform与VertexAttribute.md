---
title: Uniform 与 Vertex Attribute
description: MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform 传？理解两种 GPU 数据通道的 API 调用、数量限制、std140 布局，以及引擎材质系统的参数入口。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - glsl
  - uniform
  - vertex-attribute
  - std140
  - material
aliases:
  - Uniform vs Attribute
  - 着色器数据通道
  - 材质参数上传
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL数据流与着色器编译]] — 你已经理解 `in`/`out`/`uniform` 的基本数据流和 Shader 编译链接流程
> - [[Notes/计算机图形学/顶点数据与索引/VAO与顶点属性配置|VAO与顶点属性配置]] — 你已经能把顶点属性从 VBO 配置进顶点着色器
>
> **本模块增量**：你能正确决策「这个数据走 Attribute 还是 Uniform」，能用 `glUniform*` 把 MVP/光源/材质参数传进 Shader，能避开 Uniform 数量限制和 `std140` 对齐陷阱。
>
> **下一步**：把本笔记的数据通道知识和 [[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|GPU上的Blinn-Phong光照]] 的公式结合起来，写出真正可运行的光照 Shader；之后进入 [[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理系统]] 给模型贴上纹理。

---

# Uniform 与 Vertex Attribute

## 问题 0：MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？

在 [[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL数据流与着色器编译]] 里，你已经见过一个带 MVP 变换的彩色三角形：

```glsl
uniform mat4 uMVP;
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vColor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
```

这里同时出现了两种数据输入：

- `aPos` / `aColor`：通过 VAO/VBO 传进来的 **Vertex Attribute**
- `uMVP`：通过 `glUniformMatrix4fv` 传进来的 **Uniform**

但为什么 MVP 矩阵不走 Attribute？它明明也是 Shader 的输入啊。反过来说，顶点颜色为什么不用 Uniform？如果场景里所有顶点共享同一个光源位置，那光源位置又该走哪条通道？

这些问题的答案不取决于「这个数据叫什么」，而取决于**它在每个 Draw Call 内变化得多频繁**。

---

## 问题 1：Vertex Attribute 是什么？——从 VBO 流进顶点着色器的「每顶点数据」

在 [[Notes/计算机图形学/顶点数据与索引/VAO与顶点属性配置|VAO与顶点属性配置]] 里你已经理解：Vertex Attribute 描述的是**每个顶点各自有一份**的数据。

一个立方体有 8 个顶点，每个顶点有自己的位置、法线、UV、颜色。这些数据存在 VBO 里，通过 VAO 告诉 GPU「属性 0 是位置，属性 1 是法线，属性 2 是 UV」。

```cpp
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

// 8 个顶点 → VBO 里有 8 份 pos/normal/uv
Vertex cube[8] = { ... };
```

顶点着色器执行时，GPU 会自动把第 `i` 个顶点的属性喂给第 `i` 次着色器调用。这是 Attribute 的核心特征：**数据频率 = 每个顶点一次**。

> 你可以把 Attribute 想象成「每个学生（顶点）都有自己的考号、姓名、座位号」。这些字段随着学生不同而不同，必须跟着学生名单（VBO）一起提交。

---

## 问题 2：如果我把 MVP 矩阵也塞进 VBO，会怎样？

假设你还没听说过 Uniform。你只知道「数据要从 CPU 到 GPU，就得用 VBO + Attribute」。那 MVP 矩阵怎么传？

### 最 naive 的方案：每个顶点都存一份 MVP 矩阵

```cpp
struct VertexWithMVP {
    float pos[3];
    float mvp[16];   // 每个顶点都存一份 4×4 矩阵
};

VertexWithMVP vertices[3] = {
    { {-0.5f, -0.5f, 0.0f}, mvp },
    { { 0.5f, -0.5f, 0.0f}, mvp },
    { { 0.0f,  0.5f, 0.0f}, mvp },
};
```

顶点着色器里：

```glsl
layout (location = 0) in vec3 aPos;
layout (location = 1) in mat4 aMVP;  // 每个顶点一个矩阵

void main() {
    gl_Position = aMVP * vec4(aPos, 1.0);
}
```

**立刻发现的问题**：

1. **显存爆炸**：一个 `mat4` 是 16 个 float = 64 字节。三角形 3 个顶点就要存 3 份，共 192 字节；一个 10000 顶点的模型就要浪费 640 KB 显存，而且这些数据完全一样。
2. **更新困难**：物体每帧旋转一点，MVP 矩阵就要变。你得重写整个 VBO，把 10000 份矩阵全部更新一遍——其中 9999 份都是重复劳动。
3. **概念错位**：MVP 矩阵描述的是「这个物体相对于相机的变换」，不是「这个顶点的属性」。把它塞到每个顶点里，就像给每个学生档案里都复印一份全校课表。

这个方案能跑，但它把「每个物体一份的数据」硬塞进「每个顶点一份」的通道里。我们需要一条新的通道：专门传「在一次 Draw Call 内不变」的数据。

这就是 **Uniform**。

---

## 问题 3：Uniform 是什么？——Per-Draw-Call 的全局只读常量

Uniform 是 GLSL 里的一种全局变量，它的值在**一次 Draw Call 执行期间对所有顶点和片段都相同**。

```glsl
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
```

CPU 端通过 `glUniform*` 系列函数设置它：

```cpp
// 查询 Uniform 在 Program 中的位置
GLint modelLoc = glGetUniformLocation(program, "uModel");
GLint viewLoc  = glGetUniformLocation(program, "uView");
GLint projLoc  = glGetUniformLocation(program, "uProjection");
GLint lightLoc = glGetUniformLocation(program, "uLightPos");

// 在绘制前设置
glUseProgram(program);
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
glUniformMatrix4fv(viewLoc,  1, GL_FALSE, glm::value_ptr(view));
glUniformMatrix4fv(projLoc,  1, GL_FALSE, glm::value_ptr(projection));
glUniform3f(lightLoc, 1.0f, 2.0f, 3.0f);
```

> 你可以把 Uniform 想象成「教室里的投影仪内容」。所有学生（顶点/片段）在同一节课（Draw Call）里看到的是同一张幻灯片。老师（CPU）可以在下一节课（下一次 Draw Call）换一张，但上课时不会给每个学生单独发一份。

Uniform 和 Attribute 的最核心区别只有一条：**数据变化频率不同**。

| 维度 | Vertex Attribute | Uniform |
|------|------------------|---------|
| 数据频率 | 每个顶点不同 | 每个 Draw Call 内恒定 |
| 数据来源 | VBO（通过 VAO 配置） | CPU 通过 `glUniform*` / UBO 设置 |
| 可访问阶段 | 顶点着色器 | 顶点/片段着色器都可读 |
| 典型用途 | 位置、法线、UV、顶点颜色 | MVP 矩阵、光源、相机、材质参数 |
| 数量限制 | `GL_MAX_VERTEX_ATTRIBS`（通常 16） | `GL_MAX_UNIFORM_COMPONENTS` / locations（平台相关） |

> 这张表只用于复习。如果你还没理解两者的差异，不要靠表格入门，要回到上面的问题链里想：为什么 MVP 不能走 Attribute？

---

## 问题 4：决策规则——这个数据该走哪条通道？

判断方法不是看数据类型，而是看：**在一次 Draw Call 中，这个值是否对所有顶点/片段都一样？**

### 走 Attribute 的场景

- 顶点位置：每个顶点位置不同
- 顶点法线：每个顶点法线不同
- UV 坐标：每个顶点 UV 不同
- 顶点颜色：每个顶点颜色不同
- 骨骼权重：每个顶点权重不同

### 走 Uniform 的场景

- MVP/Model/View/Projection 矩阵：同一物体同一帧内共享
- 光源位置、颜色、强度：同一帧内所有像素共享
- 相机位置：同一帧内所有像素共享
- 材质参数（基础色、粗糙度、金属度）：同一个材质实例共享
- 时间、全局开关、后处理参数：一帧或一个 Pass 内共享

### 一个容易混淆的例子：每个实例颜色不同

假设你有 100 个立方体，每个立方体颜色不同。颜色应该走 Attribute 还是 Uniform？

答案是：**走 Uniform，因为颜色是「每个物体」变化的，不是「每个顶点」变化的**。

```cpp
for (const auto& obj : objects) {
    glUniform3f(colorLoc, obj.color.r, obj.color.g, obj.color.b);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}
```

每个 Draw Call 设置一次颜色，比给 36 个顶点各存一份颜色高效得多。只有当立方体本身需要渐变（比如每个顶点颜色不同）时，才应该走 Attribute。

---

## 问题 5：Uniform 太多了怎么办？——数量限制与 UBO

单个 Shader 能用的 Uniform 不是无限的。OpenGL 提供了几个查询指标：

```cpp
GLint maxVertexUniformComponents;
GLint maxFragmentUniformComponents;
GLint maxUniformLocations;

glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxVertexUniformComponents);
glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxFragmentUniformComponents);
glGetIntegerv(GL_MAX_UNIFORM_LOCATIONS, &maxUniformLocations);
```

- `GL_MAX_VERTEX_UNIFORM_COMPONENTS`：顶点着色器最多能有多少个 float 分量。OpenGL 4.6 要求至少 1024 个，即约 64 个 `vec4`。
- `GL_MAX_FRAGMENT_UNIFORM_COMPONENTS`：片段着色器类似，通常也是 1024。
- `GL_MAX_UNIFORM_LOCATIONS`：所有 Shader 阶段加起来能有多少个 Active Uniform 位置。

对于学习和小型项目，几十个 Uniform 完全够用。但当场景里有大量光源、大量材质参数时，每个 Draw Call 都 `glUniform*` 几十次会变成 CPU 开销。更麻烦的是：Uniform 是跟着 Program 的状态，频繁切换 Program 时 Uniform 要重新设置。

### 改进：Uniform Buffer Object（UBO）

UBO 把一组 Uniform 打包到一个 Buffer 对象里，像 VBO 一样上传到 GPU，然后一次性绑定到某个 binding point。

```glsl
layout(std140, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
};

layout(std140, binding = 1) uniform PerObject {
    mat4 model;
    vec4 baseColor;
};
```

```cpp
GLuint ubo;
glGenBuffers(1, &ubo);
glBindBuffer(GL_UNIFORM_BUFFER, ubo);
glBufferData(GL_UNIFORM_BUFFER, sizeof(PerObjectData), &perObjectData, GL_STATIC_DRAW);

// 绑定到 binding = 1
glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
```

UBO 的好处：

1. **减少 API 调用**：一次 `glBufferSubData` 更新整个结构体，代替多次 `glUniform*`。
2. **跨 Program 共享**：多个 Shader 都可以从同一个 binding point 读数据。
3. **适合按更新频率分层**：每帧更新一次的 Camera 参数放 binding 0，每物体更新的放 binding 1。

> **诚实边界**：UBO 不是免费的。它要求 CPU 端按照 `std140` 布局精确打包数据，对齐规则非常严格。理解不对齐，数据就会错乱。

---

## 问题 6：std140 布局——UBO 的「对齐强迫症」

`std140` 是 UBO 最常用的布局限定符。它强制规定了结构体成员在内存中的对齐方式，确保不同 OpenGL 实现都按同样的规则排布。

规则的核心只有一句话：**每个成员的起始地址必须是它「基准对齐量」的整数倍**。

| 类型 | 基准对齐 | 实际占用大小 | 注意 |
|------|---------|-------------|------|
| `float` / `int` / `bool` | 4 字节 | 4 字节 | 标量按 4 字节对齐 |
| `vec2` | 8 字节 | 8 字节 | |
| `vec3` | 16 字节 | 12 字节 | **大坑**：`vec3` 必须从 16 字节边界开始，但它只占用 12 字节 |
| `vec4` | 16 字节 | 16 字节 | |
| `mat4` | 16 字节 | 64 字节 | 视为 4 个 `vec4` 数组 |
| `float arr[N]` | 16 字节/元素 | N × 16 字节 | 每个 float 都占 16 字节！ |
| `struct` | 16 字节 | 填充到 16 字节倍数 | |

> 表格只用于总结。首次理解时请跟随下面的具体例子。

### 最容易错的地方：`vec3` 后面能不能紧跟 `float`？

能。因为 `vec3` 只占 12 字节，而 `float` 的对齐要求是 4 字节。如果 `vec3` 从 0 开始，它占用 0~11；下一个 `float` 可以从 12 开始，正好满足 4 字节对齐。

```glsl
layout(std140, binding = 0) uniform LightParams {
    vec3 lightPos;      // offset = 0,  size = 12
    float intensity;    // offset = 12, size = 4
    vec3 lightColor;    // offset = 16, size = 12
    float _unused;      // offset = 28, size = 4
    mat4 lightMatrix;   // offset = 32, size = 64
}; // 总大小 96 字节
```

这个布局在 C++ 端可以很自然地对应：

```cpp
struct alignas(16) LightParams {
    glm::vec3 lightPos;      // 0-11
    float intensity;         // 12-15
    glm::vec3 lightColor;    // 16-27
    float _unused;           // 28-31
    glm::mat4 lightMatrix;   // 32-95
}; // 96 bytes，和 std140 一致
```

> 关键认知：`vec3` 的「对齐量」是 16 字节，但「大小」是 12 字节。对齐量决定它从哪开始，大小决定它占多少空间。不要把两者混为一谈。

### 真正会踩坑的地方：数组

看下面这个例子：

```glsl
layout(std140, binding = 0) uniform Weights {
    float weights[4];  // offset = 0, 每个元素占 16 字节，总大小 64 字节
};
```

如果你按 C++ 直觉上传：

```cpp
struct Weights {
    float weights[4];  // 16 字节
}; // 16 字节
```

GPU 会期望 64 字节，而你只上传了 16 字节。结果 `weights[1]` 读到了你根本没传的数据，画面会出各种「幽灵 bug」。

再比如 LearnOpenGL 的经典例子：

```glsl
layout(std140) uniform ExampleBlock {
    float value;      // offset = 0
    vec3 vector;      // offset = 16（vec3 对齐到 16 字节边界）
    mat4 matrix;      // offset = 32
    float values[3];  // offset = 96，每个元素占 16 字节
}; // 总大小 144 字节
```

C++ 端如果写成：

```cpp
struct ExampleBlock {
    float value;         // 0-3
    glm::vec3 vector;    // 16-27（前面自动填充 12 字节）
    glm::mat4 matrix;    // 32-95
    float values[3];     // 96-107
}; // 108 字节，对齐到 16 后是 112 字节
```

GPU 按 144 字节解析，CPU 只给 112 字节，`values` 数组开始错位。

### 推荐的 C++ 对齐写法

最安全的方法是让 C++ 结构体完全匹配 GLSL 的 `std140` 布局：

```cpp
struct alignas(16) LightParams {
    glm::vec3 lightPos;      // offset 0
    float intensity;         // offset 12
    glm::vec3 lightColor;    // offset 16
    float _unused;           // offset 28，把 lightColor 补到 16 字节边界
    glm::mat4 lightMatrix;   // offset 32
}; // 96 bytes，和 std140 一致
```

更省心的做法：在 GLSL 里把 `vec3` 后面紧跟的字段凑成一个 `vec4`，或者避免使用标量/向量数组：

```glsl
layout(std140, binding = 0) uniform LightParams {
    vec4 lightPosAndIntensity;  // xyz = lightPos, w = intensity
    vec4 lightColorAndPadding;  // rgb = lightColor
    mat4 lightMatrix;
};
```

这样 CPU 端用一个 `glm::vec4` 存就非常自然，没有对齐坑。

> **有效失败**：我曾经把 `bool useColor` 单独作为一个字段放进 UBO，以为它只占 1 字节。结果在 `std140` 中，`bool` 的基准对齐量是 4 字节、占用大小也是 4 字节。如果 C++ 端用 `bool`（1 字节）上传，而 GLSL 端按 4 字节读取，后续所有字段都会错位。

---

## 问题 7：Uniform 的状态跟着谁走？——状态机视角

OpenGL 是状态机，Uniform 是 Program 对象的状态，不是 VAO 的状态。

```cpp
// ❌ 错误：先设置 Uniform，再 Use Program
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
glUseProgram(program);

// ✅ 正确：先 Use Program，再设置 Uniform
glUseProgram(program);
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
```

`glUniform*` 修改的是「当前激活 Program」的 Uniform 值。如果你没 `glUseProgram`，它要么无效，要么修改了上一个 Program 的值。

另一个状态机细节：Uniform 值不会因为重新绑定 VAO 而丢失，也不会因为切换 VBO 而重置。它只在你切换 Program 或主动 `glUniform*` 时改变。

```
状态变化图：

CPU 侧 C++ 数据
       │
       │ glUniformMatrix4fv(modelLoc, ...)
       ▼
┌─────────────────┐
│  当前 Program   │  ← Uniform 值存在这里
│  (uModel, uView, uLightPos...)
└────────┬────────┘
         │ glUseProgram(program)
         ▼
    GPU 执行 Shader
         │
         ├─ 顶点着色器读取 Uniform + Attribute
         │
         └─ 片段着色器读取 Uniform + in/varying
```

---

## 完整示例：用 Uniform 传 MVP 和光源，用 Attribute 传顶点数据

下面这个例子把本节所有概念串起来：

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

struct Vertex {
    float pos[3];
    float normal[3];
};

const char* vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uLightPos;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vLightDir;

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vLightDir = normalize(uLightPos - vFragPos);
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
)";

const char* fsSrc = R"(
#version 330 core
in vec3 vNormal;
in vec3 vLightDir;

uniform vec3 uObjectColor;
uniform vec3 uLightColor;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(vNormal);
    float diff = max(dot(norm, vLightDir), 0.0);
    vec3 ambient = 0.1 * uLightColor;
    vec3 diffuse = diff * uLightColor;
    FragColor = vec4((ambient + diffuse) * uObjectColor, 1.0);
}
)";

GLuint compileShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "Shader 编译失败:\n" << log << std::endl;
    }
    return s;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Uniform vs Attribute", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // 立方体顶点：位置 + 法线（简化版，每个面单独顶点，不索引）
    Vertex vertices[] = {
        // 前面
        {{-0.5f,-0.5f, 0.5f}, {0,0,1}}, {{ 0.5f,-0.5f, 0.5f}, {0,0,1}}, {{ 0.5f, 0.5f, 0.5f}, {0,0,1}},
        {{-0.5f,-0.5f, 0.5f}, {0,0,1}}, {{ 0.5f, 0.5f, 0.5f}, {0,0,1}}, {{-0.5f, 0.5f, 0.5f}, {0,0,1}},
        // 后面
        {{ 0.5f,-0.5f,-0.5f}, {0,0,-1}}, {{-0.5f,-0.5f,-0.5f}, {0,0,-1}}, {{-0.5f, 0.5f,-0.5f}, {0,0,-1}},
        {{ 0.5f,-0.5f,-0.5f}, {0,0,-1}}, {{-0.5f, 0.5f,-0.5f}, {0,0,-1}}, {{ 0.5f, 0.5f,-0.5f}, {0,0,-1}},
        // 右面
        {{ 0.5f,-0.5f, 0.5f}, {1,0,0}}, {{ 0.5f,-0.5f,-0.5f}, {1,0,0}}, {{ 0.5f, 0.5f,-0.5f}, {1,0,0}},
        {{ 0.5f,-0.5f, 0.5f}, {1,0,0}}, {{ 0.5f, 0.5f,-0.5f}, {1,0,0}}, {{ 0.5f, 0.5f, 0.5f}, {1,0,0}},
        // 左面
        {{-0.5f,-0.5f,-0.5f}, {-1,0,0}}, {{-0.5f,-0.5f, 0.5f}, {-1,0,0}}, {{-0.5f, 0.5f, 0.5f}, {-1,0,0}},
        {{-0.5f,-0.5f,-0.5f}, {-1,0,0}}, {{-0.5f, 0.5f, 0.5f}, {-1,0,0}}, {{-0.5f, 0.5f,-0.5f}, {-1,0,0}},
        // 顶面
        {{-0.5f, 0.5f, 0.5f}, {0,1,0}}, {{ 0.5f, 0.5f, 0.5f}, {0,1,0}}, {{ 0.5f, 0.5f,-0.5f}, {0,1,0}},
        {{-0.5f, 0.5f, 0.5f}, {0,1,0}}, {{ 0.5f, 0.5f,-0.5f}, {0,1,0}}, {{-0.5f, 0.5f,-0.5f}, {0,1,0}},
        // 底面
        {{-0.5f,-0.5f,-0.5f}, {0,-1,0}}, {{ 0.5f,-0.5f,-0.5f}, {0,-1,0}}, {{ 0.5f,-0.5f, 0.5f}, {0,-1,0}},
        {{-0.5f,-0.5f,-0.5f}, {0,-1,0}}, {{ 0.5f,-0.5f, 0.5f}, {0,-1,0}}, {{-0.5f,-0.5f, 0.5f}, {0,-1,0}},
    };

    GLuint vbo, vao;
    glGenBuffers(1, &vbo);
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLuint program = glCreateProgram();
    glAttachShader(program, compileShader(vsSrc, GL_VERTEX_SHADER));
    glAttachShader(program, compileShader(fsSrc, GL_FRAGMENT_SHADER));
    glLinkProgram(program);

    // 查询 Uniform 位置
    GLint modelLoc = glGetUniformLocation(program, "uModel");
    GLint viewLoc  = glGetUniformLocation(program, "uView");
    GLint projLoc  = glGetUniformLocation(program, "uProjection");
    GLint lightPosLoc = glGetUniformLocation(program, "uLightPos");
    GLint objColorLoc = glGetUniformLocation(program, "uObjectColor");
    GLint lightColorLoc = glGetUniformLocation(program, "uLightColor");

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        // Uniform：每帧更新的相机和光源
        glm::mat4 view = glm::lookAt(glm::vec3(2,2,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f/600.0f, 0.1f, 100.0f);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(lightPosLoc, 2.0f, 3.0f, 2.0f);
        glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);

        // Uniform：每个物体不同的颜色和变换
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0,1,0));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(objColorLoc, 0.8f, 0.5f, 0.2f);

        // Attribute：顶点数据通过 VAO/VBO
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}
```

这个例子展示了数据流的分层：

```
CPU 内存
   │
   ├─── Attribute 通道 ───► VBO ───► VAO ───► 顶点着色器 (aPos, aNormal)
   │
   └─── Uniform 通道 ─────► glUniform* ───► Program ───► 顶点/片段着色器
                                              │
                                              ├── uModel / uView / uProjection
                                              ├── uLightPos / uLightColor
                                              └── uObjectColor
```

---

## 常见陷阱

### 陷阱 1：`glGetUniformLocation` 返回 -1

```cpp
GLint loc = glGetUniformLocation(program, "uModel");
// loc == -1
```

可能原因：

1. **拼写错误**：Shader 里是 `uModel`，你查询 `uModle`。
2. **被编译器优化掉**：如果 Shader 里没有实际使用 `uModel`，编译器会把它优化掉，查询不到位置。
3. **Program 没有成功链接**：必须先检查 `GL_LINK_STATUS`。

### 陷阱 2：缓存 Uniform 位置后重新链接 Program

```cpp
GLint loc = glGetUniformLocation(program, "uModel");
// ... 修改 Shader 源码后重新编译链接 ...
glUniformMatrix4fv(loc, ...);  // ❌ loc 可能已失效
```

每次 `glLinkProgram` 后，Uniform 位置可能变化。要么重新查询，要么在 GLSL 中用 `layout(location = 0) uniform mat4 uModel;` 显式固定。

### 陷阱 3：在 `glUseProgram` 之前设置 Uniform

```cpp
glUniform3f(lightPosLoc, ...);  // 此时没有激活 Program
glUseProgram(program);
```

结果：Uniform 设置到了错误的 Program 上，或者根本没生效。

### 陷阱 4：std140 对齐导致数据错位

最隐蔽的 bug。CPU 端用一个自然对齐的结构体上传，GPU 按 std140 解析，结果所有字段都偏移了。排查方法：用 `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` 和手动算偏移，或者在 GLSL 中显式用 `vec4` 打包 `vec3 + float`。

### 陷阱 5：以为 Uniform 可以跨 Program 共享

```cpp
glUseProgram(programA);
glUniform3f(lightPosLoc, 1, 2, 3);
glUseProgram(programB);
// programB 里 uLightPos 还是旧值，不会自动继承 programA 的值
```

Uniform 是 Program 的私有状态。多个 Program 要共享光源参数，请用 UBO。

---

## 与现代 API 的对照

我们在解决的是「**如何把 CPU 数据按正确频率传给 GPU Shader**」这个具体问题。

| 概念 | OpenGL (GLSL) | Vulkan (SPIR-V) | D3D12 (HLSL) |
|------|---------------|-----------------|--------------|
| 每顶点数据 | Vertex Attribute + VAO/VBO | `VkVertexInputAttributeDescription` + Vertex Buffer | `D3D12_INPUT_ELEMENT_DESC` + Vertex Buffer |
| 每 Draw Call 常量 | `uniform` / `glUniform*` | Push Constants / Uniform Buffer + DescriptorSet | `cbuffer` / Root Constants / Constant Buffer |
| 大量常量组织 | UBO (`layout(std140)`) | `VkBuffer` + DescriptorSet | `ID3D12Resource` + Root Signature |
| 状态绑定 | `glUseProgram` 携带 Uniform 状态 | Pipeline Layout 定义 Descriptor 布局 | PSO + Root Signature |

> **个人项目推荐**：学习和小型项目用 OpenGL 的 `glUniform*` 完全够用。当 Uniform 数量多、切换频繁时，迁移到 UBO。向现代 API 迁移时，mentally map 为："`glUniform*` ≈ Push Constants / Root Constants；UBO ≈ Constant Buffer / Uniform Buffer"。

---

## 与 SelfGameEngine 的关系

### Uniform 就是引擎材质参数的底层机制

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被拆成 Template-Asset-Instance 三层。Asset 层定义的颜色、粗糙度、贴图引用，最终都要在 Instance 层变成 GPU 能读的数据。

这些数据进入 GPU 的方式，正是本笔记讲的 Uniform / UBO / Binding。

```cpp
// 引擎上层：材质实例定义参数
struct StandardMaterial {
    Vec4 baseColor;
    float roughness;
    float metallic;
    Handle<Texture> albedoMap;
};

// RHI 层：把参数组织成 GPU 绑定
// OpenGL 后端可能用 glUniform* 或 UBO
// Vulkan 后端可能用 Push Constants + DescriptorSet
```

### 更新频率分层

在 [[Notes/SelfGameEngine/渲染管线与画面/材质参数绑定与GPU上传|材质参数绑定与GPU上传]] 里，你会看到工业引擎如何把参数按更新频率分层：

- **每帧一次**：View、Projection、Camera、全局光源 → UBO binding 0
- **每材质一次**：baseColor、roughness、贴图 → UBO / BindGroup binding 1
- **每物体一次**：Model 矩阵、自定义实例数据 → Push Constant / Uniform binding 2

这种分层的根源就是「不同数据有不同的变化频率」，和本笔记「Attribute vs Uniform 按频率选通道」的直觉完全一致。

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| 数据频率判断 | 每顶点变化 → Attribute；每 Draw Call 恒定 → Uniform |
| Uniform 位置查询 | 链接成功后查询，使用前检查是否为 -1 |
| Uniform 设置顺序 | 必须先 `glUseProgram(program)`，再 `glUniform*` |
| Uniform 位置缓存 | 重新链接 Program 后必须重新查询位置 |
| UBO 对齐 | CPU 端结构体必须按 `std140` 布局手动对齐 |
| `vec3` 处理 | `vec3` 对齐量是 16 字节、大小是 12 字节；为减少心智负担，推荐把 `vec3 + 标量` 显式打包成 `vec4` |
| 跨 Program 共享 | 用 UBO 而不是靠 `glUniform*` 重复设置 |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| Attribute 与 Uniform 的决策规则 | 多纹理材质的纹理单元绑定 |
| `glUniform*` 的完整 API | 纹理对象创建与采样 |
| UBO 与 `std140` 对齐 | 引擎中的 BindGroup / DescriptorSet 抽象 |
| Uniform 的状态机模型 | 现代 API 的 Push Constants 和 Bindless |

> **下一步**：把本笔记的 Uniform/Attribute 通道知识和 [[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|GPU上的Blinn-Phong光照]] 的完整公式结合起来，写出一个带环境光 + 漫反射 + 高光的可运行 Shader。然后进入 [[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理系统]]，让模型不再只有纯色。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
