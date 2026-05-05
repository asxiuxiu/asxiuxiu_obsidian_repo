---
title: 3D模型文件格式解析——OBJ与FBX
description: 模型数据在文件中怎么存储？如何解析OBJ格式，提取顶点、法线、UV和索引，为后续GPU上传做准备。
date: 2026-03-29
tags:
  - graphics
  - model
  - obj
  - fbx
aliases:
  - OBJ Format
  - FBX Format
---

> **前置依赖**：[[Notes/计算机图形学/像素与渲染管线全貌/像素、帧缓冲与渲染管线全貌|像素、帧缓冲与渲染管线全貌]] — 你已经理解 3D 模型由顶点、面片、法线、UV 构成
> **本模块增量**：你能独立解析 OBJ 文件，提取顶点位置、法线、纹理坐标和索引数据。
> **下一步**：[[Notes/计算机图形学/Roadmap|Roadmap]] — 进入 GPU 编程阶段，把解析后的数据上传到 GPU（VBO/EBO）。

---

# 3D模型文件格式解析——OBJ与FBX

## 问题 0：3D 模型数据在文件中怎么存？

在 [[Notes/计算机图形学/像素与渲染管线全貌/像素、帧缓冲与渲染管线全貌|像素、帧缓冲与渲染管线全貌]] 中，我们知道 3D 模型 = 几何数据 + 外观数据。但这些东西在文件里到底是什么格式？

**最 naive 的方案**：自己定义一个二进制格式。

**发现的问题**：
- 需要写导出插件（Blender/Maya 不支持你的格式）
- 不同工具之间的互操作性为零
- 美术师无法直接使用

### 改进：使用业界标准格式

| 格式 | 特点 | 适用场景 |
|------|------|---------|
| **OBJ** | 纯文本、人类可读、极简 | 学习阶段、小型项目 |
| **FBX** | 二进制、功能丰富、业界标准 | 生产环境、复杂动画 |
| **glTF** | 现代标准、JSON+二进制、Web友好 | 新兴项目、WebGL |

> 学习阶段首选 OBJ——它是纯文本，可以用记事本打开看内容，几行代码就能解析。

---

## 问题 1：OBJ 格式的结构

OBJ 文件由一系列**文本行**组成，每行以一个关键字开头：

```obj
# 这是一个注释
v 1.0 0.0 0.0       # 顶点位置（Vertex position）
v 0.0 1.0 0.0
v 0.0 0.0 1.0

vn 1.0 0.0 0.0      # 顶点法线（Vertex normal）
vt 0.5 0.5          # 纹理坐标（Vertex texture coord）

f 1/1/1 2/2/1 3/3/1 # 面（Face）：三个顶点的 位置/UV/法线 索引
```

### 关键字详解

| 关键字 | 含义 | 示例 |
|--------|------|------|
| `v` | 顶点位置 | `v 1.0 0.0 0.0` |
| `vt` | 纹理坐标 | `vt 0.5 0.5` |
| `vn` | 顶点法线 | `vn 0.0 1.0 0.0` |
| `f` | 面片（索引） | `f 1/1/1 2/2/1 3/3/1` |
| `#` | 注释 | `# 这是一个立方体` |

### 面片索引的格式

```
f 1/1/1 2/2/1 3/3/1
  │ │ │  │ │ │  │ │ │
  │ │ │  │ │ │  └── 第三个顶点的法线索引
  │ │ │  │ │ └───── 第三个顶点的 UV 索引
  │ │ │  │ └─────── 第三个顶点的位置索引
  │ │ │  └───────── ...
  │ │ └──────────── 第一个顶点的法线索引
  │ └────────────── 第一个顶点的 UV 索引
  └──────────────── 第一个顶点的位置索引
```

索引从 **1** 开始（不是 0！）。如果某个分量为空，表示没有该属性：

```obj
f 1//1 2//1 3//1    # 没有 UV：位置索引//法线索引
f 1 2 3             # 只有位置索引
```

---

## 问题 2：怎么解析 OBJ 文件？

### 最 naive 的方案：逐行读取，字符串分割

```cpp
#include <fstream>
#include <sstream>
#include <vector>

struct Vertex {
    float x, y, z;
};

struct Mesh {
    std::vector<Vertex> positions;    // v
    std::vector<Vertex> normals;      // vn
    std::vector<std::pair<float,float>> uvs;  // vt
    std::vector<unsigned int> indices;        // f
};

Mesh parseOBJ(const char* filename) {
    Mesh mesh;
    std::ifstream file(filename);
    std::string line;
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        
        if (prefix == "v") {
            float x, y, z;
            iss >> x >> y >> z;
            mesh.positions.push_back({x, y, z});
        }
        else if (prefix == "vt") {
            float u, v;
            iss >> u >> v;
            mesh.uvs.push_back({u, v});
        }
        else if (prefix == "vn") {
            float x, y, z;
            iss >> x >> y >> z;
            mesh.normals.push_back({x, y, z});
        }
        else if (prefix == "f") {
            // 解析面片索引
            for (int i = 0; i < 3; i++) {
                std::string vertexData;
                iss >> vertexData;
                
                // 格式：pos/uv/normal
                size_t slash1 = vertexData.find('/');
                size_t slash2 = vertexData.find('/', slash1 + 1);
                
                int posIndex = std::stoi(vertexData.substr(0, slash1)) - 1;
                mesh.indices.push_back(posIndex);
            }
        }
    }
    
    return mesh;
}
```

### 发现的问题：索引不连续

OBJ 文件使用**顶点索引复用**——同一个几何顶点可能在多个面中出现。但现代 GPU 的 VBO 要求每个顶点的属性（位置+法线+UV）是**连续存储**的。

**解决方案**：将 "顶点索引 + 法线索引 + UV索引" 的三元组作为唯一键，构建**顶点去重表**。

```cpp
// 简化版：假设每个面的顶点都有独立的 位置/UV/法线
// 实际上需要建立 (posIdx, uvIdx, normalIdx) -> newIndex 的映射
```

---

## 问题 3：解析后的数据怎么给 GPU 用？

解析 OBJ 后，你得到的是：
- `positions[]`：顶点位置数组
- `normals[]`：法线数组
- `uvs[]`：纹理坐标数组
- `indices[]`：面片索引数组

在 GPU 上，你需要：

```cpp
// 1. 构建交织的顶点数据（位置 + 法线 + UV）
struct Vertex {
    float px, py, pz;  // 位置
    float nx, ny, nz;  // 法线
    float u, v;        // UV
};
std::vector<Vertex> vertices;
std::vector<unsigned int> indices;

// 2. 上传到 VBO（见 [[Notes/计算机图形学/顶点数据与索引/从顶点数组到VBO|从顶点数组到VBO]]）
// 3. 用 EBO 绘制（见 [[Notes/计算机图形学/顶点数据与索引/索引缓冲EBO|索引缓冲EBO]]）
```

---

## 问题 4：OBJ 的局限性，为什么需要 FBX？

OBJ 是纯文本格式，有诸多限制：

| 限制 | 说明 |
|------|------|
| 不支持动画 | 只有静态几何，没有骨骼、关键帧 |
| 不支持材质 | 只有简单的 MTL 材质文件，不支持 PBR |
| 不支持场景层级 | 没有父子关系、没有变换层级 |
| 文件体积大 | 纯文本，相同数据量比二进制大 5~10 倍 |

**FBX** 是 Autodesk 开发的二进制格式，支持：
- 骨骼动画和蒙皮
- 复杂材质和纹理引用
- 场景层级和变换
- 摄像机和灯光

> 学习阶段用 OBJ 足够。生产环境中，现代引擎通常使用 glTF（Khronos 标准）或自研格式。

---

## 本模块还缺什么？

| 已解决 | 待实践 |
|--------|--------|
| OBJ 格式结构 | 实际解析一个复杂模型 |
| 基础解析代码 | 顶点去重和交织布局 |
| 数据如何给 GPU | VBO/EBO 上传（阶段2） |

> **下一步**：[[Notes/计算机图形学/Roadmap|Roadmap]] — 按照路线图继续 GPU 编程和顶点数据管理的学习。
