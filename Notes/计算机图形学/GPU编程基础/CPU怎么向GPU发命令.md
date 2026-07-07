---
title: CPU 怎么向 GPU 发命令
description: CPU 和 GPU 是两个独立处理器，不共享内存和状态。理解程序如何通过图形后端/图形 API 与 GPU 建立会话、发送命令、同步结果。
date: 2026-07-07
tags:
  - graphics
  - graphics-backend
  - render-context
  - swapchain
  - command-buffer
aliases:
  - 图形后端
  - 命令通道
  - 渲染上下文
---

> [[Notes/计算机图形学/GPU编程基础/索引|← 返回 GPU 编程基础索引]]
> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]

> **前置依赖**：
> - [[Notes/计算机图形学/软光栅化与3D数学/3D光栅化与深度缓冲|3D光栅化与深度缓冲]] — 你的软渲染器代码全部在 CPU 上按顺序执行
> - [[Notes/计算机图形学/GPU编程基础/软渲染器到GPU管线的映射|软渲染器到 GPU 管线的映射]] — 你知道软渲染器的每一步对应 GPU 管线的哪个阶段
>
> **本模块增量**：你能解释 CPU 和 GPU 为什么不直接共享内存/状态，程序如何通过渲染上下文、命令通道、交换链与 GPU 建立会话，以及 OpenGL 的隐式状态机与 Vulkan/D3D12/Metal 的显式命令模型有何不同。
>
> **下一步**：[[Notes/计算机图形学/GPU编程基础/GPU的并行执行模型|GPU 的并行执行模型]] — GPU 能接收命令了，但它为什么能同时处理几千个像素？

---

# CPU 怎么向 GPU 发命令

## 问题 0：CPU 和 GPU 为什么不能直接共享代码和内存？

在软渲染器里，你的 C++ 代码直接操作 `std::vector<Pixel>`。这个数组在 CPU 内存里，CPU 可以任意读写。

GPU 是另一个物理芯片：
- 它有自己的显存（VRAM），不在 CPU 内存里
- 它有自己的指令集和调度机制
- 它通过 PCIe 总线与 CPU 连接

所以，CPU 不能直接调用 GPU 的函数，也不能直接读写 GPU 的内存。它们之间需要一个**中间层**来翻译请求。

---

## 问题 1：最 naive 的想法：CPU 直接写 GPU 显存有什么问题？

"我能不能直接把顶点数组和帧缓冲映射到 GPU 显存，然后 CPU 一边算一边写？"

**问题 A：不同 GPU 的显存布局完全不同**

NVIDIA、AMD、Intel、Apple 的 GPU，显存格式、对齐要求、颜色空间转换规则都不一样。你不可能为每款显卡分别写填充代码。

**问题 B：PCIe 延迟高**

CPU 逐个像素地通过 PCIe 写 GPU 显存，延迟极高。软渲染器每帧要访问几百万次像素，这种方式会让 CPU-GPU 通道彻底堵死。

**问题 C：同步问题**

CPU 写显存时，GPU 可能正在读取同一帧缓冲显示到屏幕。如果两者不同步，会出现**画面撕裂**——上半部分是旧帧，下半部分是新帧。

**结论**：不能让每个程序直接操作 GPU 硬件，必须有一个标准化中间层。

---

## 问题 2：图形后端 / 图形 API 解决什么问题？

这个中间层就是**图形后端**（Graphics Backend），也叫**图形 API**（Graphics API）。它向上提供统一的"让 GPU 做事情"的接口，向下屏蔽不同 GPU 驱动的差异。

> [!info] 术语说明
> 在这篇笔记里，**"图形后端"和"图形 API"基本同义**，指 OpenGL、Vulkan、D3D12、Metal 这类标准接口。
>
> 它们由两部分组成：
> - **API 规范**：定义你能调用哪些函数（如 Khronos 定义 Vulkan，Microsoft 定义 D3D12）。
> - **驱动实现**：GPU 厂商根据规范写的驱动，负责把 API 调用翻译成自家 GPU 能懂的指令。
>
> 在引擎语境里，你还会遇到 **RHI（Rendering Hardware Interface）**。RHI 是引擎自己封装的抽象层，下面是 OpenGL/Vulkan/D3D12 这些具体"图形后端实现"。注意区分这两个"后端"的用法。

图形后端的核心职责只有三件：

1. **申请渲染上下文（Render Context）**
   - 告诉操作系统和 GPU："这个窗口要用来渲染。"

2. **管理交换链（Swap Chain）**
   - 解决画面撕裂。GPU 维护两个帧缓冲：前台缓冲（正在显示）和后台缓冲（正在画）。画完后交换角色。

3. **提供最小绘制能力**
   - 哪怕只是"把屏幕清成深灰色"，也是一种绘制能力。

---

## 问题 3：渲染上下文到底是什么？

"渲染上下文"这个词很抽象。把它拆开，它是你的 C++ 程序在 GPU 上获得的一个**工作空间**，包含四样东西：

1. **一个渲染目标**：窗口表面或交换链，GPU 画完的东西最终显示到哪里。
2. **一个 GPU 设备连接**：你的程序在用哪张显卡、哪个计算队列。
3. **一组当前状态**：OpenGL 是全局状态机，上下文就是"当前生效的那套状态"。
4. **一个命令提交通道**：CPU 把绘制命令交给 GPU 的管道。

可以这样类比：渲染上下文就像工厂里的一个**工位**——你有工位许可证、固定工作台、当前调好的工具，以及把任务递给工人的窗口。

在 OpenGL 里：
```cpp
glfwCreateWindow(...)           // 创建窗口 + 上下文
glfwMakeContextCurrent(...)     // 把当前线程和上下文绑定
```

在 Vulkan 里，同样的目的被拆成显式链条：
```cpp
VkInstance → VkPhysicalDevice → VkDevice → VkSurfaceKHR
```

**不是做的事情变多了，而是原本隐藏起来的上下文细节被暴露出来了。**

---

## 问题 4：窗口表面（Surface）和帧缓冲是什么关系？

窗口表面不是窗口本身。窗口由操作系统管理（位置、大小、边框）；窗口表面是操作系统和 GPU 之间的**交接约定**：

> "这个窗口的这块矩形区域，专门用来显示 GPU 渲染出来的图像。"

```
GPU 显存里的帧缓冲  ← GPU 在这里画画
        ↓
   交换链（Swap Chain）  ← 管理"前台缓冲 / 后台缓冲"的切换
        ↓
   窗口表面（Surface）    ← 窗口和 GPU 的交接区
        ↓
   屏幕上的窗口（Window） ← 用户最终看到的东西
```

窗口表面本身**不存放像素**，它只是声明"这个窗口可以接收 GPU 输出"。真正的像素数据存放在帧缓冲里。

OpenGL 通过 GLFW 把 Surface 创建藏在 `glfwCreateWindow` 里。Vulkan 则要求你显式创建 `VkSurfaceKHR`，并提供平台相关的窗口句柄（Windows 上是 `HWND`，Linux 上是 X11 窗口 ID，macOS 上是 `CALayer`）。

---

## 问题 5：交换链和双缓冲解决什么问题？

如果没有双缓冲，GPU 一边画新帧，显示器一边读旧帧，就会出现撕裂。

**双缓冲**：
- **前台缓冲**：当前正在显示到屏幕
- **后台缓冲**：你正在画
- 画完后**交换**两者角色

OpenGL 里，`glfwSwapBuffers()` 帮你隐式管理。Vulkan 里，你必须显式处理：
```cpp
vkAcquireNextImageKHR(...)   // 获取下一帧可画图像
// ... 记录渲染命令 ...
vkQueuePresentKHR(...)       // 呈现到屏幕
```

---

## 问题 6：命令缓冲 vs 立即模式：CPU 怎么把命令发给 GPU？

OpenGL 是**立即模式-ish**的 API：
```cpp
glClear(GL_COLOR_BUFFER_BIT);
glUseProgram(shader);
glBindVertexArray(vao);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

这些调用会立即修改驱动里的全局状态机。驱动在后台把它们翻译成 GPU 命令，但上层感受不到。

Vulkan/D3D12/Metal 是**命令缓冲（Command Buffer）**模型：
```cpp
vkBeginCommandBuffer(cmdBuf);
// 记录命令...
vkCmdClearColorImage(cmdBuf, ...);
vkCmdBindPipeline(cmdBuf, ...);
vkCmdDraw(cmdBuf, ...);
vkEndCommandBuffer(cmdBuf);

vkQueueSubmit(queue, cmdBuf, fence);  // 批量提交
```

**区别**：

| 特性 | OpenGL | Vulkan/D3D12/Metal |
|---|---|---|
| 命令提交方式 | 隐式，驱动实时翻译 | 显式记录到 Command Buffer，批量提交 |
| 状态管理 | 全局状态机 | PSO / Pipeline State Object |
| 多线程 | 困难（上下文线程绑定） | 原生支持多线程录制 |
| 驱动开销 | 高（驱动做很多推断） | 低（开发者显式声明） |

---

## 问题 7：现代 API 为什么把"描述"和"执行"分开？

在 Vulkan 里，你需要先描述：
- 渲染过程（RenderPass）：有哪些附件、加载/存储方式、子 Pass 依赖
- 管线（Pipeline）：顶点格式、Shader、深度测试、混合模式

然后再执行：
- 绑定管线、绑定资源、调用 `vkCmdDraw`

这样做的好处：
1. **驱动可以提前验证**：提交前就知道整套状态是否合法
2. **支持多线程录制**：多个线程各自录 Command Buffer，最后批量提交
3. **CPU 和 GPU 并行**：CPU 准备第 N+1 帧，GPU 执行第 N 帧

---

## 问题 8：RHI 是干什么用的？

学会了 OpenGL，为什么还要在阶段七封装 RHI？

因为直接调用 OpenGL 有三个工程陷阱：
1. **全局状态机**：状态隐式且全局，100 个物体时很难追踪
2. **换 API 要重写**：所有 `gl*` 调用都要换成 `vk*` 或 `d3d*`
3. **多线程渲染困难**：OpenGL 上下文线程绑定

RHI 抽象层把具体 API 调用封装在统一接口后面，上层代码完全无感知。

> **阶段七才会深入讲 RHI**。现在只需要建立预期：阶段二~六写的裸 API 代码，最终会被阶段七的 RHI 层"吸收"和"替代"。

---

## 各 API 表达方式对照

| 子问题 | OpenGL | Vulkan | D3D12 | Metal |
|---|---|---|---|---|
| 建立 GPU 会话 | `glfwCreateWindow` + `glfwMakeContextCurrent` | `VkInstance` → `VkDevice` | `CreateDXGIFactory` → `ID3D12Device` | `MTLCreateSystemDefaultDevice` |
| 连接窗口 | 隐式在 GLFW 里 | `VkSurfaceKHR` | `IDXGISwapChain` | `CAMetalLayer` |
| 交换链 | `glfwSwapBuffers` | `VkSwapchainKHR` | `IDXGISwapChain` | `MTLDrawable` |
| 清屏 | `glClearColor` + `glClear` | `vkCmdClearColorImage` | `ClearRenderTargetView` | `MTLRenderCommandEncoder` 的 `clearColor` |
| 记录命令 | 隐式 | `VkCommandBuffer` | `ID3D12CommandList` | `MTLCommandBuffer` |
| 提交命令 | 隐式 | `vkQueueSubmit` | `ExecuteCommandLists` | `MTLCommandBuffer.commit` |

---

## 本模块还缺什么？

| 已建立 | 待实践 |
|---|---|
| CPU-GPU 不共享内存/状态 | GPU 具体怎么并行执行命令 |
| 渲染上下文、交换链、命令缓冲的概念 | 顶点数据怎么放到 GPU 能访问的地方 |
| OpenGL 隐式 vs Vulkan/D3D12/Metal 显式 | Shader 怎么写、怎么编译 |

---

> **下一步**：[[Notes/计算机图形学/GPU编程基础/GPU的并行执行模型|GPU 的并行执行模型]]
>
> CPU 已经知道怎么向 GPU 发命令了。但 GPU 收到命令后，为什么能同时处理几千个像素？它和普通 CPU 多线程有什么区别？

> [[Notes/计算机图形学/Roadmap|← 返回 Graphics Journey 路线图]]
