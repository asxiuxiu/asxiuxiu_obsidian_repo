---
title: 索引缓冲 EBO
description: 立方体只有 8 个顶点，但 12 个三角形需要 36 个顶点。通过 EBO 复用顶点，理解 glDrawElements 与 VAO/VBO 的绑定关系。
date: 2026-06-22
tags:
  - graphics
  - opengl
  - ebo
  - index-buffer
  - vao
aliases:
  - Element Buffer Object
  - EBO
  - 索引缓冲对象
  - glDrawElements
---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
>
> **前置依赖**：[[Notes/计算机图形学/顶点数据与索引/VAO与顶点属性配置|VAO与顶点属性配置]] — 你已经能用 VAO/VBO 画出带 UV 的三角形
>
> **本模块增量**：你能解释"为什么需要 EBO"，能用 EBO + `glDrawElements` 渲染一个立方体，并理解 EBO 在 VAO 状态机中的精确位置。
>
> **下一步**：[[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] — 把 OBJ 解析出来的顶点/索引上传到 GPU，渲染真实模型。

---

# 索引缓冲 EBO

## 问题 0：一个立方体到底需要几个顶点？

想象你要画一个立方体。一个立方体有 8 个角，每个角是一个顶点。看起来只需要 8 个顶点就够了。

但 GPU 画的是**三角形**，不是立方体。一个立方体有 6 个面，每个面由 2 个三角形组成，一共 12 个三角形。每个三角形需要 3 个顶点，所以总共需要 `12 × 3 = 36` 个顶点。

问题来了：**立方体只有 8 个唯一点，但三角形的顶点列表需要 36 个条目。** 这意味着很多顶点会被重复使用——比如立方体正面的左上角，同时属于正面左上角的三角形和左侧面、顶面的三角形。

如果不做任何处理，我们只能把同一个顶点重复写进顶点数组。

---

## 问题 1：最 naive 的方案——把顶点复制 36 份

既然 GPU 要 36 个顶点，那我们就给它 36 个顶点。每个三角形用 3 个独立的顶点，即使这 3 个顶点里有 2 个和其他三角形完全相同。

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstddef>

struct Vertex {
    float pos[3];
    float color[3];
};

// 8 个唯一点扩成 36 个顶点——每个面 2 个三角形 × 3 顶点
Vertex cubeVertices[] = {
    // 前面（z = +1），红色
    {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f,0.0f}},
    {{ 0.5f,-0.5f, 0.5f}, {1.0f,0.0f,0.0f}},
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,0.0f,0.0f}},
    {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f,0.0f}},
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,0.0f,0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f,0.0f,0.0f}},

    // 后面（z = -1），绿色
    {{ 0.5f,-0.5f,-0.5f}, {0.0f,1.0f,0.0f}},
    {{-0.5f,-0.5f,-0.5f}, {0.0f,1.0f,0.0f}},
    {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f,0.0f}},
    {{ 0.5f,-0.5f,-0.5f}, {0.0f,1.0f,0.0f}},
    {{-0.5f, 0.5f,-0.5f}, {0.0f,1.0f,0.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {0.0f,1.0f,0.0f}},

    // 左面（x = -1），蓝色
    {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f,1.0f}},
    {{-0.5f,-0.5f, 0.5f}, {0.0f,0.0f,1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f,0.0f,1.0f}},
    {{-0.5f,-0.5f,-0.5f}, {0.0f,0.0f,1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f,0.0f,1.0f}},
    {{-0.5f, 0.5f,-0.5f}, {0.0f,0.0f,1.0f}},

    // 右面（x = +1），黄色
    {{ 0.5f,-0.5f, 0.5f}, {1.0f,1.0f,0.0f}},
    {{ 0.5f,-0.5f,-0.5f}, {1.0f,1.0f,0.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f,0.0f}},
    {{ 0.5f,-0.5f, 0.5f}, {1.0f,1.0f,0.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {1.0f,1.0f,0.0f}},
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,1.0f,0.0f}},

    // 顶面（y = +1），紫色
    {{-0.5f, 0.5f, 0.5f}, {1.0f,0.0f,1.0f}},
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,0.0f,1.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {1.0f,0.0f,1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f,0.0f,1.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {1.0f,0.0f,1.0f}},
    {{-0.5f, 0.5f,-0.5f}, {1.0f,0.0f,1.0f}},

    // 底面（y = -1），青色
    {{-0.5f,-0.5f,-0.5f}, {0.0f,1.0f,1.0f}},
    {{ 0.5f,-0.5f,-0.5f}, {0.0f,1.0f,1.0f}},
    {{ 0.5f,-0.5f, 0.5f}, {0.0f,1.0f,1.0f}},
    {{-0.5f,-0.5f,-0.5f}, {0.0f,1.0f,1.0f}},
    {{ 0.5f,-0.5f, 0.5f}, {0.0f,1.0f,1.0f}},
    {{-0.5f,-0.5f, 0.5f}, {0.0f,1.0f,1.0f}},
};
```

这段代码能跑，但立刻暴露三个问题：

1. **显存浪费**：本可以只存 8 个顶点，现在存了 36 个。顶点越大（位置 + 法线 + UV + 切线 + 骨骼权重），浪费越严重。
2. **CPU→GPU 上传量增加**：每加载一个模型，都要多传很多重复数据。
3. **顶点属性无法共享**：同一个角在不同面上的法线方向不同（前面法线朝 +z，顶面法线朝 +y）。如果你希望每个面有独立的法线，复制顶点反而成了"刚需"；但如果你只是想要平滑的法线，复制顶点会让你失去共享能力。

> 这就像你写一封信，每次提到"张三"都重新写一遍他的全名、地址、电话，而不是写"详见第 1 页联系人 1"。

---

## 问题 2：能不能只存 8 个顶点，再用一个"索引表"告诉 GPU 怎么组装三角形？

当然可以。这就是 **EBO（Element Buffer Object，元素缓冲对象）**，也叫 **IBO（Index Buffer Object，索引缓冲对象）** 的核心思想：

> **顶点数据只存一份唯一顶点，索引数据存"每三个索引组成一个三角形"。**

立方体的 8 个唯一点可以定义如下：

```cpp
Vertex cubeVertices[] = {
    // 位置              颜色
    {{-0.5f,-0.5f,-0.5f}, {1.0f,0.0f,0.0f}}, // 0
    {{ 0.5f,-0.5f,-0.5f}, {0.0f,1.0f,0.0f}}, // 1
    {{ 0.5f, 0.5f,-0.5f}, {0.0f,0.0f,1.0f}}, // 2
    {{-0.5f, 0.5f,-0.5f}, {1.0f,1.0f,0.0f}}, // 3
    {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f,1.0f}}, // 4
    {{ 0.5f,-0.5f, 0.5f}, {0.0f,1.0f,1.0f}}, // 5
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,1.0f,1.0f}}, // 6
    {{-0.5f, 0.5f, 0.5f}, {0.5f,0.5f,0.5f}}, // 7
};
```

然后索引数组定义每个三角形的顶点：

```cpp
GLuint cubeIndices[] = {
    4, 5, 6,   4, 6, 7,   // 前
    0, 3, 2,   0, 2, 1,   // 后
    0, 4, 7,   0, 7, 3,   // 左
    1, 2, 6,   1, 6, 5,   // 右
    3, 7, 6,   3, 6, 2,   // 顶
    0, 1, 5,   0, 5, 4    // 底
};
```

等一下，上面的索引对应关系需要仔细看。我们以顶点编号为准：

```
        3 ──────── 2
       /│         /│
      / │        / │
     7 ──────── 6  │
     │  0 ──────┼─ 1
     │ /        │ /
     │/         │/
     4 ──────── 5
```

- 0 = 左下后（-x, -y, -z）
- 1 = 右下后（+x, -y, -z）
- 2 = 右上后（+x, +y, -z）
- 3 = 左上后（-x, +y, -z）
- 4 = 左下前（-x, -y, +z）
- 5 = 右下前（+x, -y, +z）
- 6 = 右上前（+x, +y, +z）
- 7 = 左上前（-x, +y, +z）

这样 36 个索引就能从 8 个顶点中"查表"组装出 12 个三角形。

---

## 问题 3：EBO 怎么创建和绑定？

EBO 和 VBO 一样是 OpenGL 的 Buffer Object，只是绑定目标不同：

```cpp
GLuint ebo;
glGenBuffers(1, &ebo);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
glBufferData(GL_ELEMENT_ARRAY_BUFFER,
             sizeof(cubeIndices),
             cubeIndices,
             GL_STATIC_DRAW);
```

`GL_ELEMENT_ARRAY_BUFFER` 是 EBO 的专用绑定目标。它告诉 OpenGL："这个缓冲区里的数据不是顶点，而是指向顶点的索引。"

但这里有一个**关键细节**：EBO 的绑定状态是**存储在当前 VAO 里的**，不是全局 Context 状态。这和 `GL_ARRAY_BUFFER` 完全不同。

```cpp
// ✅ 正确做法：在 VAO 绑定期间配置 EBO
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);              // 开始记录 VAO 状态

glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);   // 这行状态会被记录到 VAO 里
glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
glEnableVertexAttribArray(0);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
glEnableVertexAttribArray(1);

glBindVertexArray(0);                // 结束记录
```

绘制时只需绑定 VAO，EBO 会随 VAO 一起恢复：

```cpp
glUseProgram(program);
glBindVertexArray(vao);
glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
```

`glDrawElements` 的参数含义：

```cpp
void glDrawElements(
    GLenum mode,      // 图元类型：GL_TRIANGLES / GL_LINES / GL_POINTS 等
    GLsizei count,    // 要绘制多少个索引（不是顶点！）
    GLenum type,      // 索引数据类型：GL_UNSIGNED_BYTE / GL_UNSIGNED_SHORT / GL_UNSIGNED_INT
    const void* offset // 从索引缓冲的哪个字节偏移开始画，通常填 0
);
```

---

## 问题 4：EBO 到底"绑在哪里"？

这是 OpenGL 状态机最容易让人困惑的地方之一。我们已经知道：

- `GL_ARRAY_BUFFER` 的绑定是**全局 Context 状态**，VAO **不保存**它。
- `GL_ELEMENT_ARRAY_BUFFER` 的绑定是**VAO 状态的一部分**，会被 VAO 保存。

为什么设计成这样？

因为 `glVertexAttribPointer` 在记录属性配置时，会**隐式地**把当前 `GL_ARRAY_BUFFER` 的绑定捕获到属性状态里。所以 `GL_ARRAY_BUFFER` 的全局绑定只是临时输入，真正持久化的是"属性 0 来自哪个 VBO"这个关系。

但 EBO 没有对应的"格式配置函数"——它不需要解释"索引长什么样"，因为索引就是普通的整数数组。所以 OpenGL 直接把 `GL_ELEMENT_ARRAY_BUFFER` 的绑定本身存进 VAO。

```
VAO 存储的内容（复习）：
┌─────────────────────────────────────┐
│ ✓ glEnableVertexAttribArray 的状态   │
│ ✓ glVertexAttribPointer 的配置       │
│ ✓ 每个属性关联的 VBO                 │
│ ✓ 当前绑定的 GL_ELEMENT_ARRAY_BUFFER │  ← EBO 在这里！
│                                     │
│ ✗ 不存 GL_ARRAY_BUFFER 的当前绑定    │  ← VBO 不在这里
└─────────────────────────────────────┘
```

这意味着：**绘制带 EBO 的物体时，你不需要单独绑定 EBO，只需要绑定 VAO。**

---

## 问题 5：完整的最小示例——用 EBO 画一个彩色立方体

下面是把前面概念串起来的完整示例。为聚焦 EBO，Shader 只做了最基础的 MVP 变换占位（实际 MVP 将在后续 [[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]] 中深入）。

```cpp
// flags: override -std=c++20 -Wall -O2 -lglfw -lGL -lX11 -lpthread -lXrandr -lXi -ldl
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstddef>

struct Vertex {
    float pos[3];
    float color[3];
};

Vertex cubeVertices[] = {
    {{-0.5f,-0.5f,-0.5f}, {1.0f,0.0f,0.0f}}, // 0
    {{ 0.5f,-0.5f,-0.5f}, {0.0f,1.0f,0.0f}}, // 1
    {{ 0.5f, 0.5f,-0.5f}, {0.0f,0.0f,1.0f}}, // 2
    {{-0.5f, 0.5f,-0.5f}, {1.0f,1.0f,0.0f}}, // 3
    {{-0.5f,-0.5f, 0.5f}, {1.0f,0.0f,1.0f}}, // 4
    {{ 0.5f,-0.5f, 0.5f}, {0.0f,1.0f,1.0f}}, // 5
    {{ 0.5f, 0.5f, 0.5f}, {1.0f,1.0f,1.0f}}, // 6
    {{-0.5f, 0.5f, 0.5f}, {0.5f,0.5f,0.5f}}, // 7
};

GLuint cubeIndices[] = {
    4, 5, 6,   4, 6, 7,   // 前
    0, 3, 2,   0, 2, 1,   // 后
    0, 4, 7,   0, 7, 3,   // 左
    1, 2, 6,   1, 6, 5,   // 右
    3, 7, 6,   3, 6, 2,   // 顶
    0, 1, 5,   0, 5, 4    // 底
};

const char* vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
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
    return s;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "EBO 立方体", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // ===== 1. 创建 VBO 并上传 8 个顶点 =====
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // ===== 2. 创建 EBO（暂时不绑定，等 VAO 准备好再记录）=====
    GLuint ebo;
    glGenBuffers(1, &ebo);

    // ===== 3. 创建 VAO，在 VAO 作用域内配置所有顶点状态和 EBO 绑定 =====
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // 3.1 配置顶点属性（glVertexAttribPointer 会捕获当前 GL_ARRAY_BUFFER 的绑定）
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);

    // 3.2 绑定 EBO 并上传索引（这个绑定会被记录到 VAO 里）
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    // ===== 4. 编译 Shader =====
    GLuint program = glCreateProgram();
    glAttachShader(program, compileShader(vsSrc, GL_VERTEX_SHADER));
    glAttachShader(program, compileShader(fsSrc, GL_FRAGMENT_SHADER));
    glLinkProgram(program);

    // ===== 5. 启用深度测试，否则看不到立方体的立体感 =====
    glEnable(GL_DEPTH_TEST);

    // ===== 6. 渲染循环 =====
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}
```

运行这个程序，你会看到一个静止的彩色立方体。因为 Shader 里没有做旋转，所以看起来像是一个扁平的方块——这是正常的。我们目前的重点是验证 EBO 通路正确。

---

## 问题 6：索引该用哪种数据类型？

索引本身只是一个整数。OpenGL 支持三种索引类型：

| 类型 | 每个索引字节数 | 最大顶点数 | 适用场景 |
|------|--------------|-----------|---------|
| `GL_UNSIGNED_BYTE` | 1 | 255 | 极小的模型，比如粒子、调试线框 |
| `GL_UNSIGNED_SHORT` | 2 | 65535 | 大多数中小型模型，是历史最安全的默认值 |
| `GL_UNSIGNED_INT` | 4 | 约 42 亿 | 大型地形、复杂角色模型 |

> **推荐**：个人项目默认用 `GL_UNSIGNED_SHORT`，除非顶点数超过 65535。更小的索引意味着更少的显存占用和更高的缓存命中率。

但这里有一个诚实的 trade-off：现代 GPU 对 32 位索引的处理往往和 16 位一样快，甚至在某些架构上更快。所以如果你的模型顶点数刚好卡在 6 万~7 万附近，与其硬挤 16 位索引，不如直接上 32 位，避免未来模型变大后重新处理。

---

## 问题 7：EBO 真的"消除"了重复顶点吗？

**它只是把顶点复用的问题从 VBO 转移到了索引表。**

VBO 里仍然只存唯一的 8 个顶点，但 EBO 里需要存 36 个索引。索引表本身也占用显存和带宽。

诚实的成本分析：

| 方案 | VBO 大小 | 索引数据大小 | 总显存 |
|------|---------|------------|-------|
| 复制顶点（无 EBO） | 36 × 24 B = 864 B | 0 | 864 B |
| EBO（8 顶点 + 36 索引） | 8 × 24 B = 192 B | 36 × 2 B = 72 B | 264 B |

对于这个小立方体，EBO 节省了约 70% 的顶点显存。但如果顶点本身非常小（比如只有位置，12 字节），而索引用 4 字节，节省就没那么明显。

更重要的是：**EBO 的真正价值不只是省显存，而是让顶点共享成为可能。**

- 如果你做**平滑着色**，同一个顶点可以被多个三角形共享，法线取周围面的平均，曲面看起来光滑。
- 如果你做**硬边着色**，一个角在不同面上需要不同的法线，那就不能共享顶点，EBO 的复用优势会变小。

所以 EBO 不是万能药，它解决的是"顶点可以共享"的场景。

---

## 问题 8：最常见的三个坑

### 坑 1：EBO 没绑进 VAO

```cpp
// ❌ 错误：EBO 在 VAO 外部绑定
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);
glBindVertexArray(0);  // VAO 记录结束

glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);  // 绑定到全局，没进 VAO
```

结果：绘制时 `glDrawElements` 找不到 EBO，要么崩溃，要么画出奇怪的东西。

**正确做法**：EBO 的 `glBindBuffer` 必须在 `glBindVertexArray(vao)` 和 `glBindVertexArray(0)` 之间。

---

### 坑 2：先解绑 EBO，再解绑 VAO

```cpp
// ❌ 错误：先解绑 EBO，会把它从 VAO 里拆掉
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
glBindVertexArray(0);
```

因为 `GL_ELEMENT_ARRAY_BUFFER` 是 VAO 状态的一部分，第一行把当前 VAO 的 EBO 解绑了。下次再绑这个 VAO 绘制时，EBO 已经不在里面。

```cpp
// ✅ 正确：直接解绑 VAO 即可，不要单独解绑 EBO
glBindVertexArray(0);
```

如果你一定要"清理"，正确的顺序是：先解绑 VAO，再解绑 VBO/EBO。但实际上，现代 OpenGL 代码通常不需要在 VAO 配置阶段解绑任何东西。

---

### 坑 3：索引越界

```cpp
// ❌ 错误：索引值大于顶点数
cubeVertices 只有 8 个元素，索引范围是 0~7。
GLuint badIndices[] = { 0, 1, 9 };  // 9 越界！
```

OpenGL 不会因为索引越界报错，但会导致：
- 读取 VBO 后面的垃圾数据
- 画出完全错误的三角形
- 严重时崩溃

调试技巧：在 CPU 端先检查所有索引是否小于 `vertexCount`。

---

## 状态变化图：VBO、EBO、VAO 的完整关系

```
初始化阶段：

CPU 内存
┌─────────────────────────────────────┐
│ cubeVertices[8]  cubeIndices[36]    │
└─────────────────────────────────────┘
         │                   │
         │ glBufferData        │ glBufferData
         ▼                   ▼
GPU 显存
┌─────────────────────┐  ┌─────────────────────┐
│       VBO #1        │  │       EBO #1        │
│  8 个唯一点          │  │  36 个索引           │
│  pos + color        │  │  GLuint[36]         │
└──────────┬──────────┘  └──────────┬──────────┘
           │                        │
           │ glBindBuffer(ARRAY)    │ glBindBuffer(ELEMENT)
           │                        │
           ▼                        ▼
    ┌─────────────────────────────────────────┐
    │                 VAO #1                  │
    │  ┌─────────────────────────────────┐    │
    │  │ 属性 0：pos， 3×float，stride=24 │    │
    │  │ 属性 1：color，3×float，stride=24│    │
    │  │ 数据来源：VBO #1                  │    │
    │  │ 索引来源：EBO #1                  │    │
    │  └─────────────────────────────────┘    │
    └─────────────────────────────────────────┘

绘制阶段：

  glUseProgram(program)
       │
       ▼
  glBindVertexArray(vao)
       │
       ├──► 自动恢复属性配置和 VBO 关联
       └──► 自动恢复 EBO 绑定
       │
       ▼
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0)
       │
       ▼
  顶点着色器 ◄── 属性 0/1 ◄── VAO ◄── VBO #1
       ▲
       │
  索引寻址 ◄── EBO #1（通过 VAO 间接使用）
```

---

## 与现代 API 的对照

我们在解决的是「**如何复用顶点数据并指定图元组装方式**」这个问题。

| 概念 | OpenGL | Vulkan | D3D12 |
|------|--------|--------|-------|
| 索引缓冲 | `GL_ELEMENT_ARRAY_BUFFER` | `VkBuffer` + `vkCmdBindIndexBuffer` | `ID3D12Resource` + `IASetIndexBuffer` |
| 索引绘制 | `glDrawElements` | `vkCmdDrawIndexed` | `DrawIndexedInstanced` |
| 索引类型 | `GL_UNSIGNED_BYTE/SHORT/INT` | `VkIndexType` | `DXGI_FORMAT_R16_UINT / R32_UINT` |
| VAO 记录 EBO | VAO 保存 `GL_ELEMENT_ARRAY_BUFFER` 绑定 | Pipeline 不保存索引缓冲，每次绘制前显式绑定 | Pipeline State 不保存索引缓冲，每次绘制前显式绑定 |

> **个人项目推荐**：学习阶段用 OpenGL 的 EBO 完全够用。向现代 API 迁移时，mentally map 为："EBO ≈ GPU 显存里的一块索引缓冲区，绘制前需要显式绑定到命令缓冲"。

现代 API 之所以不保存索引缓冲绑定，是因为它们希望把"顶点格式"和"具体用哪个索引缓冲"完全分离，方便同一份格式复用到多个网格。

---

## 与 SelfGameEngine 的关系

EBO 是引擎阶段 5.2 "资源管理"中 **Mesh Import → GPU Upload** 链路的第二步。

在 [[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] 里，OBJ 的 `f 1/1/1 2/2/2 3/3/3` 解析出来本质上就是索引数组。现在你知道了：

- `v` 解析出的位置 → VBO
- `f` 解析出的面 → EBO

引擎里通常会封装一个 `GpuMesh` 或 `RHIMesh` 类：

```cpp
struct GpuMesh {
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;
    RHIVertexLayoutRef layout;  // 对应 VAO
    uint32_t indexCount;
    IndexType indexType;
};

// 绘制时
renderDevice->bindPipeline(pipeline);
renderDevice->bindVertexLayout(mesh.layout);
renderDevice->bindVertexBuffer(mesh.vertexBuffer);
renderDevice->bindIndexBuffer(mesh.indexBuffer, mesh.indexType);
renderDevice->drawIndexed(mesh.indexCount);
```

这就是阶段八 [[Notes/计算机图形学/引擎渲染架构/为什么需要渲染抽象|为什么需要渲染抽象]] 要深入的 RHI 封装。现在你先理解 OpenGL 原语，后续再学怎么抽象成跨 API 接口。

---

## 设计 checklist：什么时候用 EBO？

| 场景 | 推荐方案 | 原因 |
|------|---------|------|
| 静态模型（OBJ/FBX 加载） | 用 EBO | 大量顶点可复用，显存和带宽都省 |
| 粒子系统（每个粒子 4 个独立角点） | 不用 EBO，用 `glDrawArrays` | 粒子之间没有共享顶点，EBO 只会增加索引开销 |
| 调试线框/Gizmo | 可选 EBO | 如果复用顶点少，直接用 `glDrawArrays` 更简单 |
| 地形 / 大规模网格 | 必须用 EBO | 顶点复用率高，不用 EBO 显存爆炸 |
| 顶点数 < 256 | EBO + `GL_UNSIGNED_BYTE` | 最小化索引数据 |
| 顶点数 < 65535 | EBO + `GL_UNSIGNED_SHORT` | 默认推荐 |
| 顶点数 > 65535 | EBO + `GL_UNSIGNED_INT` | 避免索引越界 |

---

## 本模块还缺什么？

| 已解决 | 待深入 |
|--------|--------|
| 为什么需要 EBO | 真实模型（OBJ）的顶点去重与索引生成 |
| EBO 创建、绑定、绘制 | MVP 矩阵通过 Uniform 传入（[[Notes/计算机图形学/Shader与光照/Uniform与VertexAttribute|Uniform与VertexAttribute]]） |
| EBO 在 VAO 中的位置 | 多个物体的绘制状态管理 |
| `glDrawElements` 的参数 | 顶点法线共享 vs 硬边法线的顶点分裂策略 |

> **下一步**：[[Notes/计算机图形学/顶点数据与索引/从OBJ文件到GPU网格|从OBJ文件到GPU网格]] — 把 OBJ 解析出来的 `v/vt/vn/f` 转换成 VBO + EBO，真正渲染一个外部模型。

---

> [[Notes/计算机图形学/Roadmap|← 返回 图形学路线图]]
