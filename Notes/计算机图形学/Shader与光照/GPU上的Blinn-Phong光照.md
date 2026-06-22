---
title: GPU上的Blinn-Phong光照
description: 模型已经能加载到GPU，但看起来还是一块纯色塑料。把Blinn-Phong公式写进GLSL，让像素根据光源、相机、法线决定颜色。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - glsl
  - blinn-phong
  - shading
  - lighting
  - material
aliases:
  - GPU Blinn-Phong
  - Blinn-Phong光照
  - 光照着色器
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：
> - [[Notes/计算机图形学/Shader与光照/GLSL数据流与着色器编译|GLSL数据流与着色器编译]] — 你已经理解 `in`/`out`/`uniform` 的数据流和 Shader 编译链接
> - [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] — 你已经能按数据频率选择 Attribute 或 Uniform
> - [[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] — 你已经能把带法线的模型上传到 GPU
>
> **本模块增量**：你能把 Blinn-Phong 公式完整搬进 GLSL，理解「公式写在哪、数据怎么进」，能解释为什么法线要单独变换，能独立运行一个带光照的旋转立方体。
>
> **下一步**：[[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理映射与UV坐标]] — 光照有了，但模型还是纯色。下一步用纹理给表面加上细节。

---

# GPU 上的 Blinn-Phong 光照

## 问题 0：模型已经画出来了，但为什么像一块彩色塑料？

你在 [[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] 里已经能把一个带法线的立方体上传到 GPU，在 [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] 里也已经会用 Uniform 传 MVP 矩阵。现在屏幕上出现了一个旋转的立方体——但每个面都是同一个颜色，没有明暗，没有立体感。

**最 naive 的方案**：片段着色器直接输出一个固定颜色。

```glsl
#version 330 core
out vec4 FragColor;

uniform vec3 uObjectColor;

void main() {
    FragColor = vec4(uObjectColor, 1.0);
}
```

**立刻发现的问题**：
- 立方体的六个面完全分不出朝向，旋转时只能靠轮廓判断深度。
- 没有任何「被光照射」的暗示，看起来像一个剪影贴图，而不是三维物体。

**根本原因**：像素颜色还没有和「光线方向」「表面朝向」「相机位置」建立任何关系。着色（Shading）就是建立这种关系的计算。

---

## 问题 1：光线和表面到底怎么互动？

真实世界里，我们看到物体表面的亮度取决于三类反射的叠加：

| 分量 | 人话解释 | 控制什么 |
|------|---------|---------|
| **环境光（Ambient）** | 间接光照的偷懒近似 | 保证背光面不会纯黑 |
| **漫反射（Diffuse）** | 粗糙表面把光均匀地散射出去 | 物体朝向光源的一面更亮 |
| **镜面高光（Specular）** | 光滑表面像镜子一样反射光线 | 亮斑、反光、材质光泽 |

### 漫反射：兰伯特余弦定律

粗糙表面会把入射光向四面八方均匀散射。散射出去的强度与光线和表面法线的夹角余弦成正比：

$$
L_{diffuse} = k_d \cdot I \cdot \max(0, \mathbf{n} \cdot \mathbf{l})
$$

- $\mathbf{n}$：表面法线（单位向量）
- $\mathbf{l}$：指向光源的方向（单位向量）
- $k_d$：漫反射颜色（物体本身的颜色）
- $I$：光源颜色/强度
- 用 $\max(0, \dots)$ 是因为光线从背面照过来时不应该产生负的亮度。

### 高光：为什么用 Blinn-Phong 而不是 Phong？

经典 Phong 模型用**反射方向** $\mathbf{r}$ 和视线方向 $\mathbf{v}$ 的夹角计算高光，需要一次 `reflect()` 运算。Blinn-Phong 的改进是引入**半程向量（Halfway Vector）**：

$$
\mathbf{h} = \frac{\mathbf{l} + \mathbf{v}}{\|\mathbf{l} + \mathbf{v}\|}
$$

然后高光的强度变成法线 $\mathbf{n}$ 和半程向量 $\mathbf{h}$ 夹角的余弦：

$$
L_{specular} = k_s \cdot I \cdot \max(0, \mathbf{n} \cdot \mathbf{h})^p
$$

- $p$ 是光泽度（shininess），越大高光越集中。
- Blinn-Phong 的好处：避免了 Phong 在低 shininess 时出现的「高光截断」问题，而且计算量略小。
- 代价：同样的 shininess 值，Blinn-Phong 的高光范围比 Phong 更宽。要得到类似效果，通常要把 Blinn-Phong 的 $p$ 设为 Phong 的 2~4 倍。

### 完整公式

$$
L = L_{ambient} + L_{diffuse} + L_{specular}
$$

展开后：

$$
L = k_a \cdot I_a + k_d \cdot I \cdot \max(0, \mathbf{n} \cdot \mathbf{l}) + k_s \cdot I \cdot \max(0, \mathbf{n} \cdot \mathbf{h})^p
$$

> 这不是新公式——你在软渲染器阶段已经推导过它。本篇的任务是：**把它写进 GPU 的 Shader，并确保数据能正确到达每个像素。**

---

## 问题 2：这个公式应该写在顶点着色器还是片段着色器里？

### 最 naive 的想法：在顶点着色器里算，然后插值到像素

既然顶点着色器每个顶点执行一次，片段着色器每个像素执行一次，那在顶点里算岂不是更快？

```glsl
// 顶点着色器里算光照（Gouraud Shading）
void main() {
    vec3 worldPos = vec3(uModel * vec4(aPos, 1.0));
    vec3 normal = mat3(transpose(inverse(uModel))) * aNormal;
    vec3 lightDir = normalize(uLightPos - worldPos);
    vec3 viewDir = normalize(uViewPos - worldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float diff = max(dot(normal, lightDir), 0.0);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

    vColor = (0.1 + diff + spec) * uObjectColor;
    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}
```

**立刻发现的问题**：
- 高光区域在顶点之间被线性插值，如果三角形内部刚好有高光峰值，但顶点没算到，高光就会完全消失。
- 法线、视线方向、光照方向都不是线性量，直接插值会得到错误结果（尤其是非均匀缩放时）。

**改进方案：把光照计算移到片段着色器（Phong Shading）**

顶点着色器只负责把「世界空间位置」和「世界空间法线」传给片段着色器，片段着色器在每个像素上重新计算光照。GPU 的并行能力足够强，每个像素做一次点乘和幂运算并不是什么负担。

> **决策推荐**：现代 GPU 几乎总是用 Phong Shading（逐像素光照）。Gouraud Shading 只在极早期硬件或特殊优化场景下才有意义。

---

## 问题 3：法线从模型空间到世界空间，为什么不能直接用 `uModel` 乘？

顶点法线通常是在模型空间（局部坐标系）里定义的。要做光照计算，必须把它变换到世界空间。但你不能直接写：

```glsl
// ❌ 错误：非均匀缩放会让法线变形
vec3 worldNormal = (uModel * vec4(aNormal, 0.0)).xyz;
```

### 为什么不行？

法线描述的是表面朝向，不是位置。当模型被**非均匀缩放**时（比如 x 方向拉宽 2 倍，y 方向压扁 0.5 倍），表面的切线方向变了，法线方向也要做相应的反向调整，才能继续垂直于表面。

正确的变换是**法线矩阵（Normal Matrix）**：

$$
\mathbf{M}_{normal} = (\mathbf{M}_{model}^{-1})^T
$$

取模型矩阵的逆，再转置，最后取左上 3×3 部分：

```glsl
vec3 worldNormal = mat3(transpose(inverse(uModel))) * aNormal;
```

> 只有当模型只做旋转和均匀缩放时，`uModel` 的 3×3 部分才等于法线矩阵。为了保险， always 用 `transpose(inverse(model))`。

> **诚实边界**：`inverse()` 在 Shader 里不是免费的，尤其顶点着色器每个顶点都要算一次。工业引擎通常把法线矩阵作为 Uniform 直接传进来，而不是在 Shader 里求逆。学习阶段这样写没问题，但心里要知道这是可优化的点。

---

## 问题 4：光照参数和材质参数应该用什么数据通道？

回顾 [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] 里的决策规则：

| 数据 | 通道 | 原因 |
|------|------|------|
| 顶点位置 `aPos` | Attribute | 每个顶点不同 |
| 顶点法线 `aNormal` | Attribute | 每个顶点不同 |
| Model / View / Projection 矩阵 | Uniform | 一次 Draw Call 内共享 |
| 光源位置、颜色 | Uniform | 一帧内所有像素共享 |
| 相机位置 | Uniform | 一帧内所有像素共享 |
| 物体颜色、光泽度 | Uniform | 一个材质实例共享 |

所以 Blinn-Phong 的 Shader 里，顶点属性只有位置和法线；所有光照/材质参数都走 Uniform。

---

## 完整实现：带光照的旋转立方体

### 顶点着色器

```glsl
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
```

### 片段着色器

```glsl
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform vec3 uLightColor;
uniform vec3 uObjectColor;
uniform float uShininess;
uniform float uSpecularStrength;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    // 环境光
    vec3 ambient = 0.1 * uLightColor;

    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    // 镜面高光（Blinn-Phong）
    float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);
    vec3 specular = uSpecularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(result, 1.0);
}
```

### C++ 主循环骨架

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
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
)";

const char* fsSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform vec3 uLightColor;
uniform vec3 uObjectColor;
uniform float uShininess;
uniform float uSpecularStrength;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    vec3 ambient = 0.1 * uLightColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    float spec = pow(max(dot(normal, halfwayDir), 0.0), uShininess);
    vec3 specular = uSpecularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(result, 1.0);
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

GLuint createProgram(const char* vsSource, const char* fsSource) {
    GLuint program = glCreateProgram();
    GLuint vs = compileShader(vsSource, GL_VERTEX_SHADER);
    GLuint fs = compileShader(fsSource, GL_FRAGMENT_SHADER);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linkOk;
    glGetProgramiv(program, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program 链接失败:\n" << log << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// 顶点格式：位置 + 法线（36 个顶点，每个面单独展开，不共享顶点）
struct Vertex { float pos[3], normal[3]; };
Vertex cube[36] = {
    // 前面（法线 +Z）
    {{-0.5f,-0.5f, 0.5f},{0,0,1}}, {{ 0.5f,-0.5f, 0.5f},{0,0,1}}, {{ 0.5f, 0.5f, 0.5f},{0,0,1}},
    {{-0.5f,-0.5f, 0.5f},{0,0,1}}, {{ 0.5f, 0.5f, 0.5f},{0,0,1}}, {{-0.5f, 0.5f, 0.5f},{0,0,1}},
    // 后面（法线 -Z）
    {{ 0.5f,-0.5f,-0.5f},{0,0,-1}}, {{-0.5f,-0.5f,-0.5f},{0,0,-1}}, {{-0.5f, 0.5f,-0.5f},{0,0,-1}},
    {{ 0.5f,-0.5f,-0.5f},{0,0,-1}}, {{-0.5f, 0.5f,-0.5f},{0,0,-1}}, {{ 0.5f, 0.5f,-0.5f},{0,0,-1}},
    // 右面（法线 +X）
    {{ 0.5f,-0.5f, 0.5f},{1,0,0}}, {{ 0.5f,-0.5f,-0.5f},{1,0,0}}, {{ 0.5f, 0.5f,-0.5f},{1,0,0}},
    {{ 0.5f,-0.5f, 0.5f},{1,0,0}}, {{ 0.5f, 0.5f,-0.5f},{1,0,0}}, {{ 0.5f, 0.5f, 0.5f},{1,0,0}},
    // 左面（法线 -X）
    {{-0.5f,-0.5f,-0.5f},{-1,0,0}}, {{-0.5f,-0.5f, 0.5f},{-1,0,0}}, {{-0.5f, 0.5f, 0.5f},{-1,0,0}},
    {{-0.5f,-0.5f,-0.5f},{-1,0,0}}, {{-0.5f, 0.5f, 0.5f},{-1,0,0}}, {{-0.5f, 0.5f,-0.5f},{-1,0,0}},
    // 顶面（法线 +Y）
    {{-0.5f, 0.5f, 0.5f},{0,1,0}}, {{ 0.5f, 0.5f, 0.5f},{0,1,0}}, {{ 0.5f, 0.5f,-0.5f},{0,1,0}},
    {{-0.5f, 0.5f, 0.5f},{0,1,0}}, {{ 0.5f, 0.5f,-0.5f},{0,1,0}}, {{-0.5f, 0.5f,-0.5f},{0,1,0}},
    // 底面（法线 -Y）
    {{-0.5f,-0.5f,-0.5f},{0,-1,0}}, {{ 0.5f,-0.5f,-0.5f},{0,-1,0}}, {{ 0.5f,-0.5f, 0.5f},{0,-1,0}},
    {{-0.5f,-0.5f,-0.5f},{0,-1,0}}, {{ 0.5f,-0.5f, 0.5f},{0,-1,0}}, {{-0.5f,-0.5f, 0.5f},{0,-1,0}},
};

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Blinn-Phong Cube", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLuint program = createProgram(vsSrc, fsSrc);

    GLint modelLoc = glGetUniformLocation(program, "uModel");
    GLint viewLoc  = glGetUniformLocation(program, "uView");
    GLint projLoc  = glGetUniformLocation(program, "uProjection");
    GLint lightPosLoc = glGetUniformLocation(program, "uLightPos");
    GLint viewPosLoc  = glGetUniformLocation(program, "uViewPos");
    GLint lightColorLoc = glGetUniformLocation(program, "uLightColor");
    GLint objectColorLoc = glGetUniformLocation(program, "uObjectColor");
    GLint shininessLoc = glGetUniformLocation(program, "uShininess");
    GLint specStrengthLoc = glGetUniformLocation(program, "uSpecularStrength");

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        glm::mat4 view = glm::lookAt(glm::vec3(2,2,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f/600.0f, 0.1f, 100.0f);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));

        glUniform3f(lightPosLoc, 2.0f, 3.0f, 2.0f);
        glUniform3f(viewPosLoc, 2.0f, 2.0f, 3.0f);
        glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);
        glUniform3f(objectColorLoc, 0.8f, 0.5f, 0.2f);
        glUniform1f(shininessLoc, 32.0f);
        glUniform1f(specStrengthLoc, 0.5f);

        glm::mat4 model = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0,1,0));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

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

> 这个例子故意把立方体展开成 36 个顶点（每个面 6 个），是为了让每个顶点带有独立的、朝外的面法线。如果你用 8 个顶点 + EBO，就需要处理「一个顶点被多个面共享时法线怎么平均」的问题——那是 [[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] 里顶点去重的范畴。

---

## 状态变化图：数据怎么从 CPU 流到像素

```
CPU 内存
   │
   ├── Attribute 通道 ─────► VBO ─────► VAO ─────► 顶点着色器 (aPos, aNormal)
   │                                                            │
   │                                                            ▼
   │                                                    vWorldPos, vNormal
   │                                                            │
   │                                                            │ 光栅化器插值
   │                                                            ▼
   │                                                    片段着色器 (vWorldPos, vNormal)
   │                                                            │
   └── Uniform 通道 ───────► glUniform* ───► Program ───┤───────┤
                                              │         │       │
                                              │         ├── uModel / uView / uProjection
                                              │         ├── uLightPos / uLightColor
                                              │         ├── uViewPos
                                              │         └── uObjectColor / uShininess / uSpecularStrength
                                              ▼
                                         GPU 执行 Shader
                                              │
                                              ▼
                                         FragColor → 帧缓冲
```

> 关键状态机事实：Uniform 是 Program 的状态。`glUseProgram(program)` 必须在 `glUniform*` 之前。切换 VAO 不会重置 Uniform。

---

## 与现代 API 的对照

我们在解决的是「**如何把 Blinn-Phong 公式放进可编程管线并传入参数**」这个具体问题。

| 概念 | OpenGL (GLSL) | Vulkan (SPIR-V) | D3D12 (HLSL) |
|------|---------------|-----------------|--------------|
| 每顶点数据 | Attribute + VAO/VBO | `VkVertexInputAttributeDescription` | `D3D12_INPUT_ELEMENT_DESC` |
| 每 Draw Call 常量 | `uniform` + `glUniform*` | Push Constants / UBO + DescriptorSet | `cbuffer` / Root Constants |
| 顶点→片段输出 | `out` / `in` 同名匹配 | SPIR-V `location` | `SV_Position` / 自定义语义 |
| 逐像素光照 | 在 Fragment Shader 中计算 | 在 Fragment Shader 中计算 | 在 Pixel Shader 中计算 |

> **个人项目推荐**：学习阶段用 OpenGL 的 `glUniform*` 完全够用。向现代 API 迁移时，mentally map 为：「本例中的 light / material Uniform 在 Vulkan 中会变成 Push Constants 或一个小的 Uniform Buffer」。

---

## 与 SelfGameEngine 的关系

### 这就是引擎「材质系统」的最小可运行版本

在 [[Notes/SelfGameEngine/渲染管线与画面/材质系统架构|材质系统架构]] 里，材质被拆成 Template-Asset-Instance 三层。本例中的：

- **Shader 源码** ≈ Material Template 的某个变体
- **Uniform 参数集**（`uObjectColor`、`uShininess`、`uSpecularStrength`）≈ Material Asset 中美术可调的值
- **运行时 `glUseProgram` + `glUniform*` 这组调用** ≈ Material Instance 在 GPU 上的具体执行

```cpp
// 引擎上层：美术定义的材质资产
struct StandardMaterial {
    Vec4 baseColor;
    float shininess;
    float specularStrength;
};

// RHI 层：绘制时把资产参数翻译成 Uniform 上传
// OpenGL 后端：glUniform3f / glUniform1f
// Vulkan 后端：PushConstant 或 DescriptorSet 更新
```

> **关键认知**：一个 Material = Shader + Uniform 参数集。你现在写的不是「某个材质」，而是「引擎材质系统里最小可运行材质」的底层原形。

### 与引擎渲染管线的衔接

在 [[Notes/SelfGameEngine/渲染管线与画面/材质参数绑定与GPU上传|材质参数绑定与GPU上传]] 里，你会看到工业引擎如何把参数按更新频率分层：

- **每帧一次**：View、Projection、Camera、全局光源 → UBO binding 0
- **每材质一次**：baseColor、shininess → Uniform / UBO binding 1
- **每物体一次**：Model 矩阵 → Push Constant / Uniform binding 2

本例为了简单，所有 Uniform 都通过 `glUniform*` 单独上传；工业引擎会把它们按频率打包，减少 Draw Call 开销。

---

## 常见陷阱

### 陷阱 1：没有启用深度测试

```cpp
// ❌ 漏掉这一行，背面三角形可能覆盖前面三角形
glEnable(GL_DEPTH_TEST);
```

不带光照时你只会看到颜色错乱；带了光照后，错误会更明显——暗面会突然出现在亮面上。

### 陷阱 2：法线没有归一化

```glsl
// ❌ 插值后的 vNormal 长度可能不是 1
float diff = max(dot(vNormal, lightDir), 0.0);

// ✅ 必须在片段着色器里重新 normalize
vec3 normal = normalize(vNormal);
```

顶点着色器输出的法线长度是 1，但经光栅化器线性插值后，三角形内部的法线长度会变短，导致点乘结果偏小、光照变暗。

### 陷阱 3：光照在世界空间算，但位置没转到世界空间

```glsl
// ❌ 错误：在世界空间算光照，但 FragPos 还在模型空间
vec3 lightDir = normalize(uLightPos - aPos);

// ✅ 正确：所有向量必须在同一空间
vec3 lightDir = normalize(uLightPos - vWorldPos);
```

只要所有向量（法线、光源方向、视线方向、片段位置）在同一个空间，公式就成立。最常用的是世界空间；有些引擎为了效率会在观察空间算，但必须统一。

### 陷阱 4：非均匀缩放后法线变形

```glsl
// ❌ 错误：直接用 uModel 乘 aNormal
vec3 worldNormal = (uModel * vec4(aNormal, 0.0)).xyz;

// ✅ 正确：用法线矩阵
vec3 worldNormal = mat3(transpose(inverse(uModel))) * aNormal;
```

### 陷阱 5：在 `glUseProgram` 之前设置 Uniform

```cpp
// ❌ 错误
 glUniform3f(lightPosLoc, ...);
glUseProgram(program);

// ✅ 正确
glUseProgram(program);
glUniform3f(lightPosLoc, ...);
```

---

## 设计 checklist

| 检查项 | 标准 |
|--------|------|
| 光照计算位置 | 优先放在片段着色器（Phong Shading） |
| 法线变换 | 用 `mat3(transpose(inverse(model))) * aNormal` |
| 法线归一化 | 在片段着色器里 `normalize(vNormal)` |
| 空间一致性 | 法线、光源方向、视线方向必须在同一空间 |
| Uniform 设置顺序 | 先 `glUseProgram`，再 `glUniform*` |
| 深度测试 | 3D 光照场景必须 `glEnable(GL_DEPTH_TEST)` |
| 参数通道 | 顶点数据走 Attribute，光源/材质/矩阵走 Uniform |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| Blinn-Phong 公式写进 GLSL | 多光源场景怎么组织 Uniform |
| 法线矩阵的原理 | 用纹理替代纯色（漫反射贴图） |
| MVP / 光源 / 材质参数的 Uniform 上传 | 用 UBO 按更新频率打包参数 |
| 单个物体的逐像素光照 | 多个物体、多个材质实例的合批与排序 |

> **下一步**：[[Notes/计算机图形学/纹理系统/纹理映射与UV坐标|纹理映射与UV坐标]] — 光照让立方体有了立体感，但表面还是单一颜色。下一步把图片贴到表面上，让材质开始有真实细节。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
