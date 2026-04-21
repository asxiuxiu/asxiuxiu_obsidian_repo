---
title: UI 系统与 ImGui 集成
description: 解析 Piccolo 编辑器如何基于 ImGui 实现 DockSpace 布局、反射驱动的 Inspector 面板和跨平台 DPI 适配。
date: 2026-04-12
tags:
  - piccolo
  - source-analysis
  - editor
  - imgui
aliases:
  - Piccolo-编辑器-ImGui-集成
---

> [[Notes/Piccolo/索引|← 返回 Piccolo 索引]]

# 编辑器-源码解析：UI 系统与 ImGui 集成

## Why：为什么要学习 Piccolo 的 UI 系统？

- **问题背景**：游戏引擎编辑器需要大量即时模式的调试面板（场景树、属性 Inspector、资源浏览器、视口控制）。保留模式 UI 框架（如 Qt）虽然功能强大，但编译依赖重、与游戏渲染循环耦合复杂。ImGui 以其极简、即时模式、直接渲染到游戏窗口的特性，成为小型引擎编辑器的首选。
- **不用它的后果**：自研引擎若选择重型 UI 框架，会拖慢迭代速度；若 UI 与渲染系统分离，又会出现窗口同步、DPI 适配、输入焦点争夺等问题。
- **应用场景**：
  1. 为自研引擎快速搭建一个 ImGui 编辑器前端。
  2. 理解如何将反射系统与 Inspector 面板打通，实现零代码的属性编辑。
  3. 学习 DockSpace 布局与游戏视口的共存策略。

## What：Piccolo 的编辑器 UI 是什么？

Piccolo 的编辑器 UI 采用 **ImGui** 作为即时模式 UI 库，由 `EditorUI` 类统一封装。它继承自 `WindowUI` 基类，只暴露两个接口：`initialize()`（初始化 ImGui 上下文和 Vulkan 后端）和 `preRender()`（每帧绘制所有面板）。

编辑器窗口采用 **ImGui DockSpace** 布局，划分为五个主要区域：

```
┌─────────────────────────────────────────────────────────┐
│ [Menu]  File | Window | Debug                           │
├──────────────┬────────────────────────────┬─────────────┤
│              │                            │             │
│ World        │    Game Engine (Viewport)  │ Components  │
│ Objects      │    [Trans][Rotate][Scale]  │ Details     │
│              │    [Editor Mode / Game     │ (Inspector) │
│              │     Mode]                  │             │
├──────────────┤                            │             │
│ File Content │                            │             │
│ (Assets)     │                            │             │
└──────────────┴────────────────────────────┴─────────────┘
```

## How：Piccolo 是如何实现的？

### 1. UI 基类 WindowUI

> 文件：`engine/source/runtime/function/ui/window_ui.h`，第 16~21 行

```cpp
class WindowUI
{
public:
    virtual void initialize(WindowUIInitInfo init_info) = 0;
    virtual void preRender() = 0;
};
```

`WindowUI` 是运行时层定义的抽象基类，只负责两件事：初始化和每帧预渲染。这种极简接口让 Runtime 完全不关心 Editor 内部有多少个面板、用了什么 UI 库。

### 2. EditorUI 的初始化

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 865~914 行

```cpp
void EditorUI::initialize(WindowUIInitInfo init_info)
{
    // 创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // 根据窗口 DPI 设置内容缩放
    float x_scale, y_scale;
    glfwGetWindowContentScale(init_info.window_system->getWindow(), &x_scale, &y_scale);
    float content_scale = fmaxf(1.0f, fmaxf(x_scale, y_scale));
    windowContentScaleUpdate(content_scale);
    glfwSetWindowContentScaleCallback(...);

    // 加载字体，启用 Docking
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingAlwaysTabBar = true;
    io.Fonts->AddFontFromFileTTF(
        config_manager->getEditorFontPath().generic_string().data(), content_scale * 16, nullptr, nullptr);
    io.Fonts->Build();

    // 设置样式和颜色主题
    setUIColorStyle();

    // 设置窗口图标
    GLFWimage window_icon[2];
    window_icon[0].pixels = stbi_load(big_icon_path_string.data(), ...);
    glfwSetWindowIcon(init_info.window_system->getWindow(), 2, window_icon);

    // 初始化 Vulkan UI 渲染后端
    init_info.render_system->initializeUIRenderBackend(this);
}
```

初始化流程非常清晰：先创建 ImGui 上下文，再做平台适配（DPI、字体、图标），最后把 `EditorUI` 注册到渲染系统的 Vulkan 后端中。这保证了 UI 和游戏画面使用的是**同一个窗口和同一个 Swapchain**。

### 3. DockSpace 布局与主菜单

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 277~391 行

```cpp
void EditorUI::showEditorMenu(bool* p_open)
{
    ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_DockSpace;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ... | ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main_viewport->WorkPos, ImGuiCond_Always);
    std::array<int, 2> window_size = g_editor_global_context.m_window_system->getWindowSize();
    ImGui::SetNextWindowSize(ImVec2((float)window_size[0], (float)window_size[1]), ImGuiCond_Always);

    ImGui::Begin("Editor menu", p_open, window_flags);

    ImGuiID main_docking_id = ImGui::GetID("Main Docking");
    if (ImGui::DockBuilderGetNode(main_docking_id) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(main_docking_id);
        ImGui::DockBuilderAddNode(main_docking_id, dock_flags);
        // ... 划分左/右/下三个子区域
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &left);
        ImGuiID left_file_content = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.30f, nullptr, &left_other);
        // 将各个窗口 Dock 到对应区域
        ImGui::DockBuilderDockWindow("World Objects", left_asset);
        ImGui::DockBuilderDockWindow("Components Details", right);
        ImGui::DockBuilderDockWindow("File Content", left_file_content);
        ImGui::DockBuilderDockWindow("Game Engine", left_game_engine);
        ImGui::DockBuilderFinish(main_docking_id);
    }

    ImGui::DockSpace(main_docking_id);

    // MenuBar: Reload/Save Level, Debug, Exit
    if (ImGui::BeginMenuBar()) { ... }

    ImGui::End();
}
```

DockSpace 布局只在**首次启动**或**节点不存在时**构建一次（通过 `DockBuilderGetNode == nullptr` 判断）。四个子窗口（World Objects、Components Details、File Content、Game Engine）被预先分配到左、右、下、中四个 Dock 区域，用户后续可以自由拖拽调整。

### 4. 游戏视口窗口与 Editor/Game 模式切换

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 617~767 行（节选）

```cpp
void EditorUI::showEditorGameWindow(bool* p_open)
{
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("Game Engine", p_open, window_flags);

    // 顶部工具栏：Translate / Rotate / Scale 按钮
    if (ImGui::BeginMenuBar())
    {
        drawAxisToggleButton("Trans",   ..., TranslateMode);
        drawAxisToggleButton("Rotate",  ..., RotateMode);
        drawAxisToggleButton("Scale",   ..., ScaleMode);
        // Editor Mode / Game Mode 切换按钮
        if (g_is_editor_mode) { ImGui::Button("Editor Mode"); ... }
        else                  { ImGui::Button("Game Mode");   ... }
        ImGui::EndMenuBar();
    }

    // 计算视口区域（排除 MenuBar 的高度）
    Vector2 render_target_window_pos  = { ... };
    Vector2 render_target_window_size = { ... };
    auto menu_bar_rect = ImGui::GetCurrentWindow()->MenuBarRect();
    render_target_window_pos.y  = menu_bar_rect.Max.y;
    render_target_window_size.y = (ImGui::GetWindowSize().y + ImGui::GetWindowPos().y) - menu_bar_rect.Max.y;

    // 将视口尺寸同步到 RenderSystem
    g_runtime_global_context.m_render_system->updateEngineContentViewport(
        render_target_window_pos.x, render_target_window_pos.y,
        render_target_window_size.x, render_target_window_size.y);

    // 同步到 InputManager，用于判断鼠标是否在视口内
    g_editor_global_context.m_input_manager->setEngineWindowPos(render_target_window_pos);
    g_editor_global_context.m_input_manager->setEngineWindowSize(render_target_window_size);

    ImGui::End();
}
```

`Game Engine` 窗口的核心价值不仅是显示标题，而是**实时计算 ImGui 窗口内的有效渲染区域**，并将该区域的位置和尺寸同步给 `RenderSystem` 和 `EditorInputManager`。这确保了：
- 游戏画面只绘制在窗口客户区内（不被 MenuBar 遮挡）；
- 鼠标/键盘输入只在视口区域内被当作"游戏输入"处理。

### 5. 反射驱动的 Inspector 面板

这是 Piccolo 编辑器 UI 最精妙的设计。`Components Details` 窗口不需要为每个组件手写 ImGui 控件，而是通过**运行时反射**自动遍历字段并绘制。

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 529~572 行

```cpp
void EditorUI::showEditorDetailWindow(bool* p_open)
{
    ImGui::Begin("Components Details", p_open, ...);

    std::shared_ptr<GObject> selected_object =
        g_editor_global_context.m_scene_manager->getSelectedGObject().lock();
    if (selected_object == nullptr) { ImGui::End(); return; }

    auto&& selected_object_components = selected_object->getComponents();
    for (auto component_ptr : selected_object_components)
    {
        m_editor_ui_creator["TreeNodePush"](("<" + component_ptr.getTypeName() + ">").c_str(), nullptr);
        auto object_instance = Reflection::ReflectionInstance(
            Piccolo::Reflection::TypeMeta::newMetaFromName(component_ptr.getTypeName().c_str()),
            component_ptr.operator->());
        createClassUI(object_instance);
        m_editor_ui_creator["TreeNodePop"](("<" + component_ptr.getTypeName() + ">").c_str(), nullptr);
    }
    ImGui::End();
}
```

`createClassUI` 递归遍历 `ReflectionInstance` 的基类和字段：

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 439~527 行（节选）

```cpp
void EditorUI::createClassUI(Reflection::ReflectionInstance& instance)
{
    // 1. 先绘制基类字段
    Reflection::ReflectionInstance* reflection_instance;
    int count = instance.m_meta.getBaseClassReflectionInstanceList(reflection_instance, instance.m_instance);
    for (int index = 0; index < count; index++)
    {
        createClassUI(reflection_instance[index]);
    }
    // 2. 再绘制当前类的叶子字段
    createLeafNodeUI(instance);
    if (count > 0) delete[] reflection_instance;
}

void EditorUI::createLeafNodeUI(Reflection::ReflectionInstance& instance)
{
    Reflection::FieldAccessor* fields;
    int fields_count = instance.m_meta.getFieldsList(fields);

    for (size_t index = 0; index < fields_count; index++)
    {
        auto field = fields[index];
        auto ui_creator_iterator = m_editor_ui_creator.find(field.getFieldTypeName());
        if (ui_creator_iterator == m_editor_ui_creator.end())
        {
            // 嵌套对象：递归展开 TreeNode
            Reflection::TypeMeta field_meta = Reflection::TypeMeta::newMetaFromName(field.getFieldTypeName());
            auto child_instance = Reflection::ReflectionInstance(field_meta, field.get(instance.m_instance));
            m_editor_ui_creator["TreeNodePush"](field_meta.getTypeName(), nullptr);
            createClassUI(child_instance);
            m_editor_ui_creator["TreeNodePop"](field_meta.getTypeName(), nullptr);
        }
        else
        {
            // 基础类型：直接调用对应的 ImGui 控件生成器
            m_editor_ui_creator[field.getFieldTypeName()](field.getFieldName(), field.get(instance.m_instance));
        }
    }
    delete[] fields;
}
```

`m_editor_ui_creator` 是一个 `std::unordered_map<std::string, std::function<void(std::string, void*)>>`，在 `EditorUI` 构造函数中预先注册了常见类型的绘制逻辑：

| 类型名 | ImGui 控件 |
|--------|-----------|
| `bool` | `ImGui::Checkbox` |
| `int` | `ImGui::InputInt` |
| `float` | `ImGui::InputFloat` |
| `Vector3` | `ImGui::DragFloat3` |
| `Quaternion` | `ImGui::DragFloat4` |
| `std::string` | `ImGui::Text`（只读） |
| `Transform` | 自定义 `DrawVecControl`（Position/Rotation/Scale） |

这意味着：只要一个 C++ 类/组件在头文件中标记了 `META(Reflection)`，编辑器就能**自动生成**对应的 Inspector 面板，无需任何手写 UI 代码。

## 与上下层的关系

- **上层调用者**：`PiccoloEditor` 每帧调用 `m_editor_ui->preRender()`，触发所有 ImGui 面板绘制。
- **下层依赖**：
  - `EditorUI` 依赖 `WindowSystem`（GLFW 窗口句柄、DPI 信息）和 `RenderSystem`（Vulkan ImGui 后端初始化、视口更新）；
  - `createClassUI` 强依赖 `runtime/core/meta/reflection` 的运行时反射 API；
  - 面板中的操作（如 Reload Level、切换 Debug 选项）直接修改 `WorldManager`、`RenderDebugConfig` 等运行时全局对象。

## 设计亮点与可迁移原理

1. **反射驱动 Inspector：从零到无限的扩展性**
   - Piccolo 没有为每个组件写独立的属性面板代码。`createClassUI` + `createLeafNodeUI` 通过反射元数据递归遍历所有字段，配合类型到 ImGui 控件的映射表，实现了**通用 Inspector**。
   - **可迁移点**：自研引擎若已有反射系统，应优先实现一个类似的"反射→UI"映射层，而不是为每个新组件复制粘贴 ImGui 代码。这是编辑器可维护性的分水岭。

2. **DockSpace 与游戏视口的坐标同步**
   - `Game Engine` 窗口不仅是 UI 面板，它通过 `MenuBarRect()` 精确计算视口偏移和尺寸，并实时同步给 `RenderSystem`。这让引擎画面可以**内嵌**在 ImGui 窗口中，而不是独占整个屏幕。
   - **可迁移点**：自研引擎使用 ImGui 时，必须建立"ImGui 窗口坐标 → 渲染视口"的同步机制。这是实现多视口、编辑器布局的前提。

3. **UI 基类与渲染后端的解耦**
   - `WindowUI` 接口极其精简（`initialize` + `preRender`），Runtime 只关心"有一个 UI 需要渲染"，不关心它是 ImGui、Nuklear 还是自研 UI。
   - **可迁移点**：Runtime 库中只应保留抽象的 UI 接口，具体的 ImGui 上下文创建、Vulkan 后端绑定应下沉到 Editor 层或平台适配层。

## 关键源码片段

> 文件：`engine/source/editor/source/editor_ui.cpp`，第 439~448 行

```cpp
void EditorUI::createClassUI(Reflection::ReflectionInstance& instance)
{
    Reflection::ReflectionInstance* reflection_instance;
    int count = instance.m_meta.getBaseClassReflectionInstanceList(reflection_instance, instance.m_instance);
    for (int index = 0; index < count; index++)
    {
        createClassUI(reflection_instance[index]);
    }
    createLeafNodeUI(instance);
    if (count > 0)
        delete[] reflection_instance;
}
```

## 关联阅读

- [[编辑器-源码解析：主循环与初始化流程|主循环与初始化流程]]
- [[编辑器-源码解析：场景管理与视口交互|场景管理与视口交互]]
- [[核心层-源码解析：反射系统的运行时实现|反射系统的运行时实现]]
- [[框架层-源码解析：Component 系统架构|Component 系统架构]]

---

**索引状态**：第一轮（接口层/骨架扫描）已完成。
