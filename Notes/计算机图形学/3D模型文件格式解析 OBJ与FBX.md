---
title: 3D 模型文件格式解析：OBJ 与 FBX
date: 2026-04-05
tags:
  - 图形学
  - 3D格式
  - OBJ
  - FBX
aliases:
  - Wavefront OBJ
  - 3D Model Format
---

> [[索引|← 返回 计算机图形学索引]]

# 3D 模型文件格式解析：OBJ 与 FBX

> [!info] 前置知识
> 建议先了解 [[计算机图形学/变换续 - 视图与投影|MVP 变换]]，理解顶点如何在渲染管线中流动。

---

## Why：为什么要学习 3D 文件格式？

- **问题背景**：渲染管线的起点是**几何数据**。无论是写软光栅还是调用 OpenGL/Vulkan，你都需要把三维模型（顶点、面、法线、纹理坐标）送进 GPU。这些数据不会凭空出现，它们通常存储在专门的 3D 模型文件中。
- **不用 X 的后果**：如果你不了解格式结构，遇到模型加载失败、法线错乱、UV 翻转时，只能盲目调试或依赖第三方库的黑箱行为，无法定位是文件问题还是代码问题。
- **应用场景**：
  1. 写自己的离线渲染器或游戏引擎，需要加载自定义模型。
  2. 为美术工具写导出插件或批处理脚本。
  3. 在资源管线中做格式转换、减面、LOD 生成。

---

## What：OBJ 格式是什么？

### 核心定义

**Wavefront OBJ** 是一种由 Alias|Wavefront 公司（现 Autodesk）推出的**纯文本**3D 模型文件格式。它以**人类可读**、**跨平台兼容**和**结构简单**著称，是图形学入门和科研中最常用的格式之一。

### 关键特点

| 特点 | 说明 |
|------|------|
| **纯文本** | 可用任何文本编辑器打开，便于调试和学习 |
| **顶点驱动** | 以顶点列表为中心，面通过索引引用顶点 |
| **支持多边形** | 默认支持三角面、四边面甚至 n-gon |
| **分离材质** | 材质信息存储在独立的 `.mtl` 文件中 |
| **无层级/骨骼** | 不支持动画、骨骼、场景树 |

### OBJ 文件结构详解

OBJ 的语法极其简单：**每一行以一个关键字开头，后跟参数**。

#### 1. 几何数据关键字

| 关键字 | 参数 | 含义 |
|--------|------|------|
| `v` | `x y z [w]` | 顶点坐标（Geometric Vertex），默认 `w=1.0` |
| `vt` | `u v [w]` | 纹理坐标（Texture Vertex） |
| `vn` | `x y z` | 顶点法线（Vertex Normal），通常是单位向量 |
| `vp` | `u v [w]` | 参数空间顶点（Parametric Vertex），用于曲面，极少见 |

#### 2. 面片定义关键字

| 关键字 | 参数 | 含义 |
|--------|------|------|
| `f` | `v1/vt1/vn1 v2/vt2/vn2 ...` | 面（Face），通过索引引用顶点 |
| `l` | `v1 v2 ...` | 线段（Line） |

**面的索引格式**是 OBJ 最核心的语法，有四种形式：

```obj
# 1. 只有顶点索引（法线和纹理坐标需要计算或没有）
f 1 2 3

# 2. 顶点/纹理坐标
f 1/1 2/2 3/3

# 3. 顶点//法线
f 1//1 2//2 3//3

# 4. 顶点/纹理坐标/法线（最完整）
f 1/1/1 2/2/2 3/3/3
```

> [!warning] 索引从 1 开始
> OBJ 的索引是 **1-based**！这在 C/C++ 中必须减 1 才能作为数组下标。这是初学者最容易踩的坑。

> [!warning] 负索引
> OBJ 也支持相对索引（负数），表示从当前顶点列表末尾倒数。例如 `-1` 代表最后一个顶点。手写解析器时需要额外处理。

#### 3. 组和对象关键字

| 关键字      | 含义                                           |
| -------- | -------------------------------------------- |
| `o`      | 对象名称（Object Name）                            |
| `g`      | 组名称（Group Name）                              |
| `s`      | 平滑组（Smoothing Group），`s 1` 表示启用，`s off` 表示关闭 |
| `usemtl` | 使用某个材质名称                                     |
| `mtllib` | 引用外部 `.mtl` 文件                               |

#### 完整 OBJ 示例

```obj
# 一个简单的立方体 OBJ 文件
# 顶点坐标
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 1.0 1.0 0.0
v 0.0 1.0 0.0
v 0.0 0.0 1.0
v 1.0 0.0 1.0
v 1.0 1.0 1.0
v 0.0 1.0 1.0

# 纹理坐标
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

# 法线
vn 0.0 0.0 -1.0
vn 0.0 0.0 1.0
vn 0.0 -1.0 0.0
vn 0.0 1.0 0.0
vn -1.0 0.0 0.0
vn 1.0 0.0 0.0

# 材质引用
mtllib cube.mtl
usemtl default

# 面（顶点/纹理/法线）
f 1/1/1 2/2/1 3/3/1 4/4/1
f 5/1/2 8/4/2 7/3/2 6/2/2
f 1/1/3 5/2/3 6/3/3 2/4/3
f 2/1/6 6/2/6 7/3/6 3/4/6
f 3/1/4 7/2/4 8/3/4 4/4/4
f 4/1/5 8/2/5 5/3/5 1/4/5
```

### MTL 材质文件

MTL（Material Template Library）是与 OBJ 配对的材质描述文件。

#### 常用关键字

| 关键字 | 含义 |
|--------|------|
| `newmtl` | 定义新材质名称 |
| `Ka` | 环境光反射系数（Ambient Color） |
| `Kd` | 漫反射系数（Diffuse Color） |
| `Ks` | 镜面反射系数（Specular Color） |
| `Ns` | 高光指数（Shininess） |
| `d` / `Tr` | 不透明度（Transparency） |
| `map_Kd` | 漫反射纹理贴图路径 |
| `map_Bump` / `bump` | 凹凸/法线贴图路径 |

```mtl
newmtl default
Ka 0.1 0.1 0.1
Kd 0.8 0.2 0.2
Ks 1.0 1.0 1.0
Ns 32.0
d 1.0
map_Kd texture.png
```

---

## What：FBX 等现代格式为什么被提出？

### OBJ 的局限性

随着 3D 产业的发展，OBJ 的简单性反而成了束缚：

| 局限          | 说明                                 |
| ----------- | ---------------------------------- |
| **无动画**     | 不能存储骨骼、蒙皮、关键帧、动作曲线                 |
| **无场景层级**   | 不支持节点树、父子关系、变换继承                   |
| **无材质高级特性** | 不支持 PBR（金属度/粗糙度）、Shader Graph、多层材质 |
| **纯文本效率低**  | 大型场景文件体积极大，解析慢                     |
| **无坐标系规范**  | 不同软件导出的 OBJ 坐标系、缩放单位可能不同           |
| **不支持实例化**  | 同一份网格无法被多个对象引用                     |

### FBX 的崛起

**FBX（Filmbox）** 是 Autodesk 推出的**二进制/ASCII 可选**的 3D 交换格式。它几乎成了行业事实标准，原因如下：

| 特性        | FBX 支持情况                                  |
| --------- | ----------------------------------------- |
| **动画与骨骼** | ✅ 完整支持骨骼层级、蒙皮权重、关键帧动画、Blend Shape         |
| **场景图**   | ✅ 支持完整的节点树（Node Graph）、Transform 继承       |
| **多格式编码** | ✅ 同时支持二进制（体积小、解析快）和 ASCII（可读）             |
| **材质与光照** | ✅ 支持多种光照模型、相机参数、PBR 属性扩展                  |
| **跨软件兼容** | ✅ Maya、3ds Max、Blender、Unity、Unreal 均原生支持 |

> [!note] FBX 的代价
> FBX 的 SDK 是闭源的，且协议限制较多。这促使了开源格式如 **glTF 2.0**（Khronos Group 推出）的兴起，glTF 被称为 "3D 界的 JPEG"，专为 Web 和实时渲染优化。

### 格式演进脉络

```
OBJ (1980s) ──► 3DS/DAE/COLLADA (2000s) ──► FBX (行业主导) 
                                    └────► glTF 2.0 (开源现代标准)
```

| 格式       | 适合场景                 |     |
| -------- | -------------------- | --- |
| **OBJ**  | 学习、科研、静态模型快速交换       |     |
| **FBX**  | 游戏、影视动画、复杂场景管线       |     |
| **glTF** | Web 应用、实时渲染、现代引擎资源导入 |     |

---

## How：如何用 C++ 解析 OBJ？

### 基本思路

OBJ 是纯文本，解析流程非常直观：

1. 逐行读取文件。
2. 按空格分割，识别第一个 token（`v`, `vt`, `vn`, `f` 等）。
3. 将几何数据 push 到临时数组。
4. 遇到 `f` 时，解析索引并构建三角形（如果是四边面，拆成两个三角形）。
5. （可选）按索引展开顶点，构造 Vertex Buffer。

### 极简 OBJ 解析器

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };

struct Vertex {
    Vec3 position;
    Vec2 texcoord;
    Vec3 normal;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

bool LoadOBJ(const std::string& path, Mesh& mesh) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << path << std::endl;
        return false;
    }

    std::vector<Vec3> temp_positions;
    std::vector<Vec2> temp_texcoords;
    std::vector<Vec3> temp_normals;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vec3 v{};
            iss >> v.x >> v.y >> v.z;
            temp_positions.push_back(v);
        }
        else if (prefix == "vt") {
            Vec2 vt{};
            iss >> vt.u >> vt.v;
            temp_texcoords.push_back(vt);
        }
        else if (prefix == "vn") {
            Vec3 vn{};
            iss >> vn.x >> vn.y >> vn.z;
            temp_normals.push_back(vn);
        }
        else if (prefix == "f") {
            // 支持 f v/vt/vn v/vt/vn v/vt/vn ...
            std::vector<Vertex> faceVerts;
            std::string vertexStr;
            while (iss >> vertexStr) {
                std::istringstream vss(vertexStr);
                std::string indexStr;
                int vIdx = 0, vtIdx = 0, vnIdx = 0;

                std::getline(vss, indexStr, '/');
                vIdx = std::stoi(indexStr);

                if (std::getline(vss, indexStr, '/')) {
                    if (!indexStr.empty())
                        vtIdx = std::stoi(indexStr);
                    if (std::getline(vss, indexStr, '/')) {
                        vnIdx = std::stoi(indexStr);
                    }
                }

                // OBJ 是 1-based 索引
                Vertex vert{};
                vert.position = temp_positions[vIdx - 1];
                if (vtIdx > 0) vert.texcoord = temp_texcoords[vtIdx - 1];
                if (vnIdx > 0) vert.normal   = temp_normals[vnIdx - 1];
                faceVerts.push_back(vert);
            }

            // 三角化：f 1 2 3 4 -> (1,2,3), (1,3,4)
            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                mesh.vertices.push_back(faceVerts[0]);
                mesh.vertices.push_back(faceVerts[i]);
                mesh.vertices.push_back(faceVerts[i + 1]);
            }
        }
    }

    // 这里采用 non-indexed 布局，indices 可顺序生成
    mesh.indices.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.indices.size(); ++i)
        mesh.indices[i] = static_cast<unsigned int>(i);

    return true;
}

int main() {
    Mesh mesh;
    if (LoadOBJ("cube.obj", mesh)) {
        std::cout << "Loaded " << mesh.vertices.size() << " vertices.\n";
    }
    return 0;
}
```

### 最佳实践

| 实践        | 说明                                                                          |
| --------- | --------------------------------------------------------------------------- |
| **三角化**   | 渲染管线通常只接受三角形。在解析阶段就把四边面/n-gon 拆成三角形。                                        |
| **索引去重**  | 如果面频繁复用相同的 `v/vt/vn` 组合，可以用 `std::map` 做顶点去重，生成 Index Buffer，减少 GPU 顶点数据冗余。 |
| **预计算法线** | 如果 OBJ 中没有 `vn`，可以通过叉积计算面法线，再用角度加权平均到顶点。                                    |
| **路径处理**  | `mtllib` 和 `map_Kd` 中的路径可能是相对路径，解析时应相对于 OBJ 文件所在目录查找。                       |

### 常见陷阱

| 陷阱             | 解决方案                                                        |
| -------------- | ----------------------------------------------------------- |
| **1-based 索引** | 所有索引减 1 再访问数组                                               |
| **面只有顶点索引**    | `f 1 2 3` 没有 `vt` 和 `vn`，解析时要容错                             |
| **双面材质与法线翻转**  | 某些建模软件导出时会翻转法线或改变坐标系手性                                      |
| **四边面/多边形**    | 不能直接当三角形送 GPU，必须先三角化                                        |
| **大文件性能**      | 超百万顶点的 OBJ 用 `std::stoi` 逐字符解析会很慢，可考虑使用 `fast_float` 等快速解析库 |

---

## 总结

| 对比项      | OBJ          | FBX         |
| -------- | ------------ | ----------- |
| **可读性**  | 纯文本，极易阅读     | 二进制为主，难阅读   |
| **动画支持** | ❌ 无          | ✅ 完整        |
| **场景层级** | ❌ 扁平         | ✅ 树状节点      |
| **文件体积** | 大            | 小           |
| **解析难度** | 极易（几十行代码）    | 必须用 SDK（复杂） |
| **适用场景** | 学习、静态模型、轻量交换 | 游戏、影视、动画管线  |

**一句话记忆**：
> **OBJ 是 3D 格式的 "Hello World"，简单、开放、无动画；FBX 是工业级的 "瑞士军刀"，复杂、强大、覆盖全管线。**

如果你正在写自己的渲染器，从解析 OBJ 开始是最务实的选择；当你需要骨骼动画、场景树、材质球时，再引入 FBX 或 glTF。

---

## 延伸阅读

- [[计算机图形学/变换续 - 视图与投影|MVP 变换]]
- [[计算机图形学/5.  光栅化Rasterization(Triangles)|光栅化]]
- Wavefront OBJ 规范（原版文档，Autodesk 官网）
- Khronos glTF 2.0 规范
