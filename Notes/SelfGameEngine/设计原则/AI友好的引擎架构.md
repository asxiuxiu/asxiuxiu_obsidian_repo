---
title: AI友好的引擎架构
date: 2026-04-14
tags:
  - self-game-engine
  - architecture
  - ai-friendly
  - design-principles
aliases:
  - 引擎设计原则
  - AI引擎架构
---

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])

# AI友好的引擎架构

> **前置依赖**：无（这是所有后续笔记的起点）
> **本模块增量**：确立了 **ECS + 反射 + 确定性** 的元规则，为所有后续模块设计提供统一判据。
> **下一步**：[[从零开始的引擎骨架]] — 在原则确立后，立刻用代码验证思路。

---

## Why：为什么传统游戏引擎对 AI 不友好？

### 没有这个意识时，世界有多糟

传统游戏引擎是为"人类程序员"设计的。它们假设开发者会：
- 逐层阅读继承树来理解对象结构
- 在头文件和文档之间手动同步知识
- 用断点和日志来追踪运行时状态
- 靠经验和代码审查来避免副作用

但当 AI Agent 试图操作这样的引擎时，会遇到三重困境：

1. **看不见**：状态深埋对象图和虚函数表里
   - AI 想知道"这个敌人有多少血量"，必须先找到 `Enemy` 继承自 `Character`，`Character` 里有一个 `private Attribute* attr_` 指针，指针指向的 `HealthAttribute` 才是最终目标。这不是查询，这是考古。

2. **不敢改**：副作用散落在系统各处
   - AI 修改了一个光照强度参数，却不知道 `RenderSystem` 的某个 `OnPropertyChanged` 回调会连锁触发材质重编译。改了一个数字，引擎卡死 3 秒。没有工具边界，AI 的每次操作都是俄罗斯轮盘赌。

3. **验不了**：同样的输入，两次运行结果不同
   - AI 尝试优化物理参数，运行 100 帧后对比效果。但第二次运行时，因为 `std::sort` 不稳定、随机数种子不同、浮点累加顺序有差异，结果完全对不上。AI 失去了最基本的"控制变量法"。

### 三个真实场景

| 场景 | 传统引擎的困难 | AI 友好引擎的答案 |
|------|--------------|-----------------|
| **场景 1：AI 要帮你调优物理参数** | 物理状态分散在数十个 Manager 的私有指针里，AI 连"有哪些参数可调"都列不出来 | 所有状态都是平铺的 ECS 组件，AI 通过 `query_entities("PhysicsBody")` 即可批量读取和修改 |
| **场景 2：AI 修改了一个组件，想知道是否正确生效** | 没有变更追踪，AI 只能重新扫描整个世界来 diff | 每帧自动生成 ChangeLog，AI 只看 "自上次以来变了什么" |
| **场景 3：两个 AI Agent 同时工作，一个调渲染、一个调 gameplay** | 没有隔离机制，两个 Agent 的修改可能互相覆盖或触发未定义行为 | Orchestrator + 组件白名单 + 事务沙箱，让多 Agent 并行成为可能 |

### 这是"必须"还是"优化"

对于传统 3A 游戏开发，AI 友好架构是**优化**——人类团队用 OOP 引擎也能做出伟大作品。但对于 **vibe coding**（让 AI 长时间自主迭代引擎）和 **多 Agent 协作开发**，这是**必须**：

- AI 没有"直觉"，它只能依赖显式、结构化的信息。
- AI 没有"调试经验"，它需要状态平铺、副作用可控、结果可验证。
- AI 不会"看头文件猜语义"，它需要运行时自描述能力。

> **核心结论**：AI 友好的引擎架构，本质上是一套**让机器能理解、能操作、能验证**的设计契约。

---

## What：AI 友好架构的最小公式长什么样？

AI 友好引擎的设计可以浓缩为一个**三元组公式**：

```
AI 友好引擎 = ECS 数据模型 + 运行时反射系统 + 确定性执行框架
```

这三者缺一不可：

| 元素 | 解决什么问题 | 缺失时的症状 |
|------|-----------|------------|
| **ECS 数据模型** | 状态平铺、可查询、可批量操作 | AI 看不见状态，修改会引发副作用 |
| **运行时反射系统** | AI 不读头文件就能理解数据结构 | AI 每次操作都要猜测字段名和类型 |
| **确定性执行框架** | 同样的输入一定得到同样的输出 | AI 无法验证改动效果，无法支持网络同步 |

这个三元组的具体代码实现已经在 [[从零开始的引擎骨架]] 中给出。下面我们用一张表来说明它是如何协同工作的：

| 元素 | 在骨架中的对应代码 | 解决什么问题 |
|------|-------------------|-------------|
| **ECS 数据模型** | `World` 中的 `std::vector<Position>`、`std::vector<Velocity>` 等平铺数组 | 所有状态显式、连续、可序列化，没有隐藏在继承树或指针网中 |
| **运行时反射** | `TypeRegistry` 手动/宏注册组件字段名、类型、偏移量 | AI 无需读 C++ 头文件即可通过 `to_json_schema()` 理解数据结构 |
| **确定性执行** | `Scheduler::tick()` 按固定顺序调用每个 `System::tick(world, dt)` | 同样的初始状态 + 同样的输入序列 = 同样的最终状态 |

> 具体的最小实现代码（约 150 行，可直接编译运行）请参阅 [[从零开始的引擎骨架]]。

### 这个最小公式已经解决了什么

- **状态平铺**：`World` 里的所有数据都是连续数组，没有深层指针树，可直接序列化和 diff。
- **自描述**：`TypeRegistry` 让外部工具（包括 AI）知道每个组件有哪些字段、偏移量和类型。
- **确定性**：`scheduler.tick(world, dt)` 是纯函数式调用，输入固定，输出固定。

### 还缺什么（后续模块会逐步补齐）

- **自动化反射**：`TypeRegistry` 还是手写的，真实引擎需要宏或代码生成（见 [[反射与序列化]]）
- **系统依赖排序**：`PhysicsSystem` 必须在 `MovementSystem` 之前执行时，当前代码无法表达（见 [[系统调度与确定性]]）
- **增量变更日志**：每次都要全量扫描世界才能知道发生了什么变化（见 [[系统调度与确定性]]）
- **快照与回滚**：还不能"尝试一个改动，不满意就撤销"（见 [[系统调度与确定性]]）
- **AI 操作边界**：还没有 `AgentBridge` 和 MCP 接口（见 [[MCP与Agent桥接层]]）

这些不是缺陷，而是**后续 6 个阶段的明确目标**。

---

## How：AI 友好架构是如何一步一步复杂起来的？

### 阶段 1：能用 —— 让 AI 看得见、读得懂

**触发原因**：AI 已经能操作引擎了，但每次操作前都要把整个代码库读一遍来"猜"数据结构。上下文爆炸，操作效率极低。

**代码层面的变化**：

**A. 手写 TypeRegistry → 自动化反射生成**

```cpp
// 阶段 0：手写（如上面的示例）
// 阶段 1：通过宏或代码生成自动化
#define REFLECT_COMPONENT(Name, ...) \
    static ComponentDesc get_desc() { return { #Name, { __VA_ARGS__ }, sizeof(Name) }; }

struct Transform {
    float x, y, z;
    REFLECT_COMPONENT(Transform,
        {"x", FieldType::Float, offsetof(Transform, x)},
        {"y", FieldType::Float, offsetof(Transform, y)},
        {"z", FieldType::Float, offsetof(Transform, z)}
    )
};
```

**B. 引入 AI 可读的 Schema 导出**

引擎在启动时自动生成 `ENGINE_AI.md` 或 `engine_schema.json`，告诉 AI：
- 这个世界有哪些组件？
- 每个组件有哪些字段？类型是什么？取值范围是什么？
- 有哪些 System？它们的执行顺序是什么？

**C. 系统注册自描述工具**

每个 `System` 在初始化时，向 `ToolRegistry` 注册自己的观测/调参工具：

```cpp
struct ToolDesc {
    std::string name;
    std::string description;
    std::string inputSchema;   // JSON Schema，约束 AI 生成合法调用
    bool isReadOnly;
    bool isConcurrencySafe;
    bool isDestructive;
};
```

### 阶段 2：好用 —— 让 AI 能高效协作

**触发原因**：AI 的改动经常引发副作用，上下文太大导致对话频繁截断，多个 AI 同时工作时互相干扰。

**代码层面的变化**：

**A. 增量状态变更日志（ChangeLog）**

AI 不需要读整个世界，只需要读这一帧的 `changelog`：

```cpp
struct ChangeLog {
    struct Entry {
        Entity e;
        std::string component;
        std::string field;
        std::string oldValue;
        std::string newValue;
    };
    std::vector<Entry> entries;
};
```

**B. 命令缓冲（CommandBuffer）**

AI 的改动先进入缓冲，在 Tick 边界统一应用。如果权限检查或冲突检测失败，可以整批回滚。

**C. ENGINE_AI.md —— 静态上下文预加载**

借鉴 Claude Code 的 `CLAUDE.md` 思想，把引擎的静态知识（组件列表、常见模式、最佳实践）预加载到 system prompt 中，作为**可缓存的静态前缀**。动态信息（当前选中实体、最近变更）作为**不可缓存的动态后缀**。明确区分两者，最大化 Prompt Caching 收益。

### 阶段 3：工业级 —— 让多 Agent 能安全并行

**触发原因**：一个 AI 调渲染、一个 AI 写 gameplay、一个 AI 优化物理。它们需要同时工作，但不能互相破坏。

**代码层面的变化**：

**A. AgentBridge + MCP 适配层**

引擎对外的唯一接口是标准 MCP 协议，不绑定任何特定 AI 客户端：

```cpp
class AgentBridge {
public:
    ToolResult query_entities(const std::string& component_mask);
    ToolResult get_component(Entity e, const std::string& comp);
    ToolResult set_component(Entity e, const std::string& comp, const std::string& json);
    ToolResult step(float dt);
    ToolResult save_snapshot(const std::string& name);
    ToolResult rollback_to_snapshot(const std::string& name);
};
```

**B. 权限引擎与沙箱（PermissionEngine + Sandbox）**

```cpp
enum class PermissionBehavior { Deny, Ask, Allow };

struct SandboxConfig {
    std::vector<std::string> allowWrite;    // 允许修改的组件白名单
    std::vector<std::string> denyWrite;     // 禁止修改的组件黑名单
};

class PermissionEngine {
public:
    PermissionResult evaluate(const std::string& toolName, const std::string& input);
};
```

**C. Orchestrator 与多 Agent 编排**

顶层有一个 `Orchestrator`，负责任务分解、Agent 调度、结果合并。它确保：
- 渲染 Agent 不会修改物理组件
- 两个 Agent 不会同时修改同一个实体的同一个字段
- 冲突时自动触发人工审批或回滚

---

## ECS 重构映射：如果源码不是 ECS，该怎么办？

很多现有引擎（包括大量商业引擎和内部引擎）基于 OOP 或 Manager 模式。当你想把它们重构为 AI 友好的 ECS 架构时，可以参考以下映射表：

| 源码中的 OOP 概念 | ECS 中的对应物 | 迁移说明 |
|------------------|---------------|---------|
| `Actor` / `GameObject` 基类 | `Entity`（轻量 ID） | 不再继承行为，而是组合组件。一个实体可以拥有 `Transform` + `Mesh` + `Health`，不需要 `PlayerActor` 子类。 |
| `Actor::OnTick()` 虚函数 | `System::tick(Query<T>)` | 逻辑从对象中剥离，按组件批量执行。 |
| `Manager`（既是系统又是数据管理者） | `System` + `ComponentStorage<T>` | 将"逻辑执行"与"数据管理"彻底分离。 |
| `TickPhase` 硬编码阶段 | `Phase` + 依赖图自动排序 | 从手动维护顺序升级为基于读写 mask 的自动拓扑排序。 |
| 深层的组件指针树 | 平铺的 `ComponentArray<T>` 或 Archetype | 消除指针跳转，提升 cache locality 和 AI 可观测性。 |

### 为什么 ECS 更适合 AI 协作

1. **Cache locality = AI 上下文效率**
   - 线性扫描连续内存比追逐指针树快几个数量级。AI 查询 "所有带 Health 的敌人" 时，ECS 是 O(N) 的顺序读取，OOP 是 O(N) 的随机内存跳转。

2. **组合优于继承 = AI 可预测性**
   - AI 修改 `Health` 组件时，它确切地知道自己在改什么。不会意外触发 `DamageableActor::OnHealthChanged()` 里隐藏的副作用。

3. **数据与逻辑分离 = 可验证性**
   - 给定相同的组件输入数组，System 一定产生相同的输出数组。这是确定性测试和自动 diff 的基础。

---

## AI 友好设计红线（必须遵守的 checklist）

在设计自研引擎的任何一个模块时，逐条检查以下清单：

| 检查项 | 要求 | 本架构的满足方式 |
|--------|------|-----------------|
| **状态平铺** | 该模块的所有可变状态是否可以用 ECS 组件表示？ | `World` 中只有平铺数组，没有深层对象图。 |
| **自描述** | AI 是否可以在不读 C++ 头文件的情况下，通过运行时注册表知道数据结构？ | `TypeRegistry` 提供组件名称、字段列表、类型、偏移量。 |
| **确定性** | 给定相同的输入序列，该模块的输出是否可复现？ | 固定种子随机数、稳定排序、浮点量化、每帧哈希校验。 |
| **工具边界** | 如果 AI 通过 `AgentBridge` / MCP 操作该模块，接口的输入输出是否是结构化的 JSON/Schema？ | 所有工具都有 `inputSchema` 和 JSON 返回值。 |
| **Agent 安全** | 该模块是否支持沙箱、事务或白名单机制，防止 AI 的误操作破坏核心状态？ | `PermissionEngine` + `SandboxConfig` + `CommandBuffer` + `Snapshot` 四层保护。 |

> **核心原则**：任何不能满足上述 5 条红线的模块设计，都需要被重构或添加适配层。

---

## 如果我要 vibe coding，该偷哪几招？

以下是经过验证的、可以立刻用到个人项目中的设计招数：

1. **偷"ECS 是默认数据模型"**
   - 不要从 OOP 开始再"考虑迁移到 ECS"。最小引擎骨架就应该是 ECS。

2. **偷"反射不是可选项，是基础设施"**
   - 哪怕最初手写 `TypeRegistry`，也要在第一天就让它存在。没有反射，AI 就是瞎子。

3. **偷"ChangeLog 压缩 AI 上下文"**
   - 不要每次对话都把整个世界状态发给 AI。只发最近 N 帧的 `ChangeLog`，AI 就能理解"自上次以来发生了什么"。

4. **偷"确定性从第一天开始"**
   - 不要等做网络同步时才考虑确定性。随机数、排序、浮点精度在最小骨架时就要约束。

5. **偷"MCP 是 AI 的唯一入口"**
   - 引擎不绑定任何特定 AI 客户端。所有交互通过 MCP 工具接口。今天是 Claude，明天可以是 Kimi、OpenAI 或自研 Agent。

6. **偷"Orchestrator 是架构的一等公民"**
   - 设计 System 和组件时，默认考虑"如果有两个 Agent 同时操作这个模块，会发生什么？"

---

## 下一步预告

现在你已经确立了自研引擎的**元规则**：

- ✅ 选择 ECS 而非 OOP，因为状态需要对 AI 可见
- ✅ 建立运行时反射，因为 AI 不能读头文件
- ✅ 保证确定性执行，因为 AI 需要验证改动效果
- ✅ 预设 AgentBridge 和 MCP 边界，因为 AI 需要安全的操作入口
- ✅ 规划多 Agent 编排，因为未来不是单 Agent 在战斗

这些原则将作为判据，贯穿后续每一个模块的设计决策。

在 [[从零开始的引擎骨架]] 中，我们将把这些原则转化为**真正可以编译运行的代码**：
- 一个最简陋的 `World` 类
- 一组平铺的组件数组
- 一个固定顺序调度的 `Scheduler`
- 一个手写的 `TypeRegistry`

这是后续一切的基础。让代码跑起来，比任何设计文档都更有说服力。

> **下一步**：[[从零开始的引擎骨架]]

---

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])
