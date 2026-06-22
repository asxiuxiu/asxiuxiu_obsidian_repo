---
title: GLSL数据流与着色器编译
description: 顶点着色器输出的颜色，片段着色器怎么拿到？中间发生了什么？理解in/out/uniform、光栅化插值、Shader编译链接的完整链路。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - glsl
  - shader
  - pipeline
  - interpolation
aliases:
  - GLSL数据流
  - 着色器编译链接
  - Shader数据流
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/GPU编程基础/第一个三角形|第一个三角形]] — 你已经写过顶点/片段着色器，编译链接过一个 Program
> - [[Notes/计算机图形学/顶点数据与索引/VAO与顶点属性配置|VAO与顶点属性配置]] — 你已经理解顶点属性怎么从 VBO 流进顶点着色器
> - [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] — 你已经理解重心坐标和属性插值
>
> **本模块增量**：你能精确描述 GLSL 中 `in`/`out`/`uniform` 的数据流向，理解顶点→片段的数据经过光栅化器时发生了什么，能独立排查 Shader 编译/链接失败。
>
> **下一步**：[[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] — 数据流懂了，但 MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？光源位置呢？

---

# GLSL 数据流与着色器编译

## 问题 0：管线概念懂了，但数据到底是怎么流的？

在 [[Notes/计算机图形学/Shader与光照/GPU上的Blinn-Phong光照|GPU上的Blinn-Phong光照]] 里，你已经见过这条管线：

```
顶点数据 → 顶点着色器 → 图元装配 → 光栅化 → 片段着色器 → 输出合并
```

你也见过一段 GLSL 代码：顶点着色器把 `vColor` 传出去，片段着色器把 `vColor` 接进来，三角形内部呈现出颜色渐变。

但这里有一连串问题被一句话带过了：

- 顶点着色器输出的 `vColor`，片段着色器凭什么能拿到？
- 如果三个顶点的颜色不同，三角形内部的颜色是怎么算出来的？
- CPU 上的 C++ 代码怎么把 MVP 矩阵、光源位置传进 Shader？
- `glCompileShader` 和 `glLinkProgram` 到底有什么区别？
- 为什么有时候编译过了、链接却失败？

这些问题不解决，你写的 Shader 就是“知其然不知其所以然”——一旦画面不对，完全不知道怎么调试。

---

## 问题 1：顶点着色器输出的数据，片段着色器怎么拿到？

### 最 naive 的想法：像函数调用一样，顶点着色器 return，片段着色器接收

如果你刚从 C++ 过来，可能会觉得顶点着色器和片段着色器之间应该像这样：

```cpp
// 想象中的调用方式
VertexOutput vs_out = vertex_shader(vertex);
for (Fragment frag : rasterize(triangle, vs_out)) {
    vec4 color = fragment_shader(frag, vs_out);
}
```

但 GPU 的并行模型不是这样。顶点着色器对每个顶点执行一次，片段着色器对每个像素执行一次——它们不是一对一调用关系。一个三角形有 3 个顶点输出，但内部可能有上万个像素片段。片段着色器不可能“拿到”某个特定顶点的返回值，它拿到的是**三个顶点输出的插值结果**。

### 改进：用 `out` / `in` 声明跨阶段变量

GLSL 用 `out` 声明顶点着色器的输出，用 `in` 声明片段着色器的输入。**同名变量会被链接器自动匹配**。

```glsl
// 顶点着色器
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vColor;  // 输出到下一阶段

void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
}
```

```glsl
// 片段着色器
#version 330 core
in vec3 vColor;   // 从顶点着色器接收（名称必须一致）
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
```

这里有三条关键规则：

1. **类型必须匹配**：顶点 `out vec3` 对应片段 `in vec3`，不能一个是 `vec3` 另一个是 `vec4`。
2. **名称必须一致**：OpenGL 链接器按变量名匹配。如果你把片段里的 `vColor` 写成 `fColor`，链接器找不到对应输出，变量值为未定义（通常是 0）。
3. **位置可以显式指定**：在 GLSL 3.30+ 中可以用 `layout (location = 0) out vec3 vColor;` 显式指定位置。显式位置的好处是跨阶段匹配不依赖变量名，这也是 Vulkan SPIR-V 的强制要求。

> **历史注记**：在旧的 GLSL（以及 OpenGL ES 2.0 / WebGL 1.0）中，这种跨阶段变量叫 `varying`。现代 GLSL 用 `in`/`out` 更直观，但概念完全一样。

---

## 问题 2：三个顶点的颜色不同，三角形内部的颜色是怎么来的？

### 光栅化器会帮你做插值

顶点着色器执行 3 次，输出 3 个颜色。光栅化器在三角形内部生成片段时，会根据每个片段的**重心坐标**对颜色做线性插值。

```
顶点 0：红色 (1, 0, 0)
顶点 1：绿色 (0, 1, 0)
顶点 2：蓝色 (0, 0, 1)
        │
        ▼
   光栅化器插值
        │
        ▼
内部像素：红绿蓝按重心坐标加权混合
```

这正是你在 [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] 里学过的重心坐标插值。在软渲染器里，你自己写代码计算 `w0`、`w1`、`w2` 然后插值颜色；在 GPU 上，**光栅化器硬件自动完成这个插值**。

### 插值是“免费”的吗？

不是免费，而是**由固定功能单元完成**，不需要你写 Shader 代码。每个从顶点着色器 `out` 出来的变量，都会经过光栅化器的插值器（Interpolator）。但插值器数量和带宽有限：

- GLSL 3.3 要求至少支持 16 个 `vec4` 的插值输出（实际 GPU 通常更多）。
- 输出变量越多，光栅化阶段占用的插值器资源越多，可能降低性能。
- 某些数据（如法线）需要**透视校正插值**（Perspective-Correct Interpolation），否则远表面的插值会失真。OpenGL 默认对 `in`/`out` 做透视校正插值。

```glsl
// 如果你明确不需要透视校正（极少见），可以这样声明
in vec3 vColor flat;  // flat shading：三角形内所有片段用同一个顶点的值
```

> **诚实边界**：光栅化器插值的是**屏幕空间线性**的值。对于深度（`z`）和纹理坐标，直接屏幕空间线性插值是错的，需要透视校正。OpenGL 默认帮你做了，但理解这一点对后续写 Shadow Map、延迟渲染很重要。

---

## 问题 3：CPU 上的数据怎么进入 Shader？

现在你知道了顶点属性（通过 VAO）怎么进入顶点着色器，也知道了顶点输出怎么传到片段着色器。但还有一类数据不是“每个顶点一个”的：

- MVP 矩阵：同一帧里所有顶点共享同一个矩阵
- 光源位置：同一帧里所有像素共享同一个光源位置
- 时间、鼠标位置、全局参数……

这类数据用 **Uniform** 传递。

### Uniform 的本质：全局只读常量

```glsl
// 顶点着色器
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
```

```cpp
// C++ 端设置 Uniform（代码片段，下一篇笔记会展开）
GLint loc = glGetUniformLocation(program, "uModel");
glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(model));
```

Uniform 和 Vertex Attribute 的核心区别：

| 特性 | Vertex Attribute | Uniform |
|------|------------------|---------|
| 数据频率 | 每个顶点不同 | 每个 Draw Call 内恒定 |
| 数据来源 | VBO（通过 VAO 配置） | CPU 直接通过 `glUniform*` 设置 |
| 可访问阶段 | 顶点着色器 | 顶点/片段着色器都可读 |
| 典型用途 | 位置、法线、UV、颜色 | MVP 矩阵、光源、相机、材质参数 |

> 这里只建立直觉。Uniform 的 API 调用、数量限制、`std140` 布局、以及在引擎材质系统中的作用，会在 [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] 中深入。

---

## 问题 4：Shader 代码怎么变成 GPU 能执行的程序？

你已经见过 `glCreateShader`、`glCompileShader`、`glLinkProgram`，但这一步到底在做什么？为什么需要“编译”和“链接”两个阶段？

### 阶段一：编译（Compile）

每个 Shader（顶点、片段、几何……）都是一份 GLSL 源码。`glCompileShader` 把它编译成**中间二进制**（各厂商私有格式，对开发者不可见）。

```cpp
GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
glShaderSource(vertexShader, 1, &vsSource, nullptr);
glCompileShader(vertexShader);

// 必须检查编译是否成功
GLint success;
glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
    std::cerr << "顶点着色器编译失败：\n" << infoLog << std::endl;
}
```

编译只检查**单个 Shader 的语法和类型**：

- 语法错误（少个分号、类型不匹配）
- 使用了当前 Shader 阶段不存在的功能（比如在顶点着色器里写 `discard`）
- 超过该阶段的资源限制（如顶点属性数量超限）

**编译不检查**跨阶段变量是否匹配，也不检查 Uniform 名称是否一致。

### 阶段二：链接（Link）

链接把多个 Shader 组合成一个完整的 GPU 程序。

```cpp
GLuint program = glCreateProgram();
glAttachShader(program, vertexShader);
glAttachShader(program, fragmentShader);
glLinkProgram(program);

// 必须检查链接是否成功
glGetProgramiv(program, GL_LINK_STATUS, &success);
if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(program, 512, nullptr, infoLog);
    std::cerr << "Program 链接失败：\n" << infoLog << std::endl;
}

// 链接完成后，独立的 Shader 对象可以删除
glDeleteShader(vertexShader);
glDeleteShader(fragmentShader);
```

链接阶段做的事情：

1. **匹配顶点输出和片段输入**：检查 `out`/`in` 变量名和类型是否一致。
2. **分配 Uniform 位置**：给每个 Uniform 一个整数位置，后续 `glUniform*` 要用。
3. **优化跨阶段接口**：比如删除没有真正被片段着色器使用的顶点输出。
4. **生成最终可执行程序**：GPU 可以切换并执行这个 Program。

> **为什么链接后要 `glDeleteShader`？** 因为 `glAttachShader` + `glLinkProgram` 已经把 Shader 代码复制到 Program 里了。独立的 Shader 对象只是容器，删除它们不会影响已链接的 Program。

---

## 问题 5：Shader 编译/链接最常见的坑

### 坑 1：只检查编译，不检查链接

初学者常写：

```cpp
glCompileShader(vs);
// 检查 vs 编译...
glCompileShader(fs);
// 检查 fs 编译...
glLinkProgram(program);
// ❌ 忘记检查链接状态！
```

编译成功不代表链接成功。最常见链接失败原因是：顶点 `out vec3 vColor;`，片段写成 `in vec2 vColor;`——类型不匹配。

### 坑 2：`glUseProgram` 后立刻 `glUniform*`，但 Program 其实没链接成功

如果 Program 链接失败，`glUseProgram` 不会报错（它只把当前 Program 设为 0 或无效 Program），但 `glUniform*` 设置不会生效，画面全黑或全白。

### 坑 3：变量名拼写错误

```glsl
// 顶点
out vec3 vColor;

// 片段
in vec3 vColro;  // 拼写错误 → 链接器找不到匹配，值为 0
```

OpenGL 不会报错，只是这个变量永远为 0。

### 坑 4：Fragment Shader 没有 `out` 变量

Core Profile 要求片段着色器至少声明一个 `out vec4` 作为颜色输出。如果没有，链接通常失败。

### 坑 5：Uniform 位置缓存后，重新链接 Program 导致位置变化

```cpp
GLint loc = glGetUniformLocation(program, "uModel");
// ... 修改 Shader 源码后重新编译链接 ...
glUniformMatrix4fv(loc, ...);  // ❌ loc 可能已失效
```

每次重新链接 Program 后，Uniform 位置可能变化。要么重新查询，要么在 Shader 里用 `layout(location = 0) uniform ...` 显式固定位置。

---

## 完整的最小示例：带 MVP 变换的彩色三角形

把 VAO、VBO、Shader 编译链接、Uniform 传递串起来：

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

const char* vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uMVP;
out vec3 vColor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* fsSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
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

    GLFWwindow* window = glfwCreateWindow(800, 600, "GLSL 数据流", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // 顶点数据：位置 + 颜色
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    GLuint vbo, vao;
    glGenBuffers(1, &vbo);
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // 编译并链接 Shader
    GLuint program = glCreateProgram();
    GLuint vs = compileShader(vsSrc, GL_VERTEX_SHADER);
    GLuint fs = compileShader(fsSrc, GL_FRAGMENT_SHADER);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk;
    glGetProgramiv(program, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program 链接失败:\n" << log << std::endl;
        return -1;
    }

    // 查询 Uniform 位置
    GLint mvpLoc = glGetUniformLocation(program, "uMVP");

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 构造 MVP 矩阵
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0, 0, 1));
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -3));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        glm::mat4 mvp = proj * view * model;

        glUseProgram(program);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

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

这个示例里，数据流完整链路是：

```
CPU 内存 vertices[]
        │ glBufferData
        ▼
      VBO（显存）
        │ VAO 配置：属性0=位置，属性1=颜色
        ▼
  顶点着色器：aPos / aColor
        │
        ├─ uMVP（Uniform，来自 CPU 每帧更新）
        │
        ▼
  gl_Position → 光栅化器
  vColor      → 光栅化器插值 → 片段着色器 in vColor
        │
        ▼
   FragColor → 帧缓冲
```

---

## 状态变化图：Program 对象与 Shader 对象的关系

很多初学者搞不清 Shader 对象和 Program 对象的关系。下面这张图展示从源码到可执行程序的完整状态变化：

```
阶段 1：源码
  vsSource（C++ 字符串）
  fsSource（C++ 字符串）
        │
        │ glCreateShader + glShaderSource
        ▼
阶段 2：Shader 对象（未编译）
  ┌─────────────┐   ┌─────────────┐
  │ GL_VERTEX_  │   │ GL_FRAGMENT │
  │   SHADER    │   │   SHADER    │
  │  容器对象    │   │  容器对象    │
  └──────┬──────┘   └──────┬──────┘
         │                 │
         │ glCompileShader │
         ▼                 ▼
阶段 3：编译后的 Shader 对象
  ┌─────────────┐   ┌─────────────┐
  │ 编译后的 VS  │   │ 编译后的 FS  │
  │ 中间二进制   │   │ 中间二进制   │
  └──────┬──────┘   └──────┬──────┘
         │                 │
         │ glAttachShader  │
         └────────┬────────┘
                  ▼
阶段 4：Program 对象
  ┌─────────────────────────────┐
  │        GL_PROGRAM           │
  │  包含 VS + FS 的完整程序     │
  │  链接后生成最终可执行代码    │
  │  包含 Uniform 位置表        │
  └─────────────────────────────┘
                  │
                  │ glLinkProgram
                  ▼
阶段 5：可执行 Program
  ┌─────────────────────────────┐
  │    链接完成的 Program        │
  │  可用 glUseProgram 激活      │
  │  可设置 Uniform / 绘制      │
  └─────────────────────────────┘
                  │
                  │ glUseProgram(program)
                  ▼
阶段 6：当前激活的 Shader Program
  （OpenGL Context 的当前 Program 状态）
```

> 关键认知：**Shader 对象是临时的**，链接完成后通常可以删除；**Program 对象是持久的**，绘制时通过 `glUseProgram` 激活。

---

## 与现代 API 的对照

我们在解决的是「**Shader 阶段之间的数据传递和程序组装**」这个具体问题。

| 概念 | OpenGL (GLSL) | Vulkan (SPIR-V) | D3D12 (HLSL) |
|------|---------------|-----------------|--------------|
| 顶点输入 | `layout(location=N) in` | `location` in SPIR-V | `SEMANTIC` + `INPUT_ELEMENT_DESC` |
| 顶点→片段输出 | `out` / `in` 同名匹配 | SPIR-V `location` 显式匹配 | `SV_Position` / 自定义语义 |
| 全局常量 | `uniform` | `PushConstant` / `UniformBuffer` + DescriptorSet | `cbuffer` / `ConstantBuffer` |
| 编译单元 | `glCompileShader`（单 Shader） | `glslangValidator` / `dxc` 离线编译 SPIR-V | `dxc` / `fxc` 离线编译 Shader Bytecode |
| 链接 | `glLinkProgram`（运行时） | `vkCreateGraphicsPipelines`（运行时组合） | `CreateGraphicsPipelineState`（运行时组合） |

> **个人项目推荐**：学习阶段用 OpenGL 的在线编译链接完全够用。向现代 API 迁移时，mentally map 为："`glCompileShader` ≈ 离线编译器生成字节码；`glLinkProgram` ≈ 创建 Pipeline State Object（PSO）"。

---

## 与 SelfGameEngine 的关系

### 这就是引擎 RHI 层要封装的第一件事

在 [[Notes/SelfGameEngine/渲染管线与画面/RHI抽象层与命令模型|RHI抽象层与命令模型]] 里，你已经知道引擎需要一层 GPU 抽象。Shader 的编译链接是 RHI 层最早要处理的问题之一。

引擎不会直接暴露 `glCreateShader` 给上层。典型的 RHI 抽象是：

```cpp
struct ShaderDesc {
    ShaderStage stage;          // Vertex / Fragment / Geometry ...
    const char* source;         // GLSL / HLSL 源码
    const char* entryPoint;     // "main" 或自定义入口
};

ShaderHandle device->CreateShader(const ShaderDesc& desc);
PipelineStateHandle device->CreatePipelineState(const PipelineStateDesc& desc);
```

OpenGL 后端内部会做 `glCreateShader` / `glCompileShader` / `glAttachShader` / `glLinkProgram`；Vulkan/D3D12 后端则可能是离线编译好的 SPIR-V/DXIL 字节码 + Pipeline 创建。

### 这是引擎材质系统的入口

在 [[Notes/SelfGameEngine/渲染管线与画面/着色器变体与编译缓存|着色器变体与编译缓存]] 里，你会看到引擎如何管理“同一个材质模板的多种变体”。每个变体本质上就是一次 Shader 编译 + Program 链接。理解这里的编译链接流程，才能理解为什么变体爆炸会带来编译卡顿、为什么需要异步编译和缓存。

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| Shader 编译错误 | 必须调用 `glGetShaderiv(GL_COMPILE_STATUS)` 检查 |
| Program 链接错误 | 必须调用 `glGetProgramiv(GL_LINK_STATUS)` 检查 |
| 跨阶段变量匹配 | 名称、类型必须一致，或显式 `layout(location=N)` |
| Uniform 位置安全 | 重新链接 Program 后必须重新查询 Uniform 位置 |
| 资源释放 | 链接完成后用 `glDeleteShader` 释放临时 Shader 对象 |
| 插值器预算 | 顶点 `out` 变量不要过多，避免占用过多插值资源 |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| `in`/`out` 跨阶段数据流 | Uniform 和 Attribute 的完整 API 对比 |
| 光栅化插值机制 | Uniform Buffer / `std140` 布局 |
| Shader 编译链接流程 | 引擎中的 Shader 变体与缓存 |
| 常见编译/链接陷阱 | 多 Pass、多 Shader 阶段的管理 |

> **下一步**：[[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] — MVP 矩阵每个顶点都一样，应该用 Attribute 还是 Uniform？光源位置呢？Uniform 在引擎材质系统中扮演什么角色？

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
