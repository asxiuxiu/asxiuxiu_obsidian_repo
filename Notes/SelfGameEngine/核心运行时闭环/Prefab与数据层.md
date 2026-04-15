---
title: Prefab 与数据层
date: 2026-04-15
tags:
  - self-game-engine
  - ecs
  - prefab
  - serialization
aliases:
  - Prefab and Data Layer
---

# Prefab 与数据层

> **前置依赖**：[[事件总线]]、[[反射系统]]、[[文件IO与虚拟文件系统]]
> **本模块增量**：在 ECS 骨架上增加 Prefab 模板、实例化与覆盖、场景的序列化与反序列化能力。世界从此可以被保存、加载和复用。
> **下一步**：[[实体生命周期与注册表]]，因为 Prefab 实例化本质上是批量创建实体，需要完善的生成回收机制作为支撑。

---

## Why：为什么 ECS 引擎需要 Prefab 与数据层？

在 [[事件总线]] 中，System 之间可以解耦通信了。但如果每次启动引擎都要用 C++ 代码硬编码"在这里创建一个玩家、在那里创建一个敌人"，这个世界就永远无法被美术、策划或 AI Agent 编辑。

### 场景 1：同一个敌人在关卡中出现 50 次

你设计了一个"史莱姆"敌人，它有 `Health`、`EnemyAI`、`Mesh`、`Collider` 四个组件。如果没有 Prefab，你需要在代码里手写 50 次创建逻辑。更糟的是，当你想给所有史莱姆增加一个 `Loot` 组件时，要找到并修改 50 个创建点。

### 场景 2：策划调整了一个数值，重启游戏才能验证

策划把史莱姆的 `max_health` 从 100 改成 80。由于这个数值硬编码在 C++ 的 `create_slime()` 函数里，他必须找你改代码、重新编译、重启游戏。这在迭代期是致命的效率杀手。

### 场景 3：保存当前场景，明天继续编辑

编辑器里花了一下午摆好的道具、灯光、摄像机位置，关闭程序后就全部丢失。没有场景序列化，编辑器只是一个昂贵的玩具。

> **核心需求**：引擎需要一种**数据驱动的实体模板（Prefab）**，以及一种**将 ECS 世界持久化到磁盘并从磁盘恢复**的机制。这是从"玩具"走向"可生产工具"的关键一步。

---

## What：最简化版本的 Prefab 与场景序列化长什么样？

我们先实现一个基于 **JSON** 的 Prefab 和场景系统。JSON 对人类可读、对 AI 友好、不需要手写二进制解析器。

### Prefab 文件格式（`slime.json`）

```json
{
  "name": "Slime",
  "components": {
    "HealthComponent": { "current": 100, "max": 100 },
    "EnemyAIComponent": { "aggressive": true, "patrol_radius": 5.0 },
    "MeshComponent": { "mesh_path": "Assets/mesh/slime.obj" },
    "ColliderComponent": { "radius": 0.5 }
  },
  "children": []
}
```

### 场景文件格式（`level_01.json`）

```json
{
  "entities": [
    {
      "id": 0,
      "prefab": "Slime",
      "override": {
        "TransformComponent": { "position": [10, 0, 5] }
      }
    },
    {
      "id": 1,
      "prefab": "Slime",
      "override": {
        "TransformComponent": { "position": [20, 0, 8] },
        "HealthComponent": { "max": 150 }
      }
    },
    {
      "id": 2,
      "name": "MainLight",
      "components": {
        "TransformComponent": { "position": [0, 10, 0] },
        "LightComponent": { "color": [1, 1, 1], "intensity": 1.0 }
      }
    }
  ]
}
```

### C++ 最小实现

```cpp
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------- Prefab 定义 ----------
struct Prefab {
    std::string name;
    json components;           // 组件默认值
    std::vector<std::string> children; // 子 Prefab 名称（简化版）
};

struct PrefabRegistry {
    std::unordered_map<std::string, Prefab> prefabs;

    void load_prefab(const std::string& name, const std::string& json_path) {
        std::ifstream f(json_path);
        json j = json::parse(f);
        prefabs[name] = { name, j["components"], {} };
    }

    const Prefab* find(const std::string& name) const {
        auto it = prefabs.find(name);
        return (it != prefabs.end()) ? &it->second : nullptr;
    }
};

// ---------- 场景加载器 ----------
struct SceneLoader {
    PrefabRegistry& prefabs;
    World& world;

    Entity instantiate_prefab(const std::string& prefab_name, const json& override_json) {
        const Prefab* prefab = prefabs.find(prefab_name);
        if (!prefab) return INVALID_ENTITY;

        Entity e = world.create_entity();

        // 1. 应用 Prefab 默认组件
        for (auto& [comp_name, comp_data] : prefab->components.items()) {
            json final_data = comp_data;
            // 2. 应用覆盖
            if (override_json.contains(comp_name)) {
                for (auto& [key, val] : override_json[comp_name].items()) {
                    final_data[key] = val;
                }
            }
            // 3. 通过反射系统创建组件
            world.add_component_from_json(e, comp_name, final_data);
        }
        return e;
    }

    void load_scene(const std::string& scene_path) {
        std::ifstream f(scene_path);
        json scene = json::parse(f);

        for (auto& entry : scene["entities"]) {
            if (entry.contains("prefab")) {
                json override_json = entry.value("override", json::object());
                instantiate_prefab(entry["prefab"], override_json);
            } else {
                // 裸实体（无 Prefab）
                Entity e = world.create_entity();
                for (auto& [comp_name, comp_data] : entry["components"].items()) {
                    world.add_component_from_json(e, comp_name, comp_data);
                }
            }
        }
    }
};
```

这个最小实现解决了什么？
- **实体模板复用**：`Slime` Prefab 可以实例化任意多次，修改一次 Prefab 文件影响所有实例。
- **场景级覆盖**：每个实例可以有自己的独特覆盖（如位置、血量），而不影响其他实例。
- **纯数据驱动**：新增敌人类型不需要改 C++ 代码，只需要添加一个新的 JSON 文件。
- **AI 可编辑**：AI Agent 可以直接读写 JSON 文件来修改场景，无需理解 C++。

还缺什么？
- **嵌套 Prefab**：子实体的 `children` 列表只是字符串名，没有完整的嵌套覆盖机制。
- **运行时引用稳定**：实例化后没有建立"这个实体是 Slime Prefab 的第 3 个实例"的跟踪关系，后续修改 Prefab 无法同步到已存在的实例。
- **没有场景保存**：只有加载逻辑，没有将当前 ECS 世界写回 JSON 的能力。
- **JSON 解析性能**：对于大型场景（数万实体），JSON 解析速度远低于二进制格式。

---

## How：真实引擎的 Prefab 与数据层是如何一步一步复杂起来的？

### 阶段 1：从 JSON 文件 → 结构化资产 + 覆盖规则（解决嵌套和引用稳定性）

**触发原因**：当 Prefab 开始嵌套（如"史莱姆之王" Prefab 包含一个"史莱姆"子 Prefab 和一个"皇冠"子 Prefab），简单的 JSON 合并会导致覆盖规则混乱。而且，你需要知道"场景中的实体 E42 是 Prefab 'Slime' 的实例"，以便在 Prefab 源文件更新时同步修改实例。

**代码层面的变化**：

1. **引入 PrefabInstance 组件**
   ```cpp
   struct PrefabInstance {
       AssetID prefab_id;      // 指向源 Prefab 资产
       uint32_t instance_index;// 在该 Prefab 中的实例索引（用于子实体映射）
   };
   ```
   每个从 Prefab 实例化出来的实体都带有一个 `PrefabInstance` 组件。这建立了从运行时实体到源资产的稳定反向链接。

2. **覆盖规则表（Override Table）**
   不再把覆盖直接写到实体的组件上，而是单独存储：
   ```cpp
   struct PrefabOverride {
       Entity target_entity;
       std::string component_name;
       std::string field_path;  // 支持嵌套字段，如 "transform/position/x"
       json value;
   };
   ```
   这种分离的好处是：
   - 当源 Prefab 更新时，可以重新实例化并重新应用覆盖表。
   - 在编辑器中，用户可以清晰看到"这个字段是覆盖的"还是"继承自 Prefab 默认值的"。

3. **嵌套实例化与作用域隔离**
   实例化一个 Prefab 时，为其分配一个唯一的 `scope_id`（如自增整数）。Prefab 内部所有子实体的 EntityID 都是在这个 scope 下生成的，避免了不同 Prefab 实例之间的 ID 冲突。

**状态变化图：Prefab 实例化时的组件合并**

假设 `Slime` Prefab 的默认组件：
```
HealthComponent:    { current: 100, max: 100 }
TransformComponent: { position: [0,0,0], scale: [1,1,1] }
```

场景中对实例 E5 的覆盖：
```
override[E5]: TransformComponent.position = [10, 0, 5]
override[E5]: HealthComponent.max = 150
```

实例化后的最终组件状态：
```
E5.HealthComponent:    { current: 100, max: 150 }
E5.TransformComponent: { position: [10,0,5], scale: [1,1,1] }
```

> **精确性补充**：注意这里的关键是**覆盖发生在实例化时，而不是运行时持续合并**。一旦实体被创建，它的组件值就是普通 ECS 组件。`PrefabOverride` 表只在加载/重载 Prefab 时被读取，不会在每帧 tick 中消耗性能。

---

### 阶段 2：从只读加载 → 场景保存与增量 Diff（解决持久化）

**触发原因**：编辑器需要保存用户摆放的物体、修改的覆盖值，以及新增的裸实体。

**代码层面的变化**：

1. **场景保存器（SceneSaver）**
   遍历 ECS 世界中的所有实体，分类输出：
   - 带有 `PrefabInstance` 的实体 → 输出为 `{"prefab": "...", "override": {...}}`
   - 没有 `PrefabInstance` 的裸实体 → 输出为完整的 `{"components": {...}}`
   - 父子层级关系 → 输出为 `{"parent": entity_index}`

2. **自动生成覆盖**
   保存时，对于带有 `PrefabInstance` 的实体，将其当前组件值与源 Prefab 默认值做对比，只记录差异：
   ```cpp
   for (const auto& field : type_desc.fields) {
       void* instance_value = component_ptr + field.offset;
       void* prefab_value   = prefab_component_ptr + field.offset;
       if (memcmp(instance_value, prefab_value, field.size) != 0) {
           override_table.add(entity, comp_name, field.name, serialize(instance_value, field.type));
       }
   }
   ```
   这使得场景文件保持最小化，不会把 Prefab 的默认值重复存储一万次。

3. **增量保存与版本控制友好**
   场景文件采用人类可读的 JSON 或 TOML，而不是二进制。这样：
   - 策划修改了一个数值，git diff 只有一行变化。
   - AI Agent 可以直接用文本工具编辑场景文件。
   - 加载时如果字段缺失，使用 Prefab 默认值填充，实现向前兼容。

> **性能诚实**：
> - "文本格式"不是零开销。大型场景（10 万实体）的 JSON 加载可能需要数秒，且文件体积是二进制格式的 5~10 倍。
> - **优化策略**：编辑器使用文本格式保证可读性和版本控制；运行时发布时使用二进制格式（如 MessagePack 或自定义块格式）加速加载和减小包体。

---

### 阶段 3：工业级 → 数据层（DataLayer）、动态加载与热重载

**触发原因**：开放世界不需要一次性加载整个场景。需要按区域、按任务、按视线动态加载和卸载数据块。同时，美术修改了 Prefab 后，编辑器中的实例应该实时更新，不需要重启引擎。

**代码层面的变化**：

1. **数据层（DataLayer）**
   将世界划分为多个独立的 `DataLayer`，每个 Layer 对应一个场景文件或一个程序化生成区域：
   ```cpp
   struct DataLayer {
       std::string name;
       std::string source_file;
       std::vector<EntityID> entities;
       bool is_loaded = false;
   };
   ```
   - 玩家接近某个区域时，`DataLayerManager` 加载对应的 Layer。
   - 玩家远离时，Layer 被卸载，其所有实体被批量销毁。
   - 不同 Layer 的实体可以互相引用（如一个全局任务触发器引用某个 Layer 中的 NPC），通过 `DataReferenceManager` 做延迟解析。

2. **Prefab 热重载（Hot Reload）**
   编辑器监控 Prefab 源文件的修改时间。当文件变化时：
   - 重新加载 Prefab 默认值。
   - 遍历所有带 `PrefabInstance` 且指向该 Prefab 的实体。
   - 对每个实体：保留覆盖值，重新应用新的默认值，重建组件。
   - 通过 [[事件总线]] 发布 `PrefabReloadedEvent`，通知渲染、物理等 System 刷新内部缓存。

3. **运行时资产加载（Async Loading）**
   Prefab 中的 `MeshComponent.mesh_path` 可能引用一个尚未加载的模型文件。实例化时不阻塞等待模型加载，而是：
   - 立即创建占位 Entity 和 `MeshComponent`（使用默认立方体或空句柄）。
   - 向 `AssetManager` 提交异步加载请求。
   - 资产加载完成后，通过事件总线或 System tick 更新 `MeshComponent` 的实际资源句柄。

---

## 从源码到 ECS 的重构映射

chaos 引擎的 Prefab 系统依赖 `PrefabRSA`（Sketum 二进制资产）、`EntityProxy`（重量级代理对象）和 `DataReferenceManager`（XOR DataRef 映射）。迁移到 ECS 时，核心思想可以保留，但实现应大幅简化：

| chaos 源码结构 | ECS 等价物 | 迁移说明 |
|---------------|-----------|---------|
| `PrefabRSA` + `InstanceData` | **JSON/TOML Prefab 文件** + **`PrefabRegistry`** | 不使用 Sketum 二进制格式，改用人类可读的文本格式和通用 JSON 库。 |
| `EntityProxy`（含 `m_instance_data`、`m_prefab_instance_data`）| **`PrefabInstance` 组件** + 普通 ECS 实体 | 将 Proxy 中的元数据拆分为一个轻量组件，实体本身由 ECS World 管理，不再需要一个重量级代理对象。 |
| `DataRef`（XOR 生成全局唯一 ID）| **`scope_id` + `instance_index`** 或直接使用 **EntityID** | ECS 的 EntityID 已经是全局唯一的，不需要额外的 XOR DataRef。对于跨 Layer 引用，可以存储 `(layer_name, entity_index)` 并在加载时解析。 |
| `ScopeInstanceCollector`（两阶段：收集 + 批量创建）| **命令缓冲（CommandBuffer）批量实例化** | 利用 [[系统调度与确定性]] 中的命令缓冲，在单一安全点内批量创建 Prefab 实例的所有实体和组件。 |
| `DataLayer` + `DataLayerManager` | **SceneLayer** + **LayerManager** | 保留按区域加载/卸载的思想，但 Layer 只管理实体列表，不持有 `EntityProxy` 或其他 OOP 状态。 |
| `SketumBinaryLoader`（零开销模板特化）| **JSON/TOML 解析器** + **反射驱动的组件反序列化** | 对于 vibe coding 和 AI 友好性，优先使用文本格式。发布时可选编译为二进制（如 MessagePack）。 |

> **Why ECS 更适合这个模块？**
> - **状态平铺**：chaos 的 `EntityProxy` 是一个重达数百字节的 OOP 对象，包含大量 Prefab 元数据。ECS 中这些元数据只需要一个 `PrefabInstance` 组件（16 字节左右），其余都是普通组件。
> - **批量生命周期管理**：chaos 卸载 `DataLayer` 时需要逐个调用 `EntityProxy::deleteEntityProxy`。ECS 中可以直接遍历 `Layer.entities` 数组，批量提交销毁命令，效率更高且更确定。
> - **热重载更简单**：chaos 的热更新需要处理 `EntityProxy` 中的多重句柄和旧系统兼容字段。ECS 中热重载 Prefab 只需要遍历 `Query<PrefabInstance>`，重新创建组件并应用覆盖值即可。

---

## AI 友好设计检查清单

| 检查项 | 本模块的实现策略 |
|--------|-----------------|
| **状态平铺** | ✅ Prefab 默认值存储在只读的 `PrefabRegistry` 中。场景中的覆盖值是 ECS 组件的普通字段或独立的 `Override` 组件。没有隐藏状态。 |
| **自描述** | ✅ Prefab 和场景文件都是人类可读的 JSON/TOML。AI 可以直接读取、理解并修改这些文件，无需读 C++ 头文件。 |
| **确定性** | ✅ 给定相同的 Prefab 文件和相同的场景文件，加载后的 ECS 世界状态 bit 级一致。实例化顺序由场景文件中的实体列表顺序唯一确定。 |
| **工具边界** | ✅ MCP 接口可暴露：`load_scene(path)`、`save_scene(path)`、`instantiate_prefab(name, position)`、`reload_prefab(name)`。 |
| **Agent 安全** | ✅ DataLayer 的加载/卸载操作通过命令缓冲延迟执行。AI 可以请求加载一个新 Layer，实际生效在下一帧的安全点，不会破坏当前遍历中的 System。 |

---

## "如果我要 vibe coding，该偷哪几招？"

1. **先用 JSON，不要先写二进制格式**
   在引擎早期，可读性和调试效率比加载速度重要 100 倍。JSON 让你能用文本编辑器直接修改场景，AI 也能直接生成和修改场景文件。等 Profiler 证明加载是瓶颈时，再引入二进制发布格式。

2. **Prefab 就是 JSON，不要造复杂的资产管线**
   如果你的引擎还没有专职工具程序员，不要学大型商业引擎做复杂的资产 Cook 管线。一个文件夹里的 JSON 文件就是一个 Prefab 库，直接读取即可。

3. **覆盖值必须可见**
   在 Inspector 中，继承自 Prefab 默认值的字段应该显示为灰色，覆盖值显示为白色。这让策划和 AI 都能一眼看出"哪些是我改过的"。

4. **给每个 Layer 一个独立的场景文件**
   不要把整个关卡塞进一个巨大的 JSON。按功能或区域拆分 Layer（`terrain.json`、`npcs.json`、`lights.json`），加载更灵活，版本控制冲突更少。

5. **热重载是编辑器体验的核心**
   在开发模式下，为 Prefab 文件夹添加文件系统监控（如 `std::filesystem::last_write_time` 轮询）。当文件变化时自动触发重载。这会让你的迭代速度提升一个数量级。

---

> **本模块增量**：读完/实现完这篇笔记后，你的引擎新增了：
> - `PrefabRegistry` 与 JSON Prefab 定义
> - `SceneLoader`：从场景文件实例化实体并应用覆盖值
> - `PrefabInstance` 组件：建立运行时实体与源 Prefab 的链接
> - 场景保存与增量 Diff 能力
> - `DataLayer` 分层加载/卸载机制
>
> **下一步**：[[实体生命周期与注册表]]，因为 Prefab 实例化的本质是批量创建实体，需要完善的 EntityID 分配、生成回收和延迟销毁机制作为底层支撑。

> [[0_RoadMap|← 返回 自研引擎构建手册]]
