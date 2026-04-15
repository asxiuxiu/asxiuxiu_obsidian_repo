---
title: 极简Inspector与ECS可视化
date: 2026-04-15
tags:
  - self-game-engine
  - ECS
  - inspector
  - imgui
  - debug
aliases:
  - Minimal ECS Inspector
---

> **前置依赖**：[[最小ECS数据层]]
> **本模块增量**：在窗口中实现可交互的 ECS 可视化面板：Entity 列表、组件 Inspector、运行时增删实体和组件。
> **下一步**：[[引擎基础类型与平台抽象]] — 为 ECS 填充数学、字符串、容器、文件IO、线程池等基础设施。

---

## Why：没有 Inspector 的 ECS 是黑盒

上一章我们搭好了最小 ECS 数据层：`Entity` + `ComponentArray` + `World`。它能在内存中正确运转，但有一个致命问题——**你看不见它**。

### 没有这个层时，世界有多糟

1. **调试靠猜**
   - 玩家报告"角色有时候不移动"，你只能在 `MovementSystem` 里打日志，遍历所有实体输出坐标。问题实体淹没在海量日志中。

2. **状态修改无法验证**
   - 你写了一个 `DamageSystem`，理论上会把敌人血量减 10。但你无法实时确认某个具体敌人的 `Health.current` 是否真的变了。

3. **组件增删是高风险操作**
   - 运行中给实体添加一个 `Collider` 组件，没有任何反馈。如果 `World` 里某个 `ComponentArray` 没有同步更新，可能要过几帧才以诡异的方式崩溃。

### 这是"必须"还是"优化"

**必须**。Inspector 不是给编辑器用的奢侈品，而是**引擎开发阶段的基础设施**。在阶段 2 就引入可视化，是因为后续所有系统（数学、渲染、物理）都将通过这个 Inspector 被验证。

> **核心结论**：如果 ECS 世界不能被实时观察，它的"确定性"和"可调试性"就都是空话。

---

## What：极简 Inspector 长什么样？

下面的代码在 [[最小ECS数据层]] 的基础上，叠加了一个基于 ImGui 的 Inspector 面板。它包含三个核心能力：

1. **左侧面板列出所有 Entity 及其组件标签**
2. **选中 Entity 后，右侧面板显示该 Entity 的所有组件字段数值**
3. **可以通过按钮实时添加/删除 Entity，以及手动修改组件数值**

> [!note]
> 本章假设你已经有一个能跑 ImGui 的窗口（见 [[窗口与输入系统]]）。下面的代码只展示与 ECS 可视化相关的 ImGui 调用，不重复窗口和渲染后端初始化。

### 为可视化扩展 World

首先，我们需要让 `World` 能回答两个问题：
- "当前有哪些存活的 Entity？"
- "某个 Entity 有哪些组件？"

```cpp
// ============================================================
// 扩展 World：提供遍历和组件标签查询
// ============================================================
class World {
    // ... 保留上一章的 entityGenerations, freeEntities, positions, velocities, healths

public:
    // 获取所有存活实体的列表（用于 Inspector 左侧面板）
    std::vector<Entity> aliveEntities() const {
        std::vector<Entity> out;
        for (uint32_t id = 0; id < entityGenerations.size(); ++id) {
            out.push_back({id, entityGenerations[id]});
        }
        return out;
    }

    // 获取某个 Entity 拥有的所有组件标签（用于 Inspector 列表展示）
    std::vector<const char*> componentTags(Entity e) const {
        std::vector<const char*> tags;
        if (positions.has(e)) tags.push_back("Position");
        if (velocities.has(e)) tags.push_back("Velocity");
        if (healths.has(e)) tags.push_back("Health");
        return tags;
    }

    // 便捷方法：一键创建带默认组件的实体
    Entity createPlayer() {
        Entity e = create();
        positions.set(e, {0, 0, 0});
        velocities.set(e, {1, 0, 0});
        healths.set(e, {100, 100});
        return e;
    }

    Entity createEnemy() {
        Entity e = create();
        positions.set(e, {5, 0, 0});
        velocities.set(e, {-0.5f, 0, 0});
        healths.set(e, {50, 50});
        return e;
    }
};
```

### ImGui Inspector 面板

```cpp
#include "imgui.h"

// ============================================================
// ECS Inspector：两栏布局
// ============================================================
class ECSInspector {
    Entity selected{0xFFFFFFFF, 0};  // 当前选中的 Entity
    bool autoRunMovement = true;     // 是否每帧自动运行 MovementSystem

public:
    void draw(World& world, MovementSystem& movement, float dt) {
        // ---------- 左侧面板：Entity 列表 ----------
        ImGui::Begin("ECS World");

        if (ImGui::Button("Add Player")) world.createPlayer();
        ImGui::SameLine();
        if (ImGui::Button("Add Enemy")) world.createEnemy();
        ImGui::SameLine();
        if (ImGui::Button("Delete Selected") && selected.valid()) {
            world.destroy(selected);
            selected = {0xFFFFFFFF, 0};
        }

        ImGui::Separator();
        ImGui::Checkbox("Auto Run MovementSystem", &autoRunMovement);
        if (autoRunMovement) movement.tick(world, dt);

        ImGui::Separator();
        ImGui::Text("Entities (%zu alive):", world.aliveEntities().size());

        for (Entity e : world.aliveEntities()) {
            if (!world.valid(e)) continue;

            auto tags = world.componentTags(e);
            std::string label = "Entity " + std::to_string(e.id);
            if (!tags.empty()) {
                label += " [";
                for (size_t i = 0; i < tags.size(); ++i) {
                    label += tags[i];
                    if (i + 1 < tags.size()) label += ", ";
                }
                label += "]";
            }

            bool isSelected = (selected == e);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selected = e;
            }
        }
        ImGui::End();

        // ---------- 右侧面板：Inspector ----------
        ImGui::Begin("Inspector");
        if (selected.valid() && world.valid(selected)) {
            ImGui::Text("Entity ID: %u, Generation: %u", selected.id, selected.generation);
            ImGui::Separator();

            if (auto* pos = world.positions.get(selected)) {
                if (ImGui::TreeNodeEx("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat("x", &pos->x, 0.1f);
                    ImGui::DragFloat("y", &pos->y, 0.1f);
                    ImGui::DragFloat("z", &pos->z, 0.1f);
                    ImGui::TreePop();
                }
            }

            if (auto* vel = world.velocities.get(selected)) {
                if (ImGui::TreeNodeEx("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat("x", &vel->x, 0.1f);
                    ImGui::DragFloat("y", &vel->y, 0.1f);
                    ImGui::DragFloat("z", &vel->z, 0.1f);
                    ImGui::TreePop();
                }
            }

            if (auto* hp = world.healths.get(selected)) {
                if (ImGui::TreeNodeEx("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Current", &hp->current, 0.0f, hp->max);
                    ImGui::Text("Max: %.1f", hp->max);
                    ImGui::TreePop();
                }
            }
        } else {
            ImGui::TextDisabled("No entity selected.");
        }
        ImGui::End();
    }
};
```

### 主循环中的集成

```cpp
int main() {
    // ... 初始化窗口、ImGui、渲染后端（见 [[窗口与输入系统]]）

    World world;
    MovementSystem movement;
    ECSInspector inspector;

    // 预先创建几个实体
    world.createPlayer();
    world.createEnemy();

    while (!windowShouldClose()) {
        float dt = 1.0f / 60.0f;

        // ... 轮询输入、开始 ImGui 帧

        inspector.draw(world, movement, dt);

        // ... ImGui 渲染、交换缓冲
    }

    return 0;
}
```

### 这个最小实现已经解决了什么

| 能力 | 说明 |
|------|------|
| **Entity 列表可视化** | 左侧面板实时列出所有存活实体及其组件标签 |
| **组件字段实时查看/修改** | 选中实体后，Inspector 面板显示 Position/Velocity/Health 的具体数值 |
| **运行时增删实体** | "Add Player" / "Add Enemy" / "Delete Selected" 按钮可直接操作世界 |
| **System 开关可控** | 通过勾选框控制 MovementSystem 是否自动运行，便于观察静态和动态状态 |

### 这个最小实现还缺什么

- **没有自动反射**：组件字段还是手写硬编码到 Inspector 里的（`ImGui::DragFloat("x", &pos->x)`）
- **没有运行时增删组件**：只能增删 Entity，不能给已有 Entity 添加/移除单个组件
- **没有变更历史**：修改数值后无法 Undo/Redo
- **没有序列化**：当前场景无法保存为文件

这些将在 [[反射系统]]、[[编辑器框架]]、[[Prefab与数据层]] 中逐步补齐。

---

## 状态变化图：选中 Entity 时的数据流

理解 Inspector 和 ECS 数据层的交互方式：

```
用户点击左侧面板中的 "Entity 2 [Position, Velocity]"
    │
    ▼
selected = Entity{2, generation}
    │
    ▼
ImGui 右侧面板开始绘制 Inspector
    │
    ├──► world.positions.get(selected) → 非空 → 显示 x/y/z DragFloat
    │
    ├──► world.velocities.get(selected) → 非空 → 显示 x/y/z DragFloat
    │
    └──► world.healths.get(selected) → 空 → 跳过 Health 节点
    │
    ▼
用户在 DragFloat 中修改 x 值
    │
    ▼
直接修改 pos->x（因为 get() 返回的是 T*）
    │
    ▼
下一帧 MovementSystem.tick 使用更新后的 pos->x 继续运算
```

> **诚实性说明**：当前 Inspector 直接通过 `get()` 返回的指针修改组件数据，这意味着修改是**立即生效**的，没有任何缓冲或事务。如果多个 System 同时读取该组件，可能会出现中间状态不一致。在工业级实现中，这种运行时编辑应通过 [[系统调度与确定性]] 中的 `CommandBuffer` 进行，或至少限制在特定调试阶段执行。

---

## How：Inspector 是如何一步一步复杂起来的？

### 阶段 1：最小实现 → 能用（解决组件字段的自动显示问题）

#### 触发原因
- 组件类型从 3 个增加到 20 个，手动为每个字段写 `ImGui::DragFloat` 不可持续
- 新增一个组件后，Inspector 必须同步更新，否则无法调试

#### 代码层面的变化
1. **引入 `TypeRegistry` 萌芽**
   - 注册组件名称、字段名、字段类型、内存偏移
   - Inspector 遍历 `ComponentDesc`，根据字段类型自动选择 `DragFloat`、`Checkbox`、`ColorEdit` 等控件

2. **字段类型到 ImGui 控件的映射表**
   ```cpp
   void drawField(FieldType type, void* ptr) {
       switch (type) {
           case FieldType::Float:  ImGui::DragFloat("", (float*)ptr); break;
           case FieldType::Int:    ImGui::DragInt("", (int*)ptr); break;
           case FieldType::Bool:   ImGui::Checkbox("", (bool*)ptr); break;
           case FieldType::Vec3:   ImGui::DragFloat3("", (float*)ptr); break;
       }
   }
   ```

### 阶段 2：能用 → 好用（解决运行时增删组件和过滤搜索）

#### 触发原因
- 调试时需要给某个 Entity 临时添加一个 `Collider` 来测试碰撞
- Entity 数量超过 100 个，左侧列表难以定位

#### 代码层面的变化
1. **运行时增删单个组件**
   - Inspector 底部增加 "Add Component" 下拉菜单
   - 选择组件类型后，调用 `world.add_component<Health>(selected)`

2. **Entity 列表过滤**
   - 增加搜索框，只显示 ID 包含关键字的实体
   - 增加组件过滤按钮，如"只显示带 Velocity 的实体"

3. **场景树层级显示**
   - 引入 `Parent` / `Children` 组件后，左侧列表改为树形结构（见 [[场景图与变换]]）

### 阶段 3：好用 → 工业级（解决 Undo/Redo 和多视图）

#### 触发原因
- 误改了一个数值导致场景崩溃，需要回退
- 美术和策划需要在不同面板中同时查看/编辑场景

#### 代码层面的变化
1. **命令缓冲与 Undo/Redo**
   - Inspector 的每次编辑不再直接修改内存，而是生成 `SetComponentValueCommand`
   - `CommandBuffer` 在帧末统一执行，并记录历史栈

2. **多 Inspector 视图**
   - 支持同时打开"场景视图"、"资源视图"、"系统性能视图"
   - 每个视图独立维护筛选状态和选中对象

3. **AI 桥接可视化**
   - 增加 "AI 操作日志" 面板，显示外部 Agent 对 ECS 世界的修改历史
   - 高亮被 AI 最近修改过的组件字段（见 [[MCP与Agent桥接层]]）

---

## ECS 重构映射：如果源码是 OOP，到 ECS 该怎么办？

假设源码中有一个 `GameObject` 类：

```cpp
class GameObject {
    Transform transform;
    std::vector<Component*> components;
    GameObject* parent;
    std::vector<GameObject*> children;
};
```

在 ECS 化过程中，它的状态应该被拆分为以下平铺组件：

| OOP 中的类/字段 | ECS 中的表达 | 说明 |
|----------------|-------------|------|
| `GameObject` 实例 | `Entity`（轻量 ID） | 不再承载任何状态 |
| `Transform` | `Position` + `Rotation` + `Scale` 组件 | 可独立增删，Inspector 中分别显示 |
| `components` 向量 | `World` 中的多个 `ComponentArray<T>` | 通过 `has()` / `get()` 查询 |
| `parent` / `children` | `Parent` 组件 + `Children` 组件（或仅用 `Parent`） | 层级关系也是数据，见 [[场景图与变换]] |

**为什么 ECS 更适合 Inspector**：
- **Cache locality**：Inspector 渲染左侧面板时，只需要遍历 `entityGenerations` 数组，不需要遍历深层对象树
- **组合灵活性**：可以给一个 `Entity` 添加 `DebugHighlight` 组件，让 Inspector 用红色边框标出它，而不需要修改任何基类
- **AI 可观测性**：外部 Agent 可以通过统一的 `query_entities` 接口获取列表，不需要理解 OOP 继承关系

---

## AI 友好设计检查清单

| 检查项 | 本模块的实现 | 说明 |
|--------|-------------|------|
| **状态平铺** | ✅ 完全平铺 | Inspector 中显示的所有状态都来自 `ComponentArray` |
| **自描述** | ⚠️ 萌芽阶段 | 当前字段硬编码，但已经为自动反射奠定了基础 |
| **确定性** | ✅ 可观察 | 任何修改都能立即在 Inspector 中验证 |
| **工具边界** | ✅ 已具备雏形 | Inspector 本质上就是 AI 观察 ECS 世界的"人工接口" |
| **Agent 安全** | ❌ 尚未实现 | 直接修改内存，缺少 CommandBuffer 和权限隔离 |

---

## 设计权衡表

| 决策点 | 原型阶段 | 工业级阶段 |
|--------|---------|-----------|
| 字段显示 | 手写 ImGui 控件 | 基于反射自动生成 |
| 修改方式 | 直接修改内存指针 | 通过 CommandBuffer 缓冲 |
| Entity 筛选 | 无 / 简单文本搜索 | 组件标签过滤 + 树形层级 |
| 多视图 | 单一 Inspector | Docking 多窗口 + 自定义布局 |

---

## 如果我要 vibe coding，该偷哪几招？

1. **从 Day 1 就要有可视化调试面板**
   - 哪怕只有一个显示 Entity ID 和组件标签的简陋列表，也比纯日志调试高效十倍。

2. **Inspector 的数据源必须就是 ECS 数据层本身**
   - 不要为 Inspector 单独维护一份"显示用副本"，直接读取 `ComponentArray`。否则数据同步会拖垮你。

3. **让 System 的运行可以被开关**
   - 在 Inspector 里加一个勾选框控制 `MovementSystem` 是否自动运行，这是调试物理、AI、动画系统的基本操作。

4. **把字段编辑做得"越直接越好"**
   - 原型阶段不要纠结 Undo/Redo，先让用户能直接改数值并立刻看到效果。复杂的事务机制是阶段 7 的事。

---

> **本阶段增量总结**：引擎拥有了可交互的 ECS 世界。ImGui 面板能列出实体、显示组件字段、实时增删实体，并且有一个 System 在 Tick 中批量修改组件。所有后续系统（数学、渲染、物理）都可以在这个 Inspector 里被可视化验证。
>
> **下一步**：[[引擎基础类型与平台抽象]] — 为 ECS 填充数学、字符串、容器、文件IO、线程池等基础设施。你的 Inspector 将变得越来越丰富。
