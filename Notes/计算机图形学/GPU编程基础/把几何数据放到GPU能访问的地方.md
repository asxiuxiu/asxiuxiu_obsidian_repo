---
title: 把几何数据放到 GPU 能访问的地方
description: 软渲染器的顶点数据在 std::vector 里。GPU 无法直接读取 CPU 内存。理解为什么需要把几何数据放到 GPU 能访问的显存里，以及不同 API 如何表达这件事。
date: 2026-07-07
tags:
  - graphics
  - vbo
  - vertex-buffer
  - gpu-memory
  - vertex-data
aliases:
  - 顶点数据上 GPU
  - GPU 可访问的顶点数据
  - Vertex Buffer
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/软光栅化与3D数学/光栅化：从直线到三角形|光栅化：从直线到三角形]] — 你的软渲染器从 `std::vector<Vertex>` 读取顶点数据
> - [[Notes/计算机图形学/GPU编程基础/GPU的并行执行模型|GPU 的并行执行模型]] — 你理解 GPU 是并行处理器
>
> **本模块增量**：你能解释为什么顶点数据需要常驻 GPU 显存，能说清"每帧从 CPU 上传到 GPU"的代价，能对比不同 API 如何表达"GPU 可访问的顶点数据"这个概念。
>
> **下一步**：[[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]] — 数据放到 GPU 上了，但 GPU 看到的只是字节流，怎么知道每个顶点的格式？

---

# 把几何数据放到 GPU 能访问的地方

## 问题 0：软渲染器的顶点数据在哪？

在软渲染器里，顶点数据长这样：

```cpp
struct Vertex {
    Vec3 position;
    Vec3 color;
    Vec2 uv;
};

std::vector<Vertex> vertices = {
    { {-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
    { { 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
    { { 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f} },
};
```

这个 `vertices` 数组在你的 C++ 程序内存里。软渲染器的 `for` 循环直接读取它。

现在 GPU 要并行处理这些顶点。问题来了：**GPU 怎么拿到这些数据？**

---

## 问题 1：最 naive 的想法：每次绘制都从 CPU 传一遍

既然 GPU 不能直接读 CPU 内存，那每次绘制时把 `vertices` 从 CPU 拷贝到 GPU 不就行了？

```cpp
while (running) {
    // 每帧都把顶点数据上传到 GPU
    upload_to_gpu(vertices);
    draw_triangles();
}
```

**问题在哪里？**

1. **CPU-GPU 带宽有限**：每帧都要 `memcpy` 几 MB 数据通过 PCIe，通道很快饱和。
2. **延迟**：上传本身需要时间，GPU 还要等数据到了才能开始处理。
3. **重复工作**：如果顶点数据本帧没变化，为什么还要传？

对于一个只有 3 个顶点的三角形，这个开销几乎感觉不到。但对于一个 10 万个顶点的模型，每帧上传就是几 MB，帧率会暴跌。

---

## 问题 2：更好的想法：数据常驻 GPU 显存

核心思想：**一次性上传，反复绘制**。

```
CPU 内存：
  std::vector<Vertex> vertices  ← 只保留一份源数据

CPU → GPU（一次）：
  上传 vertices 到 GPU 显存

GPU 显存：
  GPU 顶点缓冲区  ← 绘制时 GPU 直接读取

每帧绘制：
  GPU 直接读取 GPU 顶点缓冲区
```

这样，绘制时不需要 CPU 参与，GPU 直接从自己的显存里读取顶点数据。

---

## 问题 3：GPU 可访问的顶点数据叫什么名字？

不同 API 对这个概念的命名不同：

| API | 对象名 | 本质 |
|---|---|---|
| **OpenGL** | `Buffer`（通常叫 VBO，Vertex Buffer Object） | GPU 显存里的一块数据 |
| **Vulkan** | `VkBuffer` | GPU 显存里的一块数据 |
| **D3D12** | `ID3D12Resource` | GPU 显存里的一块数据 |
| **Metal** | `MTLBuffer` | GPU 显存里的一块数据 |

**注意**：VBO 只是 OpenGL 的叫法，不是通用概念。在 Vulkan 里你可以叫它"顶点缓冲区"，类型是 `VkBuffer`。

---

## 问题 4：OpenGL 怎么表达这件事？

```cpp
float vertices[] = {
    // 位置              // 颜色
    -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f
};

GLuint vbo;
glGenBuffers(1, &vbo);                           // 创建缓冲区对象
glBindBuffer(GL_ARRAY_BUFFER, vbo);              // 把它设为当前 ARRAY_BUFFER
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),  // 上传数据
             vertices, GL_STATIC_DRAW);          // GL_STATIC_DRAW = 数据基本不变
```

上传完成后，CPU 端的 `vertices[]` 就可以释放了（除非你还要用）。

---

## 问题 5：`GL_STATIC_DRAW`、`GL_DYNAMIC_DRAW`、`GL_STREAM_DRAW` 是什么意思？

这是 OpenGL 的**使用提示（Usage Hint）**，告诉驱动你打算怎么用这个缓冲区：

| 提示 | 含义 | 场景 |
|---|---|---|
| `GL_STATIC_DRAW` | 数据设置一次，绘制多次 | 静态模型、地形 |
| `GL_DYNAMIC_DRAW` | 数据会频繁更新，绘制多次 | 骨骼动画蒙皮后的顶点 |
| `GL_STREAM_DRAW` | 数据每帧都更新，每帧都绘制 | 粒子系统、动态生成的几何 |

这只是提示，不是强制。驱动可能会据此决定把缓冲区放在哪种显存里。

---

## 问题 6：Vulkan / D3D12 / Metal 怎么表达同一件事？

### Vulkan

```cpp
// 1. 创建 VkBuffer
VkBufferCreateInfo info{};
info.size = sizeof(vertices);
info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
vkCreateBuffer(device, &info, nullptr, &buffer);

// 2. 分配显存并绑定
VkMemoryRequirements memReq;
vkGetBufferMemoryRequirements(device, buffer, &memReq);
// ... 分配 device memory ...
vkBindBufferMemory(device, buffer, memory, 0);

// 3. 把数据拷入 staging buffer，再提交到 GPU-only memory
```

Vulkan 把"创建缓冲区"和"分配/绑定内存"分开了，并且通常需要用 Staging Buffer 中转。

### D3D12

```cpp
// 创建上传堆 + 默认堆
ID3D12Resource* uploadBuffer;
ID3D12Resource* vertexBuffer;
device->CreateCommittedResource(
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
    D3D12_HEAP_FLAG_NONE,
    &CD3DX12_RESOURCE_DESC::Buffer(size),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&uploadBuffer));
// ... 拷贝数据，再转换到默认堆 ...
```

D3D12 区分 `UPLOAD` 堆（CPU 可写、GPU 可读）和 `DEFAULT` 堆（GPU 最快访问）。

### Metal

```cpp
MTLBuffer* buffer = [device newBufferWithBytes:vertices
                                        length:sizeof(vertices)
                                       options:MTLResourceStorageModeShared];
```

Metal 的 `Shared` 模式让 CPU 和 GPU 都能访问同一块内存（在 Apple 集成 GPU 上常见）。

---

## 问题 7：为什么这些 API 的"数据对象"和"内存管理"设计不同？

| API | 数据对象 | 内存可见性 | 设计哲学 |
|---|---|---|---|
| OpenGL | `Buffer` | 驱动隐式管理 | 隐藏内存细节，学习曲线平缓 |
| Vulkan | `VkBuffer` + 显式内存分配 | 开发者显式管理 | 暴露内存类型，支持精细优化 |
| D3D12 | `ID3D12Resource` + Heap | 开发者显式管理 | 与 D3D12 显式资源状态模型一致 |
| Metal | `MTLBuffer` + StorageMode | 可选 Shared/Private/Managed | Apple 统一内存架构下的灵活选择 |

**核心差异**：OpenGL 把"数据在哪、怎么搬"藏起来了；现代 API 让你显式决定数据放在 CPU 可见内存、GPU 显存、还是共享内存里。

---

## 问题 8：数据放到 GPU 上之后，还没解决的问题是什么？

顶点数据现在在 GPU 显存里了，但 GPU 看到的只是一块字节流：

```
GPU 显存里的字节流：
[-0.5 -0.5 0.0 1.0 0.0 0.0 0.5 -0.5 0.0 0.0 1.0 0.0 0.0 0.5 0.0 0.0 0.0 1.0]
```

GPU 不知道：
- 前 3 个 float 是位置
- 中间 3 个 float 是颜色
- 后面可能还有 UV

这就是下一篇要解决的问题：**告诉 GPU 顶点数据长什么样**。

---

## 本模块还缺什么？

| 已建立 | 待实践 |
|---|---|
| 顶点数据需要常驻 GPU 显存 | GPU 怎么解释字节流里的顶点格式 |
| 各 API 如何表达"GPU 可访问的顶点数据" | 顶点属性布局（stride/offset/interleaving） |
| 上传 vs 常驻的 trade-off | 多个顶点缓冲区怎么管理 |

---

> **下一步**：[[Notes/计算机图形学/GPU编程基础/告诉GPU顶点数据长什么样|告诉 GPU 顶点数据长什么样]]
>
> 数据放到 GPU 上了，但 GPU 看到的只是字节流。下一篇解决：怎么让 GPU 知道"前 3 个 float 是位置，后 3 个是颜色"？

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
