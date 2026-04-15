---
title: 最小ECS数据层
date: 2026-04-15
tags:
  - self-game-engine
  - ECS
  - architecture
  - foundation
aliases:
  - Minimal ECS Data Layer
---

> **前置依赖**：[[窗口与输入系统]]、[[可视化日志系统]]
> **本模块增量**：建立 World、Entity、ComponentArray 三个最基础的数据结构，让引擎第一次拥有"实体-组件"状态骨架。
> **下一步**：[[极简Inspector与ECS可视化]] — 没有 Inspector 的 ECS 是黑盒，下一章在窗口里直接观察 ECS 世界。

---

## Why：为什么 ECS 数据层是引擎的绝对起点？

在写一个能跑的引擎之前，你必须先回答一个问题：**"这个世界里的东西是什么？它们的状态存在哪里？"**

### 没有这个层时，世界有多糟

1. **对象是黑盒**
   - `class Player : public Actor : public GameObject` 继承链越深，状态藏得越隐蔽。你想知道"这个世界里有多少个敌人"，必须遍历所有 `Actor*`，动态类型转换，逐个检查。

2. **内存不可预测**
   - 每个对象 `new` 在堆的随机位置。批量处理时 CPU 在指针森林里跳来跳去，cache miss 严重。

3. **删除即灾难**
   - `std::vector<Enemy*>` 中删掉一个敌人，后面的指针全部错位。其他子系统如果还持有旧指针，下一帧就是崩溃或悬空访问。

### 这是"必须"还是"优化"

**必须**。ECS 数据层不是性能优化，而是让引擎状态**可见、可追踪、可批量操作**的基础设施。没有它，上层的一切（渲染、物理、AI 桥接）都无从谈起。

> **核心结论**：ECS 数据层是引擎的"地基建好了没"。在它跑通之前，不要引入渲染、网络或复杂调度。

---

## What：最小 ECS 数据层长什么样？

下面的代码是一个**真正可以编译运行**的极简 ECS 数据层，总计约 120 行。它包含三个核心能力：

1. **Entity 是轻量 ID（带 generation 防悬空）**
2. **每个组件类型有自己的 `ComponentArray<T>`（Sparse Set 风格）**
3. **World 统一管理 Entity 和组件数组的生命周期**

### Generation 防悬空：为什么 ID 不够，还要计数器？

> [!note] 核心机制
> 如果 `Entity` 只有 `id`，销毁实体后该 id 会被回收到空闲池。当新实体复用这个 id 时，持有旧句柄的子系统就会**不知不觉访问到错误实体**。
>
> `generation` 是一个只增不减的计数器，绑定在 `id` 上。每次销毁实体时，`entityGenerations[id]` 递增；创建新实体复用旧 `id` 时，新实体继承最新的 `generation`。旧句柄的 `generation` 永远落后，于是 `valid()` 校验自然失败，安全拒绝访问。

**具体例子：**

| 步骤        | 没有 generation      | 有 generation                       |
| ----------- | -------------------- | ----------------------------------- |
| 创建玩家    | `player = {id: 5}`   | `player = {id: 5, generation: 0}`   |
| 销毁玩家    | id 5 回收到空闲池    | `entityGenerations[5]` 从 `0` → `1` |
| 创建敌人    | `enemy = {id: 5}`    | `enemy = {id: 5, generation: 1}`    |
| AI 持旧句柄访问 | 默默改到敌人身上（悬空） | `valid(player)` 发现 `0 != 1`，拒绝访问 |

```cpp
#include <cstdint>
#include <vector>
#include <array>
#include <unordered_map>
#include <cassert>
#include <iostream>

// ============================================================
// 1. 轻量 Entity：ID + Generation（防止访问已销毁实体）
// ============================================================
struct Entity {
    uint32_t id{0xFFFFFFFF};
    uint32_t generation{0};
    bool valid() const { return id != 0xFFFFFFFF; }
    bool operator==(const Entity& o) const { return id == o.id && generation == o.generation; }
};

// ============================================================
// 2. 组件数据（纯 POD，无虚函数）
// ============================================================
struct Position { float x{0}, y{0}, z{0}; };
struct Velocity { float x{0}, y{0}, z{0}; };
struct Health   { float current{100.0f}; float max{100.0f}; };
```

### Sparse Set：为 ECS 量身定做的组件存储

在把组件存进 `World` 之前，必须先回答一个关键问题：**如何把一个轻量的 `Entity`（只是个整数 ID）映射到实际的组件数据上？**

最直觉的做法是用 `std::unordered_map<Entity, T>`，但它有两个致命缺陷：
1. **Cache 不友好**：哈希表节点散落在堆内存中，`System` 批量迭代时每一步都是指针追逐。
2. **无法线性扫描**：如果你想"遍历所有 `Position` 组件"，只能遍历哈希表的 bucket，性能远低于数组。

另一个想法是 `std::vector<std::optional<T>>`，以 `entity.id` 作为数组下标。这确实支持线性扫描，但问题是：
- 实体 ID 可能非常稀疏，数组会膨胀成巨大的空洞。
- 增删实体时需要在数组中间留下 `nullopt` 空洞，无法保证"真正存在的组件"在内存中连续排列。

**Sparse Set（稀疏集）** 正是为了解决这个矛盾而生的数据结构。它的核心思想用两张表协作：
- **稀疏表（sparse）**：以 `entity.id` 为下标，只存一个整数索引（或无效标记）。这张表可以很大，但每个元素只有 4 字节，允许存在空洞。
- **密集表（dense）**：实际存放组件数据的连续数组。只有"真正拥有该组件的实体"才会被记录在这里，完全无空洞，完美 Cache 友好。

于是，从 `Entity` 到组件数据的查找路径变成：
1. `sparse[entity.id]` → 拿到该实体在 dense 数组中的索引。
2. `dense[index]` → 直接访问组件数据。

这种结构同时满足了三个需求：
- **O(1) 随机访问**：查 `sparse` 表再取 `dense` 元素，两步都是 O(1)。
- **O(1) 增删**：添加时 `dense.push_back`；删除时用 `dense` 末尾元素填充被删位置，更新对应 `sparse` 索引即可。
- **完美 Cache 友好的批量迭代**：`System` 只需要线性扫描 `dense` 数组，不需要关心稀疏的实体 ID 空间。

下面的 `ComponentArray<T>` 就是 Sparse Set 思想在 ECS 组件存储中的最小实现：

```cpp
// ============================================================
// 3. ComponentArray：Sparse Set 风格的密集存储
// ============================================================
template<typename T>
class ComponentArray {
    // entityIndex[entity.id] = 该 entity 在 dense 数组中的索引（或无效标记）
    std::vector<int32_t> entityIndex;
    std::vector<T> dense;           // 实际组件数据，连续存储
    std::vector<uint32_t> denseEntityIds; // dense[i] 对应哪个 entity.id

public:
    void set(Entity e, const T& value) {
        if (e.id >= entityIndex.size()) entityIndex.resize(e.id + 1, -1);
        int32_t idx = entityIndex[e.id];
        if (idx == -1) {
            idx = static_cast<int32_t>(dense.size());
            entityIndex[e.id] = idx;
            dense.push_back(value);
            denseEntityIds.push_back(e.id);
        } else {
            dense[idx] = value;
        }
    }

    void remove(Entity e) {
        if (e.id >= entityIndex.size() || entityIndex[e.id] == -1) return;
        int32_t idx = entityIndex[e.id];
        int32_t lastIdx = static_cast<int32_t>(dense.size()) - 1;
        // 用最后一个元素填充被删除的位置，保持密集
        dense[idx] = dense[lastIdx];
        denseEntityIds[idx] = denseEntityIds[lastIdx];
        entityIndex[denseEntityIds[lastIdx]] = idx;
        dense.pop_back();
        denseEntityIds.pop_back();
        entityIndex[e.id] = -1;
    }

    bool has(Entity e) const {
        return e.id < entityIndex.size() && entityIndex[e.id] != -1;
    }

    T* get(Entity e) {
        if (!has(e)) return nullptr;
        return &dense[entityIndex[e.id]];
    }

    size_t count() const { return dense.size(); }
    T* data() { return dense.data(); }
    uint32_t* entityIds() { return denseEntityIds.data(); }
};

// ============================================================
// 4. World：统一管理 Entity 和 ComponentArray
// ============================================================
class World {
    std::vector<uint32_t> entityGenerations;
    std::vector<uint32_t> freeEntities;

public:
    ComponentArray<Position> positions;
    ComponentArray<Velocity> velocities;
    ComponentArray<Health>   healths;

    Entity create() {
        if (!freeEntities.empty()) {
            uint32_t id = freeEntities.back(); freeEntities.pop_back();
            return {id, entityGenerations[id]};
        }
        uint32_t id = static_cast<uint32_t>(entityGenerations.size());
        entityGenerations.push_back(0);
        return {id, 0};
    }

    void destroy(Entity e) {
        if (e.id >= entityGenerations.size() || entityGenerations[e.id] != e.generation) return;
        positions.remove(e);
        velocities.remove(e);
        healths.remove(e);
        entityGenerations[e.id]++;
        freeEntities.push_back(e.id);
    }

    bool valid(Entity e) const {
        return e.id < entityGenerations.size() && entityGenerations[e.id] == e.generation;
    }
};

// ============================================================
// 5. 最简单的 System：批量迭代组件
// ============================================================
class MovementSystem {
public:
    void tick(World& world, float dt) {
        size_t n = world.positions.count();
        Position* pos = world.positions.data();
        uint32_t* ids = world.positions.entityIds();
        for (size_t i = 0; i < n; ++i) {
            if (Velocity* vel = world.velocities.get({ids[i], world.entityGenerations[ids[i]]})) {
                pos[i].x += vel->x * dt;
                pos[i].y += vel->y * dt;
                pos[i].z += vel->z * dt;
            }
        }
    }
};

// ============================================================
// 主循环
// ============================================================
int main() {
    World world;
    MovementSystem movement;

    Entity player = world.create();
    world.positions.set(player, {0, 0, 0});
    world.velocities.set(player, {1, 0, 0});
    world.healths.set(player, {80, 100});

    for (int i = 0; i < 3; ++i) {
        movement.tick(world, 0.016f);
        auto* p = world.positions.get(player);
        std::cout << "Frame " << i << ": (" << p->x << ", " << p->y << ")\n";
    }

    world.destroy(player);
    return 0;
}
```

### 这个最小实现已经解决了什么

| 能力                   | 说明                                                |
| -------------------- | ------------------------------------------------- |
| **Entity 是真正的轻量 ID** | 只有 8 字节（ID + generation），不携带任何状态                  |
| **组件密集存储**           | `ComponentArray<T>::dense` 是连续数组，批量迭代 cache 友好    |
| **快速增删**             | Sparse Set 机制让组件增删都是 O(1)，且不影响其他组件                |
| **防悬空访问**            | 销毁 Entity 时 generation 递增，旧 Entity 值自然失效          |
| **纯数据组件**            | `Position`、`Velocity`、`Health` 都是 POD，可直接序列化、diff |

### 这个最小实现还缺什么

- **没有多组件联合查询**：`MovementSystem` 里需要手动检查 `velocities.get()`，无法表达 "给我所有同时有 Position 和 Velocity 的实体"
- **没有组件类型注册表**：`World` 里还是硬编码了 `positions`、`velocities`、`healths`
- **没有命令缓冲**：运行时增删组件会直接修改 `ComponentArray`
- **没有 Archetype 分组**：虽然同类型组件连续，但不同组件类型之间没有按 Entity 组合聚类

这些正是 [[组件系统架构]] 要演进的步骤。

---

## 状态变化图：remove 时的 swap-and-pop

这是理解 `ComponentArray` 内存行为的关键。以下展示了 `dense` 和 `sparse` 在删除元素前后的精确状态：

```
操作前（dense 有 4 个元素）：
  entityIndex: [FF, FF, 0, 1, 2, 3, FF...]
  dense:       [E2, E3, E4, E5]   // E2 在索引 0，E5 在索引 3
  denseIds:    [2,  3,  4,  5 ]

Remove(E3) —— id=3 在 dense 中的索引是 1：
  entityIndex: [FF, FF, 0,  FF, 2, 3, FF...]  // E3 被标记为无效
  dense:       [E2, E5, E4]                  // 末尾元素 E5 swap 到索引 1
  denseIds:    [2,  5,  4]                   // 同步更新

注意：E5 现在从索引 3 移动到了索引 1，所以 entityIndex[5] 从 3 更新为 1。
```

> **诚实性说明**：swap-and-pop 保持了 `dense` 数组的紧凑性，但**被移动元素（本例中的 E5）的内存位置发生了变化**。如果你在其他地方保存了指向 E5 组件的裸指针，这个指针会在 remove(E3) 后失效。在纯 ECS 中，你应该始终通过 `Entity` 或 `dense` 索引访问组件，不要长期持有组件裸指针。

---

## How：真实引擎的 ECS 数据层是如何一步一步复杂起来的？

### 阶段 1：最小实现 → 能用（解决 Query 和注册表问题）

#### 触发原因
- 当组件类型超过 5 个，硬编码 `World` 变得不可维护
- `MovementSystem` 手动检查 `velocities.get()` 写法笨拙，需要表达"同时有 Position 和 Velocity 的实体"

#### 代码层面的变化
1. **引入 `ComponentTypeId` 和全局注册表**
   - 每个组件类型分配一个递增的整数 ID
   - `World` 内部用 `std::vector<IComponentArray*>` 存储所有组件数组，不再硬编码

2. **引入 `Query<Position, Velocity>`**
   - 通过遍历最小的组件数组，再检查其他组件是否存在，实现多组件联合查询
   - 这是从"手动 get"到"声明式查询"的关键跃迁

### 阶段 2：能用 → 好用（解决 Cache 和迭代效率问题）

#### 触发原因
- Sparse Set 虽然让同类型组件连续，但一个 Entity 的 `Position` 和 `Velocity` 可能相距甚远
- System 每次迭代都需要两次独立的 `dense` 数组访问，无法充分利用 CPU cache

#### 代码层面的变化
1. **Archetype 存储模型**
   - 把"拥有相同组件组合的 Entity"分到同一个 Archetype 中
   - 每个 Archetype 内部用 Chunk 存储实体数据，`Position` 和 `Velocity` 在内存上相邻

2. **Query 缓存**
   - 维护一个 Archetype 匹配缓存，新实体加入时只需检查是否需要加入已知 Query
   - 避免每帧全量扫描所有 Archetype

### 阶段 3：好用 → 工业级（解决并发和规模问题）

#### 触发原因
- 实体数量超过 10 万，单线程迭代成为瓶颈
- 多线程读写同一组件类型需要严格的依赖分析和隔离

#### 代码层面的变化
1. **Chunk-based SoA 布局**
   - 每个 Chunk 固定大小（如 16KB），刚好填满一个或几个 cache line
   - 组件按 SoA（Structure of Arrays）排列，批量运算时 SIMD 友好

2. **基于读写掩码的依赖调度**
   - 每个 System 声明自己读哪些组件、写哪些组件
   - 调度器自动分析无依赖的 System，分配到不同线程并行执行

---

## AI 友好设计检查清单

| 检查项 | 本模块的实现 | 说明 |
|--------|-------------|------|
| **状态平铺** | ✅ 完全平铺 | 所有状态都在 `ComponentArray` 中，没有隐藏在对象树里 |
| **自描述** | ⚠️ 萌芽阶段 | 当前是硬编码组件，但结构足够简单，后续可用 [[反射系统]] 自动注册 |
| **确定性** | ✅ 固定顺序遍历 | `dense` 数组线性扫描，无随机指针跳转 |
| **工具边界** | ⚠️ 尚未实现 | 需要等待 [[MCP与Agent桥接层]] 的 `query_entities` 工具 |
| **Agent 安全** | ⚠️ 尚未实现 | 需要等待 [[系统调度与确定性]] 中的 `CommandBuffer` |

---

## 设计权衡表

| 决策点       | 原型阶段（< 1000 实体） | 工业级阶段（> 10 万实体）     |
| ------------ | ---------------------- | ----------------------------- |
| 存储模型     | `ComponentArray<T>` Sparse Set | Archetype + Chunk-based SoA   |
| Entity 校验  | generation 计数器      | generation + 版本化 Archetype 索引 |
| 查询方式     | 硬编码遍历 + 手动 `has()` 检查 | Query 缓存 + Archetype Graph 快速匹配 |
| 并发安全     | 单线程串行             | 基于组件读写 mask 的依赖分析 + Job System |

---

## 如果我要 vibe coding，该偷哪几招？

1. **Entity 必须是轻量 ID，不能是对象**
   - 任何把"实体"设计成类的冲动都要压下去。实体只是一个整数，状态全部在组件里。

2. **组件必须是纯 POD，不能带虚函数**
   - 虚函数表 = 不可序列化 = AI 不可预测。初始化/清理逻辑交给 System。

3. **用 Sparse Set 做 ComponentArray 的起步实现**
   - 它比哈希表快，比 Archetype 简单，是原型阶段的最佳选择。

4. **generation 从 Day 1 就要有**
   - 不要等到出 bug 才加。它是防止悬空访问的最便宜方式。

---

> **下一步**：[[极简Inspector与ECS可视化]]
>
> 现在你已经有了最简陋但可运行的 ECS 数据层。下一步是在阶段 1 的窗口里，通过 ImGui 面板直接观察 ECS 世界状态：Entity 列表、组件字段、实时增删。没有可视化调试的 ECS 只是黑盒。
