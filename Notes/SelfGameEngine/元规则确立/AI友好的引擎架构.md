---
title: AI友好的引擎架构
date: 2026-04-15
tags:
  - self-game-engine
  - architecture
  - ai-friendly
  - design-principles
aliases:
  - 引擎设计原则
  - AI引擎架构
---

> [[Notes/SelfGameEngine/0_RoadMap|← 返回 SelfGameEngine 构建手册]]

> **前置依赖**：无（这是所有后续笔记的起点）
> **本模块增量**：确立了 **ECS + 反射 + 确定性** 的元规则，为所有后续模块设计提供统一判据。读完这篇笔记，你将能回答"为什么这个引擎选择 ECS 而非 OOP"，能列出 AI 友好架构的 5 条设计红线，并能为任意模块画出"AI 观察接口"和"AI 操作接口"的草图。
>
> 假设你已经知道 C++ 的基本语法，现在我们要解决的是：**从零开始设计一个引擎时，为什么传统做法行不通？**

---

## 问题 0：为什么不能从 OOP 继承树开始？

想象你要向一个完全没见过你房间的人描述"书桌上的台灯是开着的"。

如果你住在一栋 OOP 设计的房子里，你的描述会变成这样：

> "先找到这栋楼的 `Building` 对象，它有一个 `rooms` 数组，第 3 个元素是 `Bedroom` 实例。`Bedroom` 继承自 `Room`，`Room` 里有一个 `furniture` 列表。遍历这个列表，找到类型为 `Desk` 的实例——注意 `Desk` 继承自 `Furniture`。`Desk` 有一个 `items` 指针数组，其中某个指针指向一个 `Lamp` 对象。`Lamp` 继承自 `Appliance`，`Appliance` 里有一个 `bool isOn` 字段，但它是 `protected` 的，你需要调用 `Lamp::setState(true)`，而这个虚函数内部可能触发 `Room::onLightingChanged()` 事件，导致窗帘自动关闭。"

这个人听完会崩溃——他只是想确认灯有没有开。

这就是**传统 OOP 游戏引擎**面对 AI 时的困境。状态深埋在层层继承和虚函数里，每一次简单的查询都像在考古。

### 最 naive 的方案长什么样

如果你完全不懂引擎架构，你可能会这样写：

```cpp
class GameObject {
public:
    virtual void update(float dt) {}
    std::vector<GameObject*> children;
};

class Player : public GameObject {
    Health* health_;
    Weapon* weapon_;
public:
    void update(float dt) override {
        health_->regen(dt);
        weapon_->cooldown(dt);
    }
};
```

这代码对人类来说很直观——`Player` 就是玩家，它有自己的血量和武器，每帧自动更新。但如果一个 AI Agent 接到任务"把所有敌人的血量减半"，它面对的是一片迷雾：

- 敌人是 `Enemy` 类还是 `Monster` 类？它们继承自谁？
- 血量存在 `health_` 指针指向的某个对象里，但指针地址是什么？
- `health_->takeDamage()` 会不会触发连锁反应？有没有隐藏的回调？
- 改了之后怎么确认生效？是不是要重新遍历整个场景树再读一次？

### 发现的问题

OOP 的继承树在 AI 面前暴露出三个致命缺陷：

**第一，AI 看不见**。状态不是平铺的，而是嵌套的。AI 想知道"某个实体的位置"，需要先解析继承链、找到正确的虚函数表、追踪指针——这要求 AI 拥有和人类一样的"头文件阅读理解能力"和"调试直觉"。

**第二，AI 不敢改**。`Player::update()` 里调用了 `health_->regen()`，而 `Health::regen()` 里可能触发了 `onHealthChanged` 回调，回调里又调用了 `UIManager::updateHealthBar()`，最后意外触发了一次材质重编译。AI 改了一个浮点数，引擎卡死 3 秒。没有副作用边界，AI 的每次操作都是俄罗斯轮盘赌。

**第三，AI 验不了**。同样的代码跑两遍，结果可能不一样。`std::sort` 不稳定导致遍历顺序不同，随机数种子不同导致 AI 行为不同，浮点累加顺序不同导致物理模拟漂移。AI 失去了最基本的"控制变量法"——它没法判断"上次改的参数到底有效没有"。

### 改进方案：把房子改成玻璃展厅

既然问题的根源是"东西藏得太深"，那最朴素的改进就是**全部摊开**。

不要让每个对象自己管理自己的状态，而是把同一种状态放在一起管理。所有实体的位置放在一个数组里，所有速度放在另一个数组里，所有血量放在第三个数组里。逻辑也不属于对象，而是独立的外部函数——"移动系统"负责遍历所有"有位置和速度的实体"，批量更新它们的位置。

这就是 **ECS（Entity-Component-System）** 的核心直觉：

- **Entity**：只是一个 ID，像展厅里挂的编号牌。
- **Component**：纯数据，像贴在编号牌下面的标签（位置标签、血量标签、速度标签）。
- **System**：纯逻辑，像按照标签批量处理的工作人员（"所有贴了位置和速度标签的，统一往前挪一步"）。

```cpp
// 没有继承、没有虚函数、没有指针追逐
struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health   { float current; float max; };

// 所有状态平铺在大数组里
std::vector<Position> positions;
std::vector<Velocity> velocities;
std::vector<Health>   healths;

// 逻辑是独立的外部函数
void movement_system(float dt) {
    for (size_t i = 0; i < positions.size(); ++i) {
        positions[i].x += velocities[i].x * dt;
        positions[i].y += velocities[i].y * dt;
        positions[i].z += velocities[i].z * dt;
    }
}
```

这个方案解决了 OOP 的前两个痛点：状态完全平铺，AI 可以直接索引数组；逻辑与数据分离，修改 `positions[i].x` 不会触发任何隐藏回调。

但第三个痛点——"如何验证修改生效"——还没有解决。而且，即便状态摊开了，如果 AI 连"这个世界里有哪些数组"都不知道，它依然是瞎子。

这就引出了下一个问题。

---

## 问题 1：AI 怎么知道"这个世界里有什么"？

假设你已经把引擎改成了 ECS，所有组件都平铺在数组里。现在 AI 问你："我想查询所有带 `Health` 组件的实体，怎么查？"

### 最 naive 的方案

你告诉它："去遍历 `healths` 数组，索引就是实体 ID。"

AI 又问："我想同时知道它们的位置和血量，怎么办？"

你说："去遍历 `positions` 数组，如果某个索引在 `healths` 数组里也有有效数据，就一起读。"

AI 再问你："`positions` 和 `healths` 的长度为什么不一样？哪个索引对应哪个实体？如果某个实体有 `Position` 但没有 `Health`，数组里对应位置是什么值？"

你突然发现，**平铺数据只是第一步**。如果 AI 需要读 C++ 头文件才能知道"这个引擎有哪些组件、每个组件有哪些字段、字段是什么类型"，那么每次新增一个组件，AI 就需要重新学习一次。上下文窗口会爆炸，操作效率极低。

### 改进方案：运行时自描述

解决思路很朴素：让引擎自己给自己写一份"说明书"，并且在运行时可以查询。

这份说明书至少要回答三个问题：

1. 这个世界有哪些组件？（`Position`、`Velocity`、`Health`……）
2. 每个组件有哪些字段？类型是什么？在内存中的偏移量是多少？
3. 有哪些系统？它们读哪些组件、写哪些组件？

```cpp
enum class FieldType { Float, Int, Vec3 /* ... */ };

struct FieldDesc {
    std::string name;      // "x"
    FieldType   type;      // Float
    size_t      offset;    // 0
};

struct ComponentDesc {
    std::string name;              // "Position"
    std::vector<FieldDesc> fields; // {x, y, z}
    size_t      size;              // sizeof(Position)
};

class TypeRegistry {
public:
    std::unordered_map<std::string, ComponentDesc> components;

    void register_all() {
        components["Position"] = {
            "Position",
            { {"x", FieldType::Float, offsetof(Position, x)},
              {"y", FieldType::Float, offsetof(Position, y)},
              {"z", FieldType::Float, offsetof(Position, z)} },
            sizeof(Position)
        };
        // ... Velocity, Health 等同理
    }
};
```

有了这份注册表，AI 不需要读头文件就能知道："`Position` 有 `x`、`y`、`z` 三个 `Float` 字段，偏移量分别是 0、4、8。"

但这依然是手写的。当组件数量超过 20 个时，维护这份注册表本身就是负担。在后续阶段我们会用宏或代码生成来自动化它（见 [[反射系统]]），但**元规则是：反射不是可选项，是第一天就要存在的基础设施**。没有它，AI 就是瞎子。

现在 AI 能"看见"世界了。但如果它想修改一个值，怎么保证改完不会搞崩引擎？

---

## 问题 2：AI 怎么安全地"动手"改东西？

假设 AI 通过 `TypeRegistry` 知道了 `Health.current` 是一个 `Float`，位于偏移量 0。它想把某个敌人的血量从 100 改成 50。

### 最 naive 的方案

直接改：

```cpp
healths[enemy_id].current = 50.0f;
```

这行代码本身没问题，但它发生在"引擎运行中的任意时刻"。如果此时 `HealthSystem` 正在遍历 `healths` 数组执行 `regen()`，而 AI 的修改和系统的遍历之间没有任何同步，就会发生数据竞争。更可怕的是，如果 `Health.current` 的修改触发了某个回调——比如血量降到 0 时自动销毁实体——而销毁实体意味着 `healths` 数组发生了缩容或重排，那正在遍历数组的 `HealthSystem` 就会访问到无效内存。

AI 不知道这些隐含的时序约束。对它来说，改一个数字和改一个配置文件没有区别。

### 改进方案 1：命令缓冲（Command Buffer）

不要让 AI 直接修改组件，而是让它把修改请求放进一个"待办清单"。引擎在固定的 Tick 边界（比如所有 System 都执行完之后）统一处理这些请求。这样 AI 的操作和引擎的内部遍历天然隔离。

```cpp
struct SetComponentCmd {
    Entity entity;
    std::string component;  // "Health"
    std::string field;      // "current"
    float       value;      // 50.0f
};

class CommandBuffer {
    std::vector<SetComponentCmd> commands;
public:
    void set_component(Entity e, const std::string& comp,
                       const std::string& field, float val) {
        commands.push_back({e, comp, field, val});
    }
    void flush(World& world) {
        for (auto& cmd : commands) {
            // 在这里统一应用，此时没有 System 在运行
            world.apply(cmd);
        }
        commands.clear();
    }
};
```

但这只解决了"时序安全"，没有解决"权限安全"。如果 AI 想改渲染相机的视锥参数，或者改物理世界的重力加速度，我们应该允许吗？

### 改进方案 2：权限边界与白名单

每个 AI Agent 在接入时，必须声明自己拥有哪些组件的读写权限。一个负责调优物理参数的 Agent 不应该能修改 UI 布局；一个负责生成关卡的 Agent 不应该能删除玩家的血量组件。

```cpp
struct SandboxConfig {
    std::vector<std::string> allowWrite;  // 允许修改的组件白名单
    std::vector<std::string> denyWrite;   // 明确禁止的组件黑名单
};

enum class PermissionBehavior { Deny, Ask, Allow };

class PermissionEngine {
public:
    PermissionResult evaluate(const std::string& toolName,
                              const std::string& component,
                              const std::string& field);
};
```

权限检查发生在 `CommandBuffer::flush()` 之前。如果某条命令越权，整条命令批次可以被拒绝或挂起等待人工审批。

现在 AI 能安全地修改状态了。但它改了之后，怎么知道"改对了"？

---

## 问题 3：改了之后，怎么确认"改对了"？

假设 AI 把某个敌人的移动速度从 3.0 改成了 5.0，然后运行了 100 帧来观察效果。第二次它想再试一次 4.0 做对比，却发现两次运行的结果完全对不上。

### 最 naive 的方案

"每次运行后用眼睛看结果。"

但 AI 没有眼睛。它只能通过输出数据来判断。如果同样的输入（速度=5.0）运行两遍，最终的位置却不一样，AI 就失去了"控制变量法"——它无法区分"速度改动的效果"和"随机噪声"。

导致不确定性的来源有很多：

- `std::sort` 不稳定，导致遍历顺序不同
- `rand()` 使用了全局种子，不同帧数调用时序不同
- 浮点运算在不同编译器/平台上的结果有微小差异
- 多线程 System 的执行顺序不固定

### 改进方案：确定性执行框架

让引擎成为一台"状态机"：给定相同的初始状态 + 相同的输入序列，输出必须逐位相同。

具体措施：

1. **稳定排序**：所有依赖排序的地方使用稳定排序，或显式按 Entity ID 排序。
2. **固定种子随机数**：不使用全局 `rand()`，每个需要随机性的 System 持有独立的、可播种的随机数生成器。
3. **浮点一致性**：避免不同编译器优化导致的浮点差异；必要时对关键数值做量化（如把浮点截断到固定小数位）。
4. **固定 Tick 顺序**：System 的执行顺序由调度器显式控制，而非依赖容器遍历顺序。
5. **每帧哈希校验**：对关键组件数组计算哈希值，如果两次运行同一帧的哈希不同，立刻能定位不确定性来源。

```cpp
class Scheduler {
    std::vector<std::unique_ptr<System>> systems;
public:
    void tick(World& world, float dt) {
        // 确定性：固定顺序，无隐藏状态
        for (auto& sys : systems) {
            sys->tick(world, dt);
        }
    }
};
```

确定性不仅对 AI 验证改动至关重要，也是网络同步和回放系统的基础（见 [[系统调度与确定性]]）。

但到这里我们只解决了"单个 AI 操作引擎"的场景。如果有两个 AI 同时工作呢？

---

## 问题 4：两个 AI 同时工作，怎么不互相踩坏？

设想一个场景：AI-A 负责优化渲染参数，AI-B 负责调整 gameplay 数值。它们同时在操作引擎。

### 最 naive 的方案

"给整个世界加一把大锁，同一时间只有一个 AI 能操作。"

这确实能防止冲突，但代价是串行化。如果 AI-A 正在批量调整 1000 个材质的参数，AI-B 只能干等。多 Agent 协作的优势荡然无存。

### 改进方案：Orchestrator + 组件级隔离

不要把 AI 当成"在引擎外部操作的黑盒"，而是在架构层面为"多 Agent 并行"预留设计空间。

顶层有一个 **Orchestrator**，负责任务分解和冲突检测。它的核心规则是：

1. **读写隔离**：如果两个 Agent 的写组件集合没有交集，它们可以并行执行。
2. **组件白名单**：每个 Agent 只能看到自己被授权的组件，无法读取或修改其他领域的状态。
3. **冲突合并**：如果两个 Agent 同时修改了同一个实体的同一个字段，Orchestrator 检测到冲突后可以自动合并（如取平均值）、保留后提交的、或挂起等待人工审批。
4. **事务回滚**：每个 Agent 的操作先在一个"影子世界"里预演，确认没有冲突后再合并到主世界。如果冲突无法解决，直接丢弃该 Agent 的这次修改。

```cpp
class AgentBridge {
public:
    // AI 观察世界的接口
    ToolResult query_entities(const std::string& component_mask);
    ToolResult get_component(Entity e, const std::string& comp);

    // AI 操作世界的接口
    ToolResult set_component(Entity e, const std::string& comp,
                             const std::string& json);
    ToolResult step(float dt);
    ToolResult save_snapshot(const std::string& name);
    ToolResult rollback_to_snapshot(const std::string& name);
};
```

引擎对外的唯一接口是结构化的工具调用（如 MCP 协议），不绑定任何特定 AI 客户端。今天是 Claude，明天可以是 Kimi、OpenAI 或自研 Agent。接口的输入输出都是 JSON/Schema，确保 AI 生成合法调用。

---

## 问题 5：如果已有代码不是 ECS，怎么迁移？

你可能已经有一些基于 OOP 或 Manager 模式的代码。直接重写成本太高。怎么办？

迁移的核心思路不是"重写所有代码"，而是"把状态抽出来，把逻辑摊开"。

| 源码中的 OOP 概念 | ECS 中的对应物 | 迁移说明 |
|------------------|---------------|---------|
| `Actor` / `GameObject` 基类 | `Entity`（轻量 ID） | 不再继承行为，而是组合组件。一个实体可以拥有 `Transform` + `Mesh` + `Health`，不需要 `PlayerActor` 子类。 |
| `Actor::OnTick()` 虚函数 | `System::tick(Query<T>)` | 逻辑从对象中剥离，按组件批量执行。 |
| `Manager`（既是系统又是数据管理者） | `System` + `ComponentStorage<T>` | 将"逻辑执行"与"数据管理"彻底分离。 |
| `TickPhase` 硬编码阶段 | `Phase` + 依赖图自动排序 | 从手动维护顺序升级为基于读写 mask 的自动拓扑排序。 |
| 深层的组件指针树 | 平铺的 `ComponentArray<T>` 或 Archetype | 消除指针跳转，提升 cache locality 和 AI 可观测性。 |

> 上表是**迁移参考速查**，不是教学工具。以下解释为什么这些映射是自然的。

OOP 中 `Player` 类继承自 `Actor`，`Actor` 里有一个 `Transform*` 指针——这相当于说"每个 Actor 对象都知道自己的位置"。在 ECS 中，位置不再属于任何对象，而是放在全局的 `ComponentArray<Position>` 里，由 `Entity` ID 索引。`Player` 这个概念消失了，取而代之的是"一个拥有 `Position` + `Health` + `PlayerTag` 的实体"。

这种重构的难点不在代码量，而在**思维转换**：你要停止问"玩家是什么"，开始问"玩家由哪些数据组成"。

### 为什么 ECS 更适合 AI 协作

1. **Cache locality = AI 上下文效率**
   线性扫描连续内存比追逐指针树快几个数量级。AI 查询"所有带 Health 的敌人"时，ECS 是顺序读取，OOP 是随机内存跳转。

2. **组合优于继承 = AI 可预测性**
   AI 修改 `Health` 组件时，它确切地知道自己在改什么。不会意外触发 `DamageableActor::OnHealthChanged()` 里隐藏的副作用。

3. **数据与逻辑分离 = 可验证性**
   给定相同的组件输入数组，System 一定产生相同的输出数组。这是确定性测试和自动 diff 的基础。

---

## 问题 6：以后每新增一个模块，怎么验收？

元规则确立之后，所有后续模块（日志、渲染、物理、网络……）都要接受同一套判据的检验。以下是你在设计任何一个新模块时必须逐条检查的清单。

| 检查项 | 要求 | 本架构的满足方式 |
|--------|------|-----------------|
| **状态平铺** | 该模块的所有可变状态是否可以用 ECS 组件表示？ | `World` 中只有平铺数组，没有深层对象图。 |
| **自描述** | AI 是否可以在不读 C++ 头文件的情况下，通过运行时注册表知道数据结构？ | `TypeRegistry` 提供组件名称、字段列表、类型、偏移量。 |
| **确定性** | 给定相同的输入序列，该模块的输出是否可复现？ | 固定种子随机数、稳定排序、浮点量化、每帧哈希校验。 |
| **工具边界** | 如果 AI 通过 `AgentBridge` / MCP 操作该模块，接口的输入输出是否是结构化的 JSON/Schema？ | 所有工具都有 `inputSchema` 和 JSON 返回值。 |
| **Agent 安全** | 该模块是否支持沙箱、事务或白名单机制，防止 AI 的误操作破坏核心状态？ | `PermissionEngine` + `SandboxConfig` + `CommandBuffer` + `Snapshot` 四层保护。 |

> **核心原则**：任何不能满足上述 5 条红线的模块设计，都需要被重构或添加适配层。

---

## 最小实现骨架：三元组公式落地

把上面的所有讨论浓缩成一个**可以直接编译运行**的 C++ 骨架。它不依赖任何图形 API，但已经具备了 AI 友好引擎最核心的三个能力。

```cpp
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <cassert>

// ============================================================
// 1. ECS 数据模型：所有状态平铺
// ============================================================
using Entity = uint32_t;
constexpr Entity MAX_ENTITIES = 1024;

struct Position { float x{0}, y{0}, z{0}; };
struct Velocity { float x{0}, y{0}, z{0}; };

class World {
public:
    std::vector<Position> positions;
    std::vector<Velocity> velocities;
    std::vector<bool>     alive;

    World() {
        positions.resize(MAX_ENTITIES);
        velocities.resize(MAX_ENTITIES);
        alive.resize(MAX_ENTITIES, false);
    }

    Entity spawn() {
        for (Entity e = 0; e < MAX_ENTITIES; ++e) {
            if (!alive[e]) { alive[e] = true; return e; }
        }
        assert(false); return 0;
    }
    void destroy(Entity e) { alive[e] = false; }
    bool valid(Entity e) const { return e < MAX_ENTITIES && alive[e]; }
};

// ============================================================
// 2. 运行时反射系统：自描述类型注册表
// ============================================================
enum class FieldType { Float };
struct FieldDesc { std::string name; FieldType type; size_t offset; };
struct ComponentDesc {
    std::string name;
    std::vector<FieldDesc> fields;
    size_t size;
};

class TypeRegistry {
public:
    std::unordered_map<std::string, ComponentDesc> components;

    void register_all() {
        components["Position"] = {
            "Position",
            { {"x", FieldType::Float, offsetof(Position, x)},
              {"y", FieldType::Float, offsetof(Position, y)},
              {"z", FieldType::Float, offsetof(Position, z)} },
            sizeof(Position)
        };
        components["Velocity"] = {
            "Velocity",
            { {"x", FieldType::Float, offsetof(Velocity, x)},
              {"y", FieldType::Float, offsetof(Velocity, y)},
              {"z", FieldType::Float, offsetof(Velocity, z)} },
            sizeof(Velocity)
        };
    }
};

// ============================================================
// 3. 确定性执行框架：固定顺序 Tick
// ============================================================
class System {
public:
    virtual ~System() = default;
    virtual void tick(World&, float dt) {}
};

class MovementSystem : public System {
public:
    void tick(World& world, float dt) override {
        for (Entity e = 0; e < MAX_ENTITIES; ++e) {
            if (!world.alive[e]) continue;
            world.positions[e].x += world.velocities[e].x * dt;
            world.positions[e].y += world.velocities[e].y * dt;
            world.positions[e].z += world.velocities[e].z * dt;
        }
    }
};

class Scheduler {
    std::vector<std::unique_ptr<System>> systems;
public:
    void add(std::unique_ptr<System> sys) {
        systems.push_back(std::move(sys));
    }
    void tick(World& world, float dt) {
        for (auto& sys : systems) {
            sys->tick(world, dt);  // 确定性：固定顺序，无隐藏状态
        }
    }
};

// ============================================================
// 主循环
// ============================================================
int main() {
    World world;
    TypeRegistry registry; registry.register_all();
    Scheduler scheduler;
    scheduler.add(std::make_unique<MovementSystem>());

    Entity player = world.spawn();
    world.velocities[player] = {1.0f, 0.0f, 0.0f};

    for (int i = 0; i < 3; ++i) {
        scheduler.tick(world, 0.016f);
        auto* t = &world.positions[player];
        std::cout << "Frame " << i << ": pos = (" << t->x << ", " << t->y << ")\n";
    }
    return 0;
}
```

这个最小实现解决了什么、还缺什么，用下面两张表总结。

**已解决的能力**：

| 能力 | 说明 |
|------|------|
| **状态完全平铺** | `World` 里的所有数据都是连续数组，可直接序列化、diff、分析 |
| **零指针追踪** | 没有深层对象树、没有虚函数副作用，不需要"猜"引用关系 |
| **运行时自描述** | `TypeRegistry` 让外部工具知道每个组件有哪些字段、偏移量和类型 |
| **确定性执行** | `scheduler.tick(world, dt)` 是纯函数式调用，输入固定则输出固定 |

**明确留到后续阶段的目标**（不是缺陷，是路线图）：

- **自动化反射**：`TypeRegistry` 还是手写的，真实引擎需要宏或代码生成（见 [[反射系统]]）
- **系统依赖排序**：`PhysicsSystem` 必须在 `MovementSystem` 之前执行时，当前代码无法表达（见 [[系统调度与确定性]]）
- **增量变更日志**：每次都要全量扫描世界才能知道发生了什么变化（见 [[系统调度与确定性]]）
- **快照与回滚**：还不能"尝试一个改动，不满意就撤销"（见 [[系统调度与确定性]]）
- **AI 操作边界**：还没有 `AgentBridge` 和 MCP 接口（见 [[MCP与Agent桥接层]]）

---

## 下一步预告

现在你已经确立了自研引擎的**元规则**：

- ✅ 选择 ECS 而非 OOP，因为状态需要对 AI 可见
- ✅ 建立运行时反射，因为 AI 不能读头文件
- ✅ 保证确定性执行，因为 AI 需要验证改动效果
- ✅ 预设 AgentBridge 和 MCP 边界，因为 AI 需要安全的操作入口
- ✅ 规划多 Agent 编排，因为未来不是单 Agent 在战斗

这些原则将作为判据，贯穿后续每一个模块的设计决策。

**阶段 0 已经完成。下一步进入阶段 1：让引擎拥有一个真正的窗口。**

下一篇：[[窗口与输入系统]] — 我们将创建第一个可交互窗口、建立事件循环、计算 DeltaTime，为所有后续可视化调试打下基础。

> [[Notes/SelfGameEngine/0_RoadMap|← 返回 SelfGameEngine 构建手册]]
