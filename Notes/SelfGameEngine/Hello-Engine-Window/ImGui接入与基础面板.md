---
title: ImGui接入与基础面板
date: 2026-05-03
tags:
  - self-game-engine
  - imgui
  - gui
  - debug
  - docking
aliases:
  - ImGui集成
  - 调试面板
  - 基础面板
---

> [[0_RoadMap|← 返回 SelfGameEngine 构建手册]]

> **前置依赖**：[[最简图形后端]]
> **本模块增量**：引擎获得了在屏幕上绘制调试面板的能力——窗口、按钮、文本、滑块、颜色选择器。这是后续所有可视化工具（[[可视化日志系统]]、ECS Inspector、性能分析器）的渲染基础，也是编辑器框架的雏形。
> **下一步**：[[可视化日志系统]] — 图形后端 + ImGui 就位后，建立异步日志核心和多端输出，让引擎从"黑框程序"升级为"可观测系统"。

---

# ImGui 接入与基础面板

## 问题 0：为什么需要 GUI？

到这一步，你的引擎已经能弹出一个深灰色窗口。但窗口里什么都没有——没有按钮、没有文本、没有可以拖动的面板。

这意味着你所有的调试手段仍然是 `std::cout << "FPS: " << fps`。你想调整清屏颜色？改代码、重新编译、重新运行。你想观察物理系统的碰撞次数？加一行 printf，重新编译，在命令行里翻找输出。

**没有 GUI 的引擎是一个黑盒。** 你无法在运行时观察内部状态，无法实时调整参数，无法把多个系统的信息并排显示在屏幕上。

但这里有一个关键选择：**你要的是"游戏内 UI"（玩家看到的血条、背包、对话窗口），还是"开发调试 UI"（开发者看到的日志面板、Inspector、性能图表）？**

阶段 1 的目标是后者。前者（Retained-Mode 游戏 UI）属于阶段 5 的 [[2D渲染与UI渲染]]。现在你只需要一个能快速搭建调试面板的工具。

---

## 问题 1：为什么选 ImGui，而不是 Qt 或自研 UI？

### 方案 A：Qt / Electron

Qt 是工业级桌面应用框架，提供了完整的窗口管理、布局系统、控件库、Docking、文件对话框。很多商业引擎的编辑器（如 Godot 早期版本、CryEngine）用 Qt 构建。

但 Qt 的问题在于：
- **太重**：Qt 的 DLL 体积通常 50MB+，而且有自己的事件循环、对象模型、元对象系统。它与你的引擎事件队列是两套体系，焦点切换时容易丢事件。
- **3D 视口是黑盒**：Qt 的 OpenGL Widget 是一个独立的原生窗口，无法参与 Qt 的布局流。你的 3D 场景是"嵌进去的异物"，而不是 UI 树的一部分。
- **C++ API 冗长**：写一个带按钮的窗口需要 50 行 Qt 代码，而 ImGui 只需要 5 行。

### 方案 B：自研 UI 框架（UE Slate 模式）

UE 的 Slate 是一个纯 C++ 的自研 UI 框架，从控件树、布局系统、输入路由到渲染提交全部自己掌控。3D 视口是 UI 树中的一个普通节点，没有"嵌入"的概念。

优势是完美的融合度，代价是：**Epic 花了数年迭代，UE4 早期 Slate 的 bug 数量远超想象。个人开发者在阶段 1~3 绝不应该从头写 Slate。**

### 方案 C：Dear ImGui（推荐）

ImGui 是一个**即时模式 GUI**（Immediate Mode GUI）库。它的核心哲学与 Qt/Slate 完全相反：

- **没有控件树**：不存在 `Button* btn = new Button("Click me")` 这种对象。每帧你直接调用 `ImGui::Button("Click me")`，ImGui 根据返回值判断用户是否点了它。
- **没有持久化状态**：窗口位置、折叠状态、滑块当前值——这些不是对象的属性，而是 ImGui 内部通过 ID 哈希隐式管理的状态。
- **与渲染后端完全解耦**：ImGui 只生成顶点数据和绘制命令列表（`ImDrawData`），不直接调用任何 GPU API。把它画到屏幕上是你提供的渲染后端的责任。

对个人开发者来说，ImGui 的优势是压倒性的：
- **几小时就能集成**：两个 backend 文件（`imgui_impl_glfw.cpp` + `imgui_impl_opengl3.cpp`），十几行初始化代码。
- **专注调试 UI**：ImGui 的设计初衷就是"在已有 3D 应用里快速叠加调试面板"。它不提供复杂的布局引擎，但提供了日志滚动区、树形控件、颜色选择器、绘图 API——这些都是引擎调试最需要的。
- **不绑架架构**：ImGui 是一个"客人"，住在你的渲染循环里。未来如果你决定用自研 ECS UI 系统替代游戏内 UI，ImGui 可以继续作为调试工具保留，两者完全不冲突。

> **参考 UE**：UE 的编辑器 UI 用 Slate（自研 Retained-Mode），但 UE 的Gameplay 调试也大量使用了 ImGui 风格的叠加面板（如碰撞可视化、导航网格显示）。这说明即使是拥有完整 UI 框架的工业引擎，ImGui 风格的即时调试面板仍然是不可替代的。

> **参考 Bevy**：Bevy 的调试面板（如 `bevy_inspector_egui`）直接用 egui（Rust 版 ImGui）实现。这再次验证了"即时模式 GUI 是引擎调试的最佳搭档"这一结论。

---

## 问题 2：ImGui 怎么画到屏幕上？

ImGui 自身不接触窗口系统，也不接触 GPU。它把这两件事交给了**两个 Backend**：

### Platform Backend（平台后端）

负责连接窗口系统和输入设备：
- 监听 GLFW/SDL/Win32 的鼠标、键盘、窗口尺寸变化事件
- 维护 `ImGuiIO` 中的输入状态（鼠标位置、按键按下、字符输入）
- 管理鼠标光标样式、剪贴板、窗口焦点

### Renderer Backend（渲染后端）

负责把 ImGui 生成的顶点数据变成 GPU 命令：
- 把 `ImDrawData` 上传到 GPU 顶点缓冲
- 绑定 ImGui 的默认着色器（一个简单的纹理采样顶点/片段着色器）
- 按 `ImDrawList` 的顺序执行绘制调用
- 处理字体纹理的生成和绑定

对于 GLFW + OpenGL，你只需要把 ImGui 官方提供的两个 backend 文件加入工程：
- `imgui_impl_glfw.cpp` / `.h` —— Platform Backend
- `imgui_impl_opengl3.cpp` / `.h` —— Renderer Backend

### 初始化代码

```cpp
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

void initImGui(GLFWwindow* window) {
    // 1. 创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // 2. 启用 Docking 和 Multi-Viewport（为什么需要见下文）
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // 3. 设置样式
    ImGui::StyleColorsDark();

    // 4. 初始化 Platform + Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}
```

---

## 问题 3：一帧的 ImGui 生命周期是什么？

ImGui 是即时模式的——**每帧都要重新构建整个 UI**。不存在"创建一个按钮对象然后等用户点"的概念。每帧你告诉 ImGui"这里有个按钮"，ImGui 返回"这帧它是否被点了"。

### 主循环中的 ImGui 代码

```cpp
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // ========== 1. 开始新帧 ==========
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ========== 2. 构建 UI（你的所有 ImGui 代码都在这里） ==========
    ImGui::Begin("Debug Panel");
    ImGui::Text("Hello, Engine!");
    if (ImGui::Button("Click Me")) {
        // 按钮被点击了（仅在这帧）
    }
    ImGui::End();

    // ========== 3. 生成绘制数据 ==========
    ImGui::Render();

    // ========== 4. 清屏 + 画 3D 场景 ==========
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // draw_scene();

    // ========== 5. 把 ImGui 画到屏幕上 ==========
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // ========== 6. 处理 Multi-Viewport（窗口拖出主窗口） ==========
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

    glfwSwapBuffers(window);
}
```

### 为什么必须先 `NewFrame` 再画 3D，最后 `RenderDrawData`？

因为 ImGui 需要叠加在 3D 场景之上。如果你先画 ImGui 再画 3D，`glClear` 会把 ImGui 清掉。正确的顺序是：
1. 更新 ImGui 输入状态（`NewFrame`）
2. 构建 UI（`Begin`/`End`）
3. 清屏
4. 画 3D 场景
5. 画 ImGui（覆盖在 3D 之上）
6. 交换缓冲

> **注意**：`ImGui::Render()` 本身不接触 GPU。它只是把你在 `NewFrame` 和 `Render` 之间调用的所有 ImGui 函数，打包成一个 `ImDrawData` 结构。真正的 GPU 提交发生在 `ImGui_ImplOpenGL3_RenderDrawData` 中。

---

## 问题 4：窗口管理是怎么工作的？

### 最 naive 的理解："创建一个窗口对象"

如果你用过 Qt 或 WinForms，你会想象：

```cpp
// ❌ 这不是 ImGui 的方式
Window* win = new Window("Debug Panel");
win->addChild(new Button("Click Me"));
```

ImGui 没有对象。每帧你直接调用函数：

```cpp
// ✅ ImGui 的方式
ImGui::Begin("Debug Panel");
if (ImGui::Button("Click Me")) { /* ... */ }
ImGui::End();
```

`Begin("Debug Panel")` 不是"创建一个名为 Debug Panel 的窗口对象"，而是"在这帧的绘制列表里添加一个窗口区域"。ImGui 用窗口标题的字符串哈希作为 ID 来跟踪状态（位置、尺寸、折叠状态）。如果你有两个窗口标题相同，它们会共享同一个状态——导致奇怪的行为。

### 解决同名窗口：显式 ID

```cpp
ImGui::Begin("Debug Panel");           // ID = hash("Debug Panel")
ImGui::Begin("Debug Panel##1");        // ID = hash("Debug Panel##1")
ImGui::Begin("Debug Panel##2");        // ID = hash("Debug Panel##2")
```

`##` 后面的内容不会显示在标题栏，但会参与 ID 哈希计算。

### 窗口状态持久化

ImGui 会自动把窗口的位置、尺寸、Docking 关系保存到 `imgui.ini` 文件。下次启动程序时，窗口会恢复到上次关闭时的布局。你不需要写任何序列化代码。

---

## 问题 5：Docking 是什么？为什么阶段 1 就要开启？

Docking（停靠）是让 ImGui 窗口可以互相吸附、并排、堆叠的能力。没有 Docking，你的调试面板是散落在屏幕上的浮动窗口，每次启动都要重新排列。

有了 Docking，你可以：
- 把"日志面板"拖到左下角，它会自动吸附成左下区域
- 把"ECS Inspector"拖到右侧，与"属性面板"堆叠成标签页
- 把"性能分析器"拖到顶部，与"场景视图"并排

这看起来只是"用户体验优化"，但它在工程上有更深层的意义：

**Docking 是编辑器框架的骨架。** 阶段 7 的 [[编辑器框架]] 需要支持复杂的布局系统、视口嵌入、资源浏览器。如果你在阶段 1 就启用 Docking，后续所有面板（日志、Inspector、性能、资源）都自动继承了这个能力，不需要为每个面板单独写布局代码。

> **如何启用**：在初始化时设置 `io.ConfigFlags |= ImGuiConfigFlags_DockingEnable`。然后在一个全屏的 ImGui 窗口里调用 `ImGui::DockSpaceOverViewport()`，整个窗口区域就变成了 Docking 容器。其他 ImGui 窗口可以自动停靠到这个容器里。

```cpp
// 在主循环的 UI 构建阶段
ImGui::DockSpaceOverViewport(
    ImGui::GetMainViewport(),
    ImGuiDockNodeFlags_PassthruCentralNode
);
```

`PassthruCentralNode` 的意思是：Docking 容器的中央区域保持透明，3D 场景可以透过它显示。Docking 面板围绕在中央区域的四周和边缘。

---

## 问题 6：字体怎么加载？

ImGui 默认内嵌了一套英文位图字体（ProggyClean，13px）。对于纯英文调试来说够用了，但如果你想显示中文——比如日志里有中文路径、组件名是中文——就必须加载外部 TTF 字体。

### 加载中文字体

```cpp
void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // 加载支持中文的字体（如思源黑体、微软雅黑）
    // 注意：字体文件必须存在，否则 ImGui 会回退到默认字体
    io.Fonts->AddFontFromFileTTF(
        "Assets/fonts/SourceHanSansSC-Regular.otf",
        18.0f,           // 字体大小（像素）
        nullptr,
        io.Fonts->GetGlyphRangesChineseFull()  // 加载所有中文字形
    );

    // 构建字体图集纹理
    // 这必须在 Renderer Backend 初始化后调用，因为需要 GPU 纹理上传
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // OpenGL 纹理上传（imgui_impl_opengl3 会在 NewFrame 时自动处理）
    // 如果你用自定义渲染后端，需要手动创建纹理并把 ID 赋值给 io.Fonts->TexID
}
```

### 字体图集的内存问题

中文字体有数万个字形。如果一次性加载全部，`GetGlyphRangesChineseFull()` 会生成一个巨大的纹理图集（可能 2048×2048 或更大），显存占用几十 MB。

**改进：只加载常用字**

```cpp
static const ImWchar ranges[] = {
    0x0020, 0x00FF,  // Basic Latin + Latin Supplement
    0x4E00, 0x9FFF,  // CJK Unified Ideographs（常用汉字）
    0x3000, 0x303F,  // CJK Symbols and Punctuation
    0
};
io.Fonts->AddFontFromFileTTF("path/to/font.ttf", 18.0f, nullptr, ranges);
```

更高级的方案是**动态字体图集**（如 `imgui_freetype` + 动态缓存），但阶段 1 不需要。固定加载常用 3000~5000 个汉字足够覆盖大多数调试场景。

---

## 问题 7：Multi-Viewport 是什么？要不要开？

Multi-Viewport 允许你把 ImGui 窗口拖出主窗口，成为独立的操作系统窗口。比如你可以把"性能分析器"拖到第二个显示器上，主显示器继续显示 3D 场景。

开启方式：

```cpp
io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
```

然后在 `RenderDrawData` 之后加上：

```cpp
if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    GLFWwindow* backup = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();      // 更新所有独立窗口的位置/尺寸
    ImGui::RenderPlatformWindowsDefault(); // 渲染所有独立窗口
    glfwMakeContextCurrent(backup);      // 恢复主窗口上下文
}
```

### Trade-off

**优点**：多显示器开发体验大幅提升。调试面板不占用主窗口空间。

**代价**：
- OpenGL 上下文切换开销：每个独立窗口都需要一个共享的 OpenGL 上下文
- 输入路由复杂：鼠标从主窗口移到独立窗口时，坐标系、焦点、事件投递都需要特殊处理
- 对于 Vulkan：每个独立窗口需要独立的 Surface 和 SwapChain，资源管理复杂度翻倍

**个人项目推荐**：阶段 1 开启 Multi-Viewport，因为它对开发体验的改善非常显著，而且 GLFW backend 已经帮你处理了所有平台细节。如果遇到性能问题（如大量独立窗口时的上下文切换），可以在阶段 5 关闭或限制独立窗口数量。

---

## 问题 8：向 ECS 演进，ImGui 该怎么融入？

ImGui 是全局状态驱动的（`ImGuiContext`），这与 ECS 的"所有状态都是组件"的理念似乎冲突。但冲突没有想象中严重。

### ImGui 与 ECS 的协作边界

**不要试图把 ImGui 本身 ECS 化。** ImGui 的即时模式设计天生不适合变成组件系统：
- 每帧重建的 `ImDrawData` 不是持久化状态
- 窗口的折叠、位置由 ImGui 内部 ID 哈希管理，不需要外部存储
- 控件回调（按钮点击、滑块变化）是瞬时的，不适合用 ECS Event 模拟

**正确的做法是：把 ImGui 当作 ECS 的"观察者"和"操作者"。**

```
[ECS World 状态]
    │
    ├──► [LogEvent System]  ──► 把日志事件写入 LogBuffer 组件
    │                            │
    │                            ▼
    │                       [ImGui LogPanel]
    │                       每帧 Query LogBuffer，
    │                       用 ImGui::Text 显示到面板
    │
    ├──► [Transform System] ──► 更新 Position/Rotation/Scale 组件
    │                            │
    │                            ▼
    │                       [ImGui Inspector]
    │                       选中 Entity 后，
    │                       用 ImGui::InputFloat3 修改 Position
    │                       修改直接写回 ECS 组件
    │
    └──► [Performance System] ──► 统计各 System 耗时
                                   │
                                   ▼
                              [ImGui Profiler]
                              用 ImGui::PlotLines 显示帧时间曲线
```

在这个模型里：
- **ECS 是真相源**：所有数据（日志、Transform、性能统计）都存储在 ECS World 的组件中
- **ImGui 是视图层**：它只是读取这些组件，用人类友好的方式显示出来
- **ImGui 也是控制器**：用户在 Inspector 里修改数值，直接写回 ECS 组件

> **参考 Bevy**：Bevy 的 `bevy_inspector_egui` 正是这个模式。egui 窗口读取 ECS Query，显示组件字段，用户修改后直接通过 `Commands` 写回 World。ImGui 不拥有任何数据，它只是 ECS 数据的可视化投影。

### 长期架构：两套 UI 系统并存

```
┌─────────────────────────────────────────┐
│           引擎 UI 架构（长期）              │
├─────────────────────────────────────────┤
│  调试/编辑器 UI：ImGui（即时模式）           │
│  ├── 日志面板、Inspector、Profiler        │
│  ├── Docking 布局、Gizmo、节点图          │
│  └── 不面向玩家，只面向开发者              │
├─────────────────────────────────────────┤
│  游戏内 UI：ECS 原生 Retained-Mode（阶段5）  │
│  ├── 血条、背包、对话框、菜单             │
│  ├── UI 节点是 ECS Entity                 │
│  └── 支持动画、交互、数据绑定             │
└─────────────────────────────────────────┘
```

这个双轨制是成熟引擎的标准做法。UE 用 Slate 做编辑器，用 UMG 做游戏内 UI；Unity 用 IMGUI/UIToolkit 做编辑器，用 uGUI 做游戏内 UI。个人项目完全可以沿用这个思路：ImGui 专注调试，自研 ECS UI 专注游戏。

---

## 问题 9：AI 如何观察/操作 ImGui？

### 可观测性

ImGui 本身不是为 AI 设计的——它是为人类开发者设计的调试工具。AI 不应该"点击 ImGui 的按钮"，而应该直接操作 ECS 数据。

但 ImGui 对 AI 有一个重要价值：**视觉验证**。AI 通过 MCP 修改了某个组件的数值后，它可以要求引擎截图，然后观察 ImGui Inspector 是否显示了正确的数值、3D 视图是否发生了预期的变化。这是"闭环验证"的关键一环。

### 工具边界

AI 的 MCP 接口应该直接操作 ECS World，而不是操作 ImGui：

```cpp
// ✅ AI 应该这样做：
{
    "method": "ecs.set_component",
    "params": {
        "entity": 42,
        "component": "Transform",
        "field": "position",
        "value": [1.0, 2.0, 3.0]
    }
}

// ❌ AI 不应该这样做：
{
    "method": "imgui.click_button",
    "params": {
        "window": "Inspector",
        "button": "Set Position"
    }
}
```

ImGui 是人类的眼睛和手，AI 的眼睛应该是 ECS Query，AI 的手应该是 ECS Command。

---

## 阶段 1 的完整代码

下面是整合了上述所有改进的阶段 1 完整代码。它在深灰色窗口之上叠加了一个 Docking 布局的调试面板，包含一个可点击的按钮和一个显示 FPS 的文本标签：

```cpp
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <chrono>

// ========== 初始化 ==========
bool initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // 启用 Docking + Multi-Viewport
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // 自动保存布局到 imgui.ini
    io.IniFilename = "imgui.ini";

    ImGui::StyleColorsDark();

    // 初始化 backends
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 460")) return false;

    // 加载中文字体（可选，字体文件需存在）
    // io.Fonts->AddFontFromFileTTF("Assets/fonts/NotoSansCJK-Regular.ttc", 18.0f,
    //     nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    return true;
}

void shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

// ========== 构建 UI ==========
void buildUI(float dt, float fps) {
    // 创建全屏 Docking 空间
    ImGui::DockSpaceOverViewport(
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode
    );

    // 示例：调试面板
    ImGui::Begin("Debug");
    ImGui::Text("FPS: %.1f | Frame: %.3f ms", fps, dt * 1000.0f);

    static float clear_color[3] = {0.1f, 0.1f, 0.1f};
    ImGui::ColorEdit3("Clear Color", clear_color);

    if (ImGui::Button("Reset Color")) {
        clear_color[0] = clear_color[1] = clear_color[2] = 0.1f;
    }

    ImGui::Separator();
    ImGui::Text("Mouse: %.0f, %.0f", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);

    ImGui::End();

    // 示例：日志占位面板（[[可视化日志系统]] 中填充）
    ImGui::Begin("Logs");
    ImGui::Text("Log output will appear here...");
    ImGui::End();
}

// ========== 主循环 ==========
int main() {
    // GLFW + OpenGL 初始化（来自 [[最简图形后端]]）
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Hello Engine", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD failed\n";
        return -1;
    }

    if (!initImGui(window)) {
        std::cerr << "ImGui init failed\n";
        return -1;
    }

    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point last_time = Clock::now();
    float fps = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        glfwPollEvents();

        // ImGui 新帧
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 构建 UI
        buildUI(dt, fps);

        // 生成绘制数据
        ImGui::Render();

        // 清屏 + 画 3D（阶段 5 填充）
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 渲染 ImGui
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Multi-Viewport
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }

        glfwSwapBuffers(window);

        // FPS 计算
        static float fps_timer = 0;
        static int frame_count = 0;
        fps_timer += dt;
        frame_count++;
        if (fps_timer >= 1.0f) {
            fps = (float)frame_count;
            fps_timer = 0;
            frame_count = 0;
        }
    }

    shutdownImGui();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

---

## 常见陷阱

### 陷阱 1：`glfwPollEvents` 必须在 `NewFrame` 之前

`ImGui_ImplGlfw_NewFrame` 会读取 GLFW 的输入状态。如果你在 `glfwPollEvents` 之前调用 `NewFrame`，ImGui 读到的还是上一帧的鼠标位置。

### 陷阱 2：`RenderDrawData` 必须在 `SwapBuffers` 之前

如果先 `SwapBuffers` 再 `RenderDrawData`，ImGui 会画到下一帧的后台缓冲里，导致延迟一帧显示。

### 陷阱 3：多视口下的 OpenGL 上下文切换

开启 `ViewportsEnable` 后，每个独立窗口都有自己的 OpenGL 上下文（通过 GLFW 的 context sharing 实现）。如果你在渲染 3D 场景时绑定了某个 VAO，然后 ImGui 的独立窗口渲染切换了上下文，再切回来时 VAO 绑定可能已经丢失。安全做法是在 `RenderPlatformWindowsDefault` 之后重新绑定你需要的状态。

### 陷阱 4：字体图集未生成

如果你加载了自定义字体但忘了调用 `Build()` 或 `GetTexDataAsRGBA32()`，ImGui 不会报错，但所有文字都会显示成空白方块。

---

## 下一步预告

现在你的引擎已经能：
- 在深灰色窗口上叠加 ImGui 调试面板
- 用 Docking 布局管理多个面板的位置关系
- 用按钮、滑块、颜色选择器与引擎实时交互
- 把面板拖到第二个显示器（Multi-Viewport）
- 自动保存和恢复布局（`imgui.ini`）

**但这还不够。这些面板还是"空壳"——日志面板里没有日志，Inspector 里没有 ECS 数据，性能图表没有数据曲线。**

下一篇：[[可视化日志系统]] — 我们将建立异步日志核心、多 Sink（控制台/文件/ImGui）、级别过滤和结构化输出。让你的引擎真正拥有第一个"能看见的调试工具"：一个实时滚动、支持级别过滤的 ImGui 日志面板。从此所有系统验证都通过面板反馈，不再硬编码 `printf`。

---

> [[0_RoadMap|← 返回 SelfGameEngine 构建手册]]
