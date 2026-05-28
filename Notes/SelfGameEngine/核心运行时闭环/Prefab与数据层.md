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

## 问题 0：为什么 ECS 引擎不能直接用 C++ 代码硬编码世界？

### 场景绑定

你在编辑器里花一下午摆好了 50 个史莱姆、3 盏灯光、2 个摄像机触发器。关闭程序后，所有位置、旋转、覆盖数值全部消失——因为它们只存在于内存中的 C++ 对象，没有任何东西把它们写到磁盘。第二天打开引擎，你必须重新手动摆放一遍。

更常见的痛苦是迭代窒息：策划把史莱姆的 `max_health` 从 100 改成 80。由于数值硬编码在 `create_slime()` 函数里，他必须找你改代码、重新编译、重启游戏。一次数值验证的成本是 5 分钟编译 + 30 秒启动，一天验证 20 次就是近两小时的纯等待。

### 根因分析

问题的本质矛盾是**代码与数据的紧耦合**。当实体的结构（有哪些组件）和状态（组件字段值）都散落在 C++ 的 `new Entity()` 和 `comp.health = 100` 里时：
- **复用性为零**：同一种敌人在关卡中出现 50 次，创建逻辑要手写 50 次或套一个循环——但循环里的覆盖值（位置、血量）仍然散落在代码里。
- **可编辑性为零**：非程序员无法触及 C++ 源码，AI Agent 也无法通过修改文本文件来调整世界。
- **可持久性为零**：内存中的 ECS 世界是瞬态的，进程结束后 bit 级归零。

### 分支方案

#### 方案 A：纯代码创建 + 头文件配置宏
```cpp
#define SLIME_HEALTH 100
#define SLIME_RADIUS 0.5f

Entity create_slime(World& w, vec3 pos) {
    Entity e = w.create();
    w.add<Health>(e, SLIME_HEALTH);
    w.add<Transform>(e, pos);
    return e;
}
// 在 50 个地方调用 create_slime(w, {x,0,z})
```
- 改了宏要重编译。覆盖值（位置）仍在代码里。无法保存编辑器摆放结果。

#### 方案 B：脚本语言驱动（如 Lua）
```lua
-- slime.lua
function spawn_slime(world, pos)
    local e = world:create_entity()
    world:add_component(e, "Health", { current = 100, max = 100 })
    world:add_component(e, "Transform", { position = pos })
    return e
end
```
- 解耦了数据与编译，但脚本本身成了新的"硬编码"——脚本文件里的数值仍然无法被编辑器 diff 保存，且需要维护一套脚本绑定层。

#### 方案 C：数据驱动的 Prefab + 场景文件（默认推荐）
```json
// Prefabs/slime.json —— 这是模板
{
  "name": "Slime",
  "components": {
    "Health": { "current": 100, "max": 100 },
    "Transform": { "position": [0,0,0], "scale": [1,1,1] }
  }
}
```
```json
// Scenes/level_01.json —— 这是场景实例
{
  "entities": [
    { "prefab": "Slime", "override": { "Transform": { "position": [10,0,5] } } },
    { "prefab": "Slime", "override": { "Transform": { "position": [20,0,8] }, "Health": { "max": 150 } } }
  ]
}
```
- 模板与实例彻底分离。策划改 JSON 即可，无需编译。编辑器保存即写文件。AI Agent 可以直接读写。

### 决策分析

| 维度 | 方案 A 宏定义 | 方案 B 脚本 | 方案 C 数据驱动 |
|------|-------------|-----------|--------------|
| 是否需编译 | ✅ 每次改值都编译 | ❌ 无需编译 | ❌ 无需编译 |
| 编辑器友好 | ❌ 无法编辑 | ⚠️ 需要脚本解析 | ✅ 直接读写 JSON |
| AI Agent 友好 | ❌ | ⚠️ 需要理解脚本语法 | ✅ 纯文本自描述 |
| 版本控制 diff | ❌ 二进制可执行变 | ⚠️ 脚本逻辑混杂 | ✅ 一行数值一行 diff |
| 运行时开销 | 无 | 脚本 VM 开销 | 无（解析一次性） |

> **默认推荐**：方案 C。不要因为在开发早期就退回到方案 A 或 B。数据驱动是引擎从"玩具"走向"可生产工具"的底线门槛。

### 引擎对照

- **chaos**：`PrefabRSA`（Sketum 二进制资产）承载模板数据，但依赖 `EntityProxy` 重量级代理对象来维护实例与模板的链接。数据驱动是底层事实，但实现被包裹在复杂的 OOP 层次中。
- **UE**：`UBlueprint` 就是可视化 Prefab——一个 Blueprint 资产定义了默认组件和默认值，`AActor` 实例化时可以覆盖 `EditDefaultsOnly` 属性。场景持久化由 `UWorld` + `ULevel` 的 `Serialize()` 负责，`.umap` 文件本质上是二进制覆盖表。
- **Bevy**：`Scene` 是 ECS 世界的快照，可以被序列化为 `.scn.ron`（人类可读）或 `.scn`（二进制）。`DynamicScene` 允许在运行时从文件生成实体。Bevy 的哲学是"世界即数据"， Prefab 能力直接内建于场景系统中。

---

## 问题 1：如何用纯文本格式定义一个可复用的实体模板？

### 场景绑定

你正在给策划/AI Agent 设计第一个工作流：在磁盘上新建一个文件，描述"史莱姆长什么样"，然后引擎能读懂它并创建出对应的 ECS 实体。这个文件格式必须能被文本编辑器打开、能被 git diff、能被 Python 脚本生成——因为策划和 Agent 都不会写 C++。

### 根因分析

模板文件格式选择的本质矛盾是**人类可读性 vs 机器解析效率**。早期引擎如果选择二进制，调试时策划无法肉眼检查文件内容，AI Agent 也无法直接生成。如果选择纯文本但格式设计糟糕（比如 XML 过度嵌套），又会导致覆盖规则难以手写和维护。

### 分支方案

#### 方案 A：JSON
```json
{
  "name": "Slime",
  "components": {
    "Health": { "current": 100, "max": 100 },
    "Mesh": { "path": "Assets/mesh/slime.obj", "cast_shadow": true }
  },
  "children": [
    { "name": "SlimeEye_L", "components": { "Mesh": { "path": "eye.obj" } } }
  ]
}
```
- **优势**：解析库极多（nlohmann/json、simdjson），几乎所有语言原生支持，AI 训练数据最丰富。
- **劣势**：不支持注释（标准 JSON），字段重复时无 schema 校验容易打错字，大文件体积偏大。

#### 方案 B：TOML
```toml
name = "Slime"

[components.Health]
current = 100
max = 100

[components.Mesh]
path = "Assets/mesh/slime.obj"
cast_shadow = true

[[children]]
name = "SlimeEye_L"
[children.components.Mesh]
path = "eye.obj"
```
- **优势**：原生支持注释，人类手写时层级感更强，git merge 冲突更少。
- **劣势**：深层嵌套（如组件字段里的嵌套结构体）表达力弱于 JSON，C++ 解析库不如 JSON 成熟。

#### 方案 C：自定义二进制块（如 Sketum、MessagePack）
```cpp
// 不是给人看的，是给发布版加载的
struct PrefabBinaryHeader {
    uint32_t magic = 0x50524546; // 'PREF'
    uint32_t component_count;
    uint32_t string_table_offset;
};
```
- **优势**：加载速度比 JSON 快 5~10 倍，文件体积小，可以 mmap 直接映射。
- **劣势**：完全不可读，无法版本控制 diff，AI 和策划无法直接编辑。

### 决策分析

| 维度 | JSON | TOML | 自定义二进制 |
|------|------|------|-----------|
| 人类可读 | ✅ | ✅✅（带注释） | ❌ |
| 手写友好 | ⚠️ | ✅ | ❌ |
| 解析库成熟度 | ✅✅ | ⚠️ | 需自研 |
| 加载速度（1万实体） | ~200ms | ~250ms | ~20ms |
| 文件体积 | 大 | 大 | 小 |
| git diff 友好 | ✅ | ✅ | ❌ |
| AI 生成/编辑 | ✅✅ | ✅ | ❌ |

> **默认推荐**：编辑器和工作流使用 **JSON**（通用性最强，AI 生态最好）；发布管线中自动 Cook 为 **MessagePack 或自定义二进制**（加载速度是大型场景的关键路径）。不要在开发期为了"省几毫秒加载"而提前引入二进制格式，调试效率的代价远高于加载时间。

### 引擎对照

- **chaos**：使用自研的 `SketumBinaryLoader`（零开销模板特化）加载 `PrefabRSA`。开发期没有人类可读的中间格式，调试 Prefab 需要专用工具。
- **UE**：`.uasset` 是二进制，但编辑器提供完整的可视化编辑和文本导出（`.json` 格式的 `FDumpAssetRegistry`）。Cook 后的 `.upak` 是压缩二进制，加载路径与编辑路径完全分离。
- **Bevy**：默认使用 `.scn.ron`（类似 Rust 结构体的文本格式），发布时可通过 `AssetProcessor` 预编译为 `.scn` 二进制。`ron` 的选择说明 Bevy 优先保证人类可读和版本控制友好。

---

## 问题 2：场景中同一种 Prefab 出现多次，如何既共享默认值又允许个体差异？

### 场景绑定

关卡设计师在 `level_01.json` 里放了 50 个史莱姆。它们都来自同一个 `Slime.prefab`，但：
- 第 1 个在 `(10, 0, 5)`，血量是默认值 100。
- 第 2 个在 `(20, 0, 8)`，策划为了制造精英怪，把它的 `max_health` 改成了 150。
- 第 3 个在 `(30, 0, 2)`，除了位置不同，其他全是默认。

如果你只是简单地"把 Prefab 的组件数据 memcpy 到实体上"，那 50 个史莱姆会完全一模一样——没有个体差异的世界是死寂的。

### 根因分析

本质矛盾是**共享（Sharing）与变异（Mutation）的冲突**。模板的价值在于"一改全改"（修改 Prefab 文件，所有实例同步更新），但实例的价值在于"每个个体有自己的状态"。错误的解法要么破坏共享（每个实例完全独立，失去模板意义），要么破坏变异（所有实例完全相同）。

### 分支方案

#### 方案 A：实例化后直接修改组件（破坏模板链接）
```cpp
Entity e = instantiate("Slime");
auto& h = w.get<Health>(e);
h.max = 150; // 直接改组件
```
- 引擎永远不知道 `h.max = 150` 是一个"覆盖值"还是"运行时逻辑修改"。保存场景时，无法区分"哪些字段应该存到 override 表"，只能全量保存——Prefab 的共享价值丧失。

#### 方案 B：运行时持续合并（每帧从 Prefab 默认值 + Override 表重新计算）
```cpp
// 假设有个 System 每帧执行：
for (auto [e, inst] : query<PrefabInstance>()) {
    const Prefab& p = registry.get(inst.prefab_id);
    merge_components(e, p.defaults, overrides[e]); // 每帧合并！
}
```
- 覆盖值确实被独立记录了，但**每帧合并**意味着：即使角色原地不动，你也在做数百次字段级 memcmp/merge。这是纯粹的性能浪费——覆盖值在 99.9% 的时间里是不变的。

#### 方案 C：实例化时一次性合并，运行时独立存储 Override 表（默认推荐）
```cpp
struct PrefabInstance {
    AssetID prefab_id;
    uint32_t instance_index; // 用于子实体映射
};

struct PrefabOverride {
    Entity target_entity;
    std::string component_name;
    std::string field_path;  // "position/x"
    json value;
};

// ---------- 实例化时的一次性合并 ----------
Entity instantiate_prefab(const std::string& name, const json& override_json) {
    const Prefab* p = registry.find(name);
    Entity e = world.create_entity();
    world.add<PrefabInstance>(e, { p->id, next_instance_index++ });

    for (auto& [comp_name, comp_data] : p->components.items()) {
        json final = comp_data;
        if (override_json.contains(comp_name)) {
            for (auto& [key, val] : override_json[comp_name].items())
                final[key] = val;
        }
        world.add_component_from_json(e, comp_name, final);
    }
    return e;
}
```
- 合并只发生在**实例化瞬间**。一旦实体被创建，它的组件就是普通 ECS 组件，和其他手动创建的实体没有任何性能差异。`PrefabOverride` 表只在加载、保存、热重载时被访问，不参与每帧 tick。

**状态变化示例**：
```
Prefab 默认值：
  Health:    { current: 100, max: 100 }
  Transform: { position: [0,0,0], scale: [1,1,1] }

场景覆盖表：
  override[E5]: Transform.position = [10, 0, 5]
  override[E5]: Health.max        = 150

实例化后的 E5：
  E5.Health:    { current: 100, max: 150 }   ← 来自默认值 + 覆盖
  E5.Transform: { position: [10,0,5], scale: [1,1,1] }
```

### 决策分析

| 维度 | 方案 A 直接修改 | 方案 B 运行时持续合并 | 方案 C 实例化时合并 |
|------|--------------|-------------------|-----------------|
| 运行时开销 | 无 | 每帧 O(n×fields) | 仅实例化时有开销 |
| 保存场景能力 | ❌ 无法识别覆盖 | ✅ 覆盖表即状态 | ✅ 覆盖表即状态 |
| Prefab 更新同步 | ❌ 无链接 | ✅ 重新合并即可 | ✅ 重新实例化 + 重应用覆盖 |
| 代码复杂度 | 低 | 中（需合并系统） | 中（需 Override 表） |
| 编辑器体验 | ❌ 无法显示"继承/覆盖" | ✅ | ✅ |

> **默认推荐**：方案 C。覆盖发生在**实例化时**，而不是运行时持续合并。一旦实体被创建，它的组件值就是普通 ECS 组件。`PrefabOverride` 表只在加载/重载 Prefab 时被读取，不会在每帧 tick 中消耗性能。

### 引擎对照

- **chaos**：`EntityProxy` 同时持有 `m_instance_data`（运行时状态）和 `m_prefab_instance_data`（源资产指针），但这两份数据在 Proxy 内部混合管理，热重载时需要处理多重句柄的同步。ECS 中应该将这份元数据拆分为独立的 `PrefabInstance` 组件，实体本身由 World 管理。
- **UE**：`AActor` 的 `InstanceData` 与 `ClassDefaults` 分离，属性系统（`FProperty`）在序列化时自动计算 Delta。`UObject` 的 `Archetype` 链接允许编辑器标记"覆盖字段"为粗体。UE 的合并发生在反序列化（加载）时，运行时无持续合并开销。
- **Bevy**：`SceneSpawner` 在生成 `DynamicSceneBundle` 时一次性应用组件值。Bevy 没有显式的 Override 表概念，因为 ECS 的组件模型天然扁平——"覆盖"就是直接给实体写入不同的组件值。如果需要追踪"哪些值来自 Prefab 默认"，需要额外引入 `Override<T>` 组件或资源。

---

## 问题 3：Prefab 里包含子 Prefab 时，覆盖值该作用在哪个层级？

### 场景绑定

你做了一个"史莱姆之王" Prefab，它包含：
- 一个子实体引用 `Slime`（身体）
- 一个子实体引用 `Crown`（头顶的皇冠）

策划想在场景里放一只史莱姆之王，并且要求"这只的皇冠要比默认大 1.5 倍"。但如果覆盖规则设计不当，"scale: 1.5" 可能会错误地作用到史莱姆的身体上——因为身体也来自 `Slime` Prefab，而 `Crown` 是它的子实体。更糟的是，如果两个子 Prefab 都包含一个叫 `Eye` 的孙实体，覆盖值会冲突到无法预测的地步。

### 根因分析

嵌套 Prefab 的根因矛盾是**作用域隔离失败**。当 Prefab A 实例化 Prefab B 作为子实体时，B 内部的实体 ID 是在 A 的实例化过程中动态生成的。如果没有显式的作用域边界，两个不同的 A 实例里的 B 子实体会产生 ID 冲突，且覆盖值无法定向到"第 1 个 A 实例的第 2 个子实体"。

### 分支方案

#### 方案 A：扁平化合并（放弃嵌套，所有组件拍平到根实体）
```json
// 史莱姆之王的 Prefab 文件变成：
{
  "name": "KingSlime",
  "components": { "Health": { "max": 500 } },
  "children": [
    { "name": "Body",  "components": { "Mesh": { "path": "slime.obj" } } },
    { "name": "Crown", "components": { "Mesh": { "path": "crown.obj" }, "Transform": { "scale": [1.2,1.2,1.2] } } }
  ]
}
```
- 子实体不再引用独立 Prefab，而是内联定义。失去了"一改全改"——如果 `slime.obj` 的路径变了，你要在所有包含史莱姆的 Prefab 里逐个修改。

#### 方案 B：运行时递归解析，无稳定 ID（按名称匹配）
```cpp
// 覆盖规则按实体名匹配：
override_json["Crown"]["Transform"]["scale"] = [1.5, 1.5, 1.5];
```
- 如果两个子实体同名，覆盖会同时作用到两者。如果 Prefab 版本更新后子实体改名了，所有旧场景的覆盖值全部失效。名称是设计时标签，不是稳定标识符。

#### 方案 C：scope_id 隔离 + 实例索引树（默认推荐）
```cpp
struct PrefabInstance {
    AssetID prefab_id;
    uint32_t instance_index; // 本 Prefab 在场景中的第几个实例
};

// 实例化时分配全局唯一的 scope_id
uint32_t next_scope_id = 1;

Entity instantiate_prefab_recursive(const Prefab& p, uint32_t scope_id, 
                                    uint32_t local_index, const json& overrides) {
    Entity e = world.create_entity();
    world.add<PrefabInstance>(e, { p.id, local_index });

    // 应用本实体的覆盖：overrides 按 (scope_id, local_index) 寻址
    apply_overrides(e, overrides, scope_id, local_index);

    // 递归实例化子 Prefab，子实体的 local_index 在父作用域内自增
    uint32_t child_local_index = 0;
    for (const auto& child_prefab_name : p.children) {
        const Prefab& child_p = registry.find(child_prefab_name);
        // 子实体继承父 scope_id，但拥有独立的 local_index 链
        instantiate_prefab_recursive(child_p, scope_id, child_local_index++, 
                                     overrides);
    }
    return e;
}
```
- 每个 Prefab 实例获得一个全局唯一的 `scope_id`。内部所有子实体的寻址方式是 `(scope_id, local_index)`，不同实例之间完全隔离。即使子 Prefab 结构变化，只要 `local_index` 不变，覆盖值就能稳定命中。

**覆盖表在嵌套场景下的结构**：
```json
{
  "overrides": {
    "scope_42": {
      "0": { "Health": { "max": 500 } },           // 根实体（KingSlime）
      "1": { "Transform": { "scale": [1.5,1.5,1.5] } }  // 第1个子实体（Crown）
    }
  }
}
```

### 决策分析

| 维度 | 方案 A 扁平化 | 方案 B 名称匹配 | 方案 C scope_id 隔离 |
|------|------------|--------------|-------------------|
| 嵌套复用性 | ❌ 无 | ✅ | ✅ |
| 覆盖稳定性 | ✅（无嵌套即无冲突） | ❌ 改名即失效 | ✅ 按索引，稳定 |
| 结构变更容错 | ❌ 需手动同步 | ❌ 需手动同步 | ⚠️ 插入/删除子实体会偏移索引 |
| 实现复杂度 | 低 | 低 | 中（需维护 scope 映射） |
| 编辑器实现 | 简单 | 简单 | 中（需显示层级树） |

> **默认推荐**：方案 C。chaos 引擎的 `ScopeInstanceCollector` 本质上就是 scope_id 隔离 + 两阶段收集。UE 的 `USceneComponent` _attach 层级也有稳定的 `ComponentTemplate` 索引。不要在嵌套场景下退回到方案 A，失去嵌套复用能力的 Prefab 系统会在项目规模增长后迅速崩塌。

### 引擎对照

- **chaos**：`ScopeInstanceCollector` 采用**两阶段**：先收集所有子实体的 `EntityProxy` 创建请求，再批量执行。`scope_id` 隐含在 `DataRef` 的 XOR 编码中。迁移到 ECS 时，应将 `scope_id` 显式化，并用 `CommandBuffer` 批量创建子实体。
- **UE**：`UBlueprint` 的组件层级在 `SimpleConstructionScript` 中维护，每个 `USCS_Node` 有稳定的 `VariableGuid`。实例化时，`AActor` 的 `UserConstructionScript` 按节点顺序构建组件树。覆盖值通过 `UObject::InstanceData` 与 `Archetype` 的 Delta 计算来定位到具体节点。
- **Bevy**：`Scene` 中的实体通过 `Parent` / `Children` 组件建立层级。Bevy 的 `SceneSpawner` 在生成时为每个源实体分配新的 `EntityID`，但**不保留**"我是 Prefab X 的第 Y 个实例"的元数据。如果需要嵌套覆盖，必须在组件中手动添加 `PrefabPath` 或 `PrefabInstance` 组件来重建链接。

---

## 问题 4：编辑器摆好的场景如何保存到磁盘，只记录差异而不重复存储默认值？

### 场景绑定

策划在编辑器里调整了 50 个史莱姆的位置，给其中 3 个加了精英血量覆盖，还手动创建了一盏没有 Prefab 的"主光源"。点击保存时，引擎不能把 50 个史莱姆的完整组件数据（Health、Mesh、Collider……每个 100+ 字段）全量写进 JSON——那会让场景文件膨胀到数 MB，且 git diff 时全是噪声。理想情况下，场景文件只记录：
- "这 50 个实体来自 Slime Prefab"
- "第 2 个的 Health.max 覆盖为 150"
- "第 51 个是裸实体，名叫 MainLight，有这些组件……"

### 根因分析

根因是**信息冗余与版本控制噪声**。Prefab 的存在意味着"默认值已经在磁盘上有一份了"，场景文件应该是指针 + 增量的组合。全量保存不仅浪费空间，还破坏了"修改 Prefab 默认值 → 所有实例自动更新"的语义——因为场景文件里存的是旧默认值，加载时会覆盖新的 Prefab 默认值。

### 分支方案

#### 方案 A：全量保存每个实体
```json
{
  "entities": [
    { "components": { "Health": { "current": 100, "max": 100 }, "Transform": { "position": [10,0,5] } } },
    { "components": { "Health": { "current": 100, "max": 100 }, "Transform": { "position": [20,0,8] } } }
  ]
}
```
- 简单直接，但 `max: 100` 被重复存储了 50 次。修改 Prefab 默认值后，场景文件里的旧值会错误地覆盖新默认值。

#### 方案 B：仅保存 Prefab 名，不存任何覆盖
```json
{ "entities": [ { "prefab": "Slime" }, { "prefab": "Slime" } ] }
```
- 文件极小，但所有实例完全一模一样——丢失了策划在编辑器里做的任何微调。

#### 方案 C：增量 Diff + 裸实体全量（默认推荐）
```cpp
struct SceneSaver {
    void save(const std::string& path) {
        json out;
        out["entities"] = json::array();

        for (Entity e : world.all_entities()) {
            json entry;
            if (world.has<PrefabInstance>(e)) {
                const auto& inst = world.get<PrefabInstance>(e);
                const Prefab& p = registry.get(inst.prefab_id);
                entry["prefab"] = p.name;
                entry["override"] = compute_diff(e, p); // 只存差异！
            } else {
                entry["name"] = world.get_or_default<Name>(e).value;
                entry["components"] = serialize_all_components(e);
            }
            out["entities"].push_back(entry);
        }
        write_file(path, out.dump(2));
    }

    json compute_diff(Entity e, const Prefab& p) {
        json diff;
        for (auto& [comp_name, type_id] : world.component_types(e)) {
            void* instance_ptr = world.get_component_raw(e, type_id);
            void* prefab_ptr   = p.get_component_default(comp_name); // 通过反射获取
            if (!prefab_ptr) continue;

            const TypeDesc& desc = reflect(type_id);
            for (const auto& field : desc.fields) {
                void* inst_field = (char*)instance_ptr + field.offset;
                void* pref_field = (char*)prefab_ptr + field.offset;
                if (std::memcmp(inst_field, pref_field, field.size) != 0) {
                    diff[comp_name][field.name] = serialize(inst_field, field.type);
                }
            }
        }
        return diff;
    }
};
```
- 带有 `PrefabInstance` 的实体只输出差异字段。裸实体（如灯光、触发器）全量输出组件。场景文件保持最小化，git diff 一行数值变化清晰可见。

### 决策分析

| 维度 | 方案 A 全量 | 方案 B 仅存Prefab名 | 方案 C 增量 Diff |
|------|----------|------------------|---------------|
| 文件大小 | 极大（MB级） | 极小 | 小（仅差异） |
| Prefab更新兼容性 | ❌ 旧值覆盖新默认值 | ✅ | ✅ |
| 保留实例独特性 | ✅ | ❌ | ✅ |
| git diff 可读性 | ❌ | ✅ | ✅ |
| 实现复杂度 | 低 | 极低 | 中（需反射+逐字段memcmp） |
| 向前兼容 | ✅ | ✅ | ✅（缺失字段用Prefab默认） |

> **默认推荐**：方案 C。增量 Diff 是编辑器场景持久化的工业标准。UE 的 `ULevel::Serialize`、Unity 的 `Scene` YAML、Bevy 的 `DynamicScene` 都遵循"指针 + 增量"的模型。`memcmp` 逐字段比较需要 [[反射系统]] 提供字段偏移和大小信息。

### 引擎对照

- **chaos**：`EntityProxy` 的保存逻辑被埋在 `SketumBinary` 的序列化管线中，Delta 计算与 Proxy 的内部状态强耦合。迁移到 ECS 后，`compute_diff` 可以独立为一个纯函数：输入是 `Entity` + `Prefab`，输出是 `json`，不依赖任何代理对象。
- **UE**：`UObject::Serialize` 会自动对比 `Archetype`（类默认值）与当前实例值，只序列化差异。`ULevel` 的 `.umap` 文件本质上就是一个巨大的 Delta 表。UE 的 `FArchive` 支持 `IsSaving()` 和 `IsLoading()` 双路径，编辑器保存走文本-ish 的 `FStructuredArchive`，发布 Cook 走二进制 `FMemoryArchive`。
- **Bevy**：`DynamicScene` 在提取实体时会检查组件是否来自 `TypeRegistration`，保存为 `.scn.ron` 时默认写入完整组件值（因为 Bevy 没有显式的 Prefab 默认值系统）。如果需要增量保存，需要自定义 `SceneFilter` 或在保存前与基准 `Scene` 做 Diff。

---

## 问题 5：大型开放世界不可能一次性加载全部实体，如何分块管理场景数据？

### 场景绑定

你的引擎最初只有一个 `level_01.json`，里面放 200 个实体，加载时间 500ms——没问题。但当你开始做一个开放世界关卡，实体数量达到 10 万（地形块、植被、NPC、道具、触发器），`level_01.json` 变成了 50MB。玩家出生在村庄，但引擎启动时就把整个大陆的 10 万个实体全部加载进内存，创建过程卡住 8 秒钟。更荒谬的是，当玩家传送到地下城时，地面上的 3 万棵草和 500 只兔子仍然占用内存和 CPU。

### 根因分析

根因是**内存容量与加载时间的物理限制**。无论 ECS 的遍历多快，创建 10 万个实体、分配 10 万份组件内存、解析 50MB JSON 都需要时间和空间。开放世界的核心假设是"玩家同时能感知到的世界是有限的"，所以数据应该按**空间邻近性**或**功能职责**分块，按需加载和卸载。

### 分支方案

#### 方案 A：单一场景文件，启动时全量加载
```cpp
// 只有一个 level_openworld.json
SceneLoader loader;
loader.load_scene("level_openworld.json"); // 卡住 8 秒
```
- 实现最简单，但扩展性为零。10 万实体是内存杀手，加载时间是帧率杀手。

#### 方案 B：按坐标网格触发，无显式 Layer 概念
```cpp
// 每帧检查玩家位置，超过边界就加载新区块
void tick_streaming(vec3 player_pos) {
    for (auto& chunk : world_chunks) {
        bool should_load = distance(player_pos, chunk.center) < load_radius;
        if (should_load && !chunk.loaded) load_chunk(chunk);
        if (!should_load && chunk.loaded) unload_chunk(chunk);
    }
}
```
- 自动按距离管理，但**耦合了物理坐标与数据管理**。UI Layer、任务 Layer、全局光照 Layer 并不遵循空间坐标规则——按坐标卸载可能会误删一个全局任务触发器。

#### 方案 C：DataLayer 分区 + 显式加载/卸载 + 跨 Layer 引用延迟解析（默认推荐）
```cpp
struct DataLayer {
    std::string name;           // "Terrain", "NPCs", "QuestTriggers"
    std::string source_file;    // "Layers/terrain.json"
    std::vector<Entity> entities;
    bool is_loaded = false;
    bool is_persistent = false; // 是否随玩家位置卸载
};

struct DataLayerManager {
    std::unordered_map<std::string, DataLayer> layers;

    void load_layer(const std::string& name) {
        DataLayer& layer = layers[name];
        if (layer.is_loaded) return;
        json j = parse_file(layer.source_file);
        for (auto& entry : j["entities"]) {
            Entity e = instantiate_entry(entry);
            layer.entities.push_back(e);
        }
        layer.is_loaded = true;
    }

    void unload_layer(const std::string& name) {
        DataLayer& layer = layers[name];
        if (!layer.is_loaded || layer.is_persistent) return;
        for (Entity e : layer.entities) {
            world.destroy_entity(e); // 或提交到 CommandBuffer 延迟销毁
        }
        layer.entities.clear();
        layer.is_loaded = false;
    }
};

// 跨 Layer 引用：任务触发器引用某个 Layer 中的 NPC
struct DataReference {
    std::string target_layer;
    std::string target_entity_name; // 或 (scope_id, local_index)
    bool resolved = false;
    Entity target = INVALID_ENTITY;
};

// DataReferenceManager 在每帧的安全点尝试解析未决引用
void DataReferenceManager::resolve_pending() {
    for (auto& ref : pending_refs) {
        if (!layer_manager.is_loaded(ref.target_layer)) continue;
        ref.target = find_entity_by_name(ref.target_layer, ref.target_entity_name);
        ref.resolved = (ref.target != INVALID_ENTITY);
    }
}
```
- `DataLayer` 将世界划分为独立的逻辑分区，每个 Layer 有自己的场景文件。`persistent` 标记允许全局 Layer（如任务系统）不受玩家位置影响。`DataReferenceManager` 处理跨 Layer 的实体引用——当目标 Layer 尚未加载时，引用保持为延迟解析状态，加载完成后自动绑定。

### 决策分析

| 维度 | 方案 A 单文件 | 方案 B 坐标触发 | 方案 C DataLayer |
|------|-----------|-------------|----------------|
| 实现复杂度 | 极低 | 低 | 中（需 LayerManager + 引用解析） |
| 内存可控性 | ❌ 全量常驻 | ✅ 按距离 | ✅ 显式控制 + 持久标记 |
| 非空间数据支持 | ✅ | ❌ 坐标系统无法表达 | ✅ 任意命名 Layer |
| 跨 Layer 引用 | N/A | ❌ 无 | ✅ 延迟解析 |
| 版本控制冲突 | ❌ 一人改全局冲突 | ⚠️ 按块减少 | ✅ 按功能/区域独立文件 |
| 编辑器支持 | 简单 | 中 | 中（需 Layer 面板） |

> **默认推荐**：方案 C。chaos 的 `DataLayer` + `DataLayerManager` 就是这一思路，但 chaos 的 Layer 持有 `EntityProxy` 指针，卸载时需要逐个调用析构。ECS 中 Layer 只管理 `EntityID` 数组，批量提交销毁命令即可。UE 的 `ULevelStreaming` 和 Bevy 的 `Scene` 动态加载也是同一原理的分层管理。

### 引擎对照

- **chaos**：`DataLayer` 是核心概念，每个 Layer 有自己的 `SketumBinary` 源文件，`DataLayerManager` 根据游戏逻辑（而非纯坐标）决定加载/卸载。`DataReferenceManager` 用 XOR `DataRef` 做跨 Layer 引用，目标未加载时引用悬空。迁移到 ECS 时，`DataRef` 可替换为 `(layer_name, entity_name)` 二元组，由 `DataReferenceManager` 在加载完成后解析为 `EntityID`。
- **UE**：`UWorld` 包含多个 `ULevel`，`ULevelStreaming` 负责按触发体积（`ALevelStreamingVolume`）或蓝图逻辑动态加载/卸载 SubLevel。`FStreamableManager` 提供异步资产加载，`TSoftObjectPtr` 和 `TSoftClassPtr` 是跨 Level 的软引用——目标未加载时不会硬崩溃，只是返回 nullptr。
- **Bevy**：Bevy 没有内置的 Layer 系统，但可以通过 `Scene` 的按需加载 + `Resource<LoadedScenes>` 来模拟。Bevy 的 `Handle<Scene>` 是软引用，`AssetServer::load()` 是异步的，场景实体通过 `SceneSpawner` 在后台生成。跨场景引用通常通过 `Entity` 的 `Name` 组件或自定义资源来间接实现。

---

## 问题 6：美术修改 Prefab 源文件后，已放置的实例能否不重启就更新？

### 场景绑定

美术把史莱姆的模型从 `slime_v1.obj` 改成了 `slime_v2.obj`，并且调整了 `Collider` 的半径。如果引擎必须重启才能看到效果，美术每调一次参数就要等 30 秒启动——一天调 100 次就是 50 分钟的死亡等待。理想的体验是：美术保存 Prefab 文件后，编辑器里的 50 个史莱姆实例在 1 秒内全部更新模型和碰撞体，且策划之前覆盖的位置、血量值完全保留。

### 根因分析

根因是**运行时状态与源资产的同步问题**。实例化时的一次性合并（问题 2 的方案 C）意味着运行时实体已经"脱离"了 Prefab——它的组件值是普通 ECS 组件。当源 Prefab 变化时，引擎必须知道：
1. 哪些实体受这次变化影响？
2. 运行时覆盖值不能丢失。
3. 新组件（如美术新增的 `Loot` 组件）要自动添加。
4. 旧组件（如删除的 `Decal` 组件）要自动移除。

### 分支方案

#### 方案 A：重启引擎（最原始）
- 可靠，但迭代速度 unacceptable。在工业流程中，这会导致美术和策划拒绝使用编辑器。

#### 方案 B：定时全量重建世界
```cpp
// 检测到 Prefab 变化后：
void reload_all() {
    save_scene("temp.json");   // 先保存当前状态
    clear_all_entities();      // 销毁所有实体
    load_scene("temp.json");   // 重新加载
}
```
- 会丢失运行时临时状态（如当前的动画播放进度、物理速度、AI 状态机）。且全局保存+加载的代价是 O(n)，n 越大越慢。

#### 方案 C：基于 PrefabInstance 的增量热重载（默认推荐）
```cpp
struct PrefabHotReloader {
    PrefabRegistry& registry;
    World& world;

    void reload_prefab(AssetID prefab_id) {
        // 1. 重新加载 Prefab 默认值
        Prefab& new_prefab = registry.reload_from_disk(prefab_id);

        // 2. 查询所有指向该 Prefab 的实体
        for (auto [e, inst] : world.query<PrefabInstance>()) {
            if (inst.prefab_id != prefab_id) continue;

            // 3. 收集该实体的覆盖值（从 OverrideTable 或运行时组件反向计算）
            json overrides = override_table.get_overrides(e);

            // 4. 按新 Prefab 的组件列表重建实体
            //    - 新 Prefab 有的组件：添加或更新默认值
            //    - 旧 Prefab 有但新 Prefab 没有的：移除
            //    - 覆盖字段：保留用户修改
            rebuild_entity_from_prefab(e, new_prefab, overrides);
        }

        // 5. 通过事件总线通知其他 System 刷新缓存
        event_bus.publish(PrefabReloadedEvent{ prefab_id });
    }

    void rebuild_entity_from_prefab(Entity e, const Prefab& p, const json& overrides) {
        // 重建组件：先移除旧 Prefab 独有的组件
        for (auto type_id : old_component_set[e]) {
            if (!p.has_component(type_id)) world.remove_component(e, type_id);
        }
        // 再应用新默认值 + 覆盖
        for (auto& [comp_name, comp_data] : p.components.items()) {
            json final = comp_data;
            if (overrides.contains(comp_name)) {
                for (auto& [k, v] : overrides[comp_name].items()) final[k] = v;
            }
            world.add_or_replace_component_from_json(e, comp_name, final);
        }
    }
};
```
- 只重建受影响的实体，不影响其他 Prefab 的实例或裸实体。覆盖值通过 `OverrideTable` 或运行时 diff 计算保留。`PrefabReloadedEvent` 让渲染系统知道"这个实体的 Mesh 变了，重新上传 GPU 数据"，让物理系统知道"Collider 半径变了，重新构建碰撞体"。

### 决策分析

| 维度 | 方案 A 重启 | 方案 B 全量重建 | 方案 C 增量热重载 |
|------|----------|--------------|----------------|
| 迭代速度 | ❌ 30秒+ | ⚠️ 数秒~数十秒 | ✅ <1秒 |
| 运行时状态保留 | N/A | ❌ 丢失 | ✅ 覆盖保留，临时状态可选保留 |
| 实现复杂度 | 无 | 低 | 中（需事件通知+缓存刷新） |
| 可靠性 | ✅ 最高 | ⚠️ 保存/加载可能出错 | ⚠️ 组件增删需边界处理 |
| 可扩展性 | N/A | ❌ O(n) | ✅ O(受影响实体数) |

> **默认推荐**：方案 C。热重载是编辑器体验的核心。chaos 和 UE 的编辑器都实现了这一能力。不要在开发期为了"简单"而接受重启——迭代速度的代价会在项目生命周期中被无限放大。

### 引擎对照

- **chaos**：热更新需要处理 `EntityProxy` 中的多重句柄和旧系统兼容字段，因为 Proxy 是一个重达数百字节的 OOP 对象。迁移到 ECS 后，热重载只需要遍历 `Query<PrefabInstance>`，重新创建组件并应用覆盖值即可——`PrefabInstance` 组件只有 16 字节左右，其余都是普通 ECS 组件。
- **UE**：编辑器模式下，`UBlueprint` 的 `Compile()` 会触发所有实例的 `RerunConstructionScripts()`，重新执行 `UserConstructionScript` 并应用 `InstanceData` 覆盖。`FPropertyChangedEvent` 允许属性面板单个字段修改时只刷新该字段，无需重建整个 Actor。
- **Bevy**：Bevy 没有内置的 Prefab 热重载系统。`AssetServer` 可以监控 `Handle<Scene>` 的源文件变化并重新加载，但已生成的实体不会自动更新——因为 `Scene` 生成后，`Entity` 与 `Scene` 资产之间没有持久链接。要实现热重载，需要自定义 `PrefabInstance` 组件和 `PrefabReloader` 系统。

---

## 问题 7：Prefab 引用的外部资产（模型、贴图）尚未加载时，实例化会阻塞主线程吗？

### 场景绑定

史莱姆 Prefab 里有一个 `MeshComponent`，引用 `Assets/mesh/slime.obj`。这个模型文件有 2MB，从磁盘读取并上传到 GPU 需要 50ms。如果在 `instantiate_prefab()` 里同步加载这 50ms，主线程会被阻塞，帧率从 60fps 骤降到 20fps——玩家会感受到明显的卡顿。更复杂的情况是：一个开放世界的 Layer 里有 1000 棵树，每棵树的模型、贴图、碰撞体都需要加载，同步阻塞的总时间可达数秒。

### 根因分析

根因是**磁盘 I/O 与 GPU 上传的阻塞性**。CPU 内存操作是纳秒级的，但磁盘读取是毫秒级，GPU 资源创建（纹理上传、缓冲区创建）可能需要 GPU 空闲时才能执行。主线程（游戏逻辑线程）不能被这些操作阻塞，否则帧率保证失效。

### 分支方案

#### 方案 A：同步阻塞加载
```cpp
Entity instantiate_with_block(const Prefab& p) {
    Entity e = world.create_entity();
    for (auto& [name, data] : p.components.items()) {
        if (name == "Mesh") {
            MeshAsset mesh = load_mesh_from_disk(data["path"]); // 阻塞 50ms！
            world.add<MeshComponent>(e, mesh);
        }
    }
    return e;
}
```
- 最简单，但卡帧 unacceptable。只适用于编辑器初始化或极小规模的工具场景。

#### 方案 B：完全不加载，保留空句柄
```cpp
world.add<MeshComponent>(e, MeshHandle::null());
// 渲染系统看到 null handle 就跳过
```
- 不卡帧，但实体在资产加载完成前完全不可见（或不可交互）。如果逻辑系统没有做空检查，可能会空指针崩溃。且加载完成后没有机制通知 ECS 更新句柄。

#### 方案 C：占位句柄 + 异步资产管线 + 完成回调（默认推荐）
```cpp
struct MeshComponent {
    AssetHandle<Mesh> handle; // 可以是已完成句柄、占位句柄或失败句柄
};

struct AssetManager {
    // 返回一个立即有效的占位句柄，但底层资源尚未加载
    template<typename T>
    AssetHandle<T> request_async(const std::string& path) {
        AssetID id = hash_path(path);
        if (is_loaded(id)) return handles[id];
        
        // 提交异步加载任务到线程池
        pending_jobs.emplace_back(id, path, [](AssetID id, void* data) {
            // 后台线程：读磁盘、解析、上传 GPU
            T* asset = load_and_upload<T>(data);
            return asset;
        });
        
        // 返回占位句柄，状态为 "Loading"
        return create_placeholder(id);
    }
};

// 实例化时不阻塞
Entity instantiate_async(const Prefab& p) {
    Entity e = world.create_entity();
    if (p.components.contains("Mesh")) {
        std::string path = p.components["Mesh"]["path"];
        auto handle = asset_manager.request_async<Mesh>(path);
        world.add<MeshComponent>(e, handle);
    }
    return e;
}

// 每帧或每隔几帧，主线程检查已完成的异步任务
void AssetManager::update() {
    for (auto& job : check_completed_jobs()) {
        // 将占位句柄替换为真实资源
        resolve_handle(job.id, job.result);
        // 通知 ECS：这个资产已就绪
        event_bus.publish(AssetLoadedEvent{ job.id });
    }
}

// 渲染系统可以选择：看到 Loading 句柄时渲染一个默认立方体
void RenderSystem::tick() {
    for (auto [e, mesh] : query<MeshComponent>()) {
        if (mesh.handle.is_loading()) draw_debug_cube(e);
        else if (mesh.handle.is_ready()) draw_mesh(e, mesh.handle.get());
    }
}
```
- 实例化瞬间完成，资产在后台线程加载。占位句柄保证 ECS 世界的完整性——实体、组件都存在，只是资源状态是 `Loading`。加载完成后通过事件总线通知相关系统更新。渲染系统可以优雅降级（显示占位立方体或透明）。

### 决策分析

| 维度 | 方案 A 同步阻塞 | 方案 B 空句柄 | 方案 C 异步占位 |
|------|------------|-----------|-------------|
| 主线程阻塞 | ❌ 严重 | ✅ 无 | ✅ 无 |
| 实例化即时性 | ✅ | ✅ | ✅ |
| 运行时安全性 | ✅ | ❌ 可能崩溃 | ✅ 占位保证非空 |
| 加载完成通知 | N/A | ❌ 无 | ✅ 事件总线 |
| 优雅降级 | N/A | ❌ 不可见 | ✅ 默认立方体/透明 |
| 实现复杂度 | 低 | 极低 | 中（需线程池+状态机） |

> **默认推荐**：方案 C。UE 的 `FStreamableManager` + `TSoftObjectPtr`、Bevy 的 `AssetServer` + `Handle<T>`、chaos 的资产管线都遵循同一模式：请求时返回句柄，后台加载，完成后通知。不要在主线程做任何磁盘 I/O——这是帧率稳定的铁律。

### 引擎对照

- **chaos**：资产加载通过 `SketumBinaryLoader` 进行，大型资产（纹理、模型）在后台线程解析，但句柄系统和 ECS 的集成需要额外封装。迁移时，将 `SketumHandle` 映射为 `AssetHandle<T>` 模板，利用 ECS 的组件模型自然表达"资源组件"状态。
- **UE**：`FStreamableManager::RequestAsyncLoad()` 返回 `TSharedPtr<FStreamableHandle>`，`TSoftObjectPtr` 在资产未加载时返回 nullptr，加载完成后自动解析。`UAssetManager` 提供 Primary Asset Label 系统，可以按标签批量异步预加载（如"进入村庄前预加载村庄资产"）。
- **Bevy**：`AssetServer::load()` 是异步的，立即返回 `Handle<T>`。`Handle` 内部有加载状态（`Loading` / `Loaded` / `Failed`）。`RenderAssetPlugin` 会在资产加载完成后自动提取 GPU 资源。Bevy 的 `StandardMaterial` 和 `Mesh` 都使用 `Handle<T>`，渲染系统在 `ExtractSchedule` 阶段检查句柄状态并决定是提取还是跳过。

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

## 默认推荐路径总结

### 默认推荐路径

| 决策点 | 默认推荐 | 关键理由 |
|--------|---------|---------|
| 模板格式 | **JSON**（开发期）+ **MessagePack**（发布期） | AI 友好、git 友好、解析库成熟 |
| 覆盖机制 | **实例化时一次性合并** + **独立 OverrideTable** | 运行时零开销，保存时可 diff |
| 嵌套作用域 | **scope_id + local_index** 树 | 稳定、可隔离、可扩展 |
| 场景持久化 | **增量 Diff**（Prefab 实体）+ **全量**（裸实体） | 最小化文件，最大化向前兼容 |
| 大世界管理 | **DataLayer** 分区 + **显式加载/卸载** | 非空间数据也可管理，跨 Layer 引用有延迟解析 |
| 热重载 | **基于 PrefabInstance 的增量重建** + **事件通知** | 迭代速度是编辑器生命线 |
| 资产加载 | **异步占位句柄** + **后台线程池** | 主线程绝不阻塞，渲染可优雅降级 |

### UE 工业级设计吸收

- **Archetype / InstanceData 分离**：UE 的 `UObject` 默认属性与实例属性分离机制，对应本模块的 `Prefab` 默认值 + `OverrideTable` 设计。
- **ULevelStreaming 分层思想**：UE 的 SubLevel 动态加载直接映射到 `DataLayer` + `DataLayerManager`。
- **FStreamableManager 异步管线**：UE 的软引用 + 异步加载 + 完成回调模式，映射到 `AssetHandle<T>` + `AssetManager::update()`。
- **Property 级 Delta 序列化**：UE 编辑器保存时只存差异字段，映射到 `SceneSaver::compute_diff()` 的逐字段 `memcmp` 逻辑。

### ECS 映射

| 传统 OOP 概念 | ECS 映射 | 说明 |
|-------------|---------|------|
| Prefab 模板 | **Resource<PrefabRegistry>** | 全局只读资源，不绑定到任何实体 |
| Prefab 实例链接 | **PrefabInstance 组件** | 轻量组件（AssetID + index），附加到每个实例实体 |
| 覆盖值 | **OverrideTable 资源** 或 **Override<T> 组件** | 集中存储便于热重载，分散组件存储便于遍历 |
| 场景层级 | **Parent / Children 组件** | Bevy 风格，ECS 原生表达层级 |
| DataLayer | **LayerManager 资源** + **LayerTag 组件** | 实体通过 Tag 标记所属 Layer，Manager 负责批量加载/卸载 |
| 异步资产 | **AssetHandle<T> 组件字段** | MeshComponent 内嵌 Handle，渲染系统查询状态 |
| 热重载触发 | **PrefabReloader 系统** | 读取文件监控事件，在 `Update` 阶段执行重建 |

### 条件切换路径

| 条件 | 从默认路径切换为 |
|------|---------------|
| 单个场景实体数 < 500，且无 Prefab 嵌套 | 可以暂时退化为"全量保存每个实体"，简化反射依赖 |
| 目标平台为内存极度受限的嵌入式设备 | 发布格式从 MessagePack 切换为自定义 zero-copy 块格式 |
| 需要策划在 Excel 中批量编辑数值 | 在 JSON 上层增加 CSV/Excel 导出导入管线（而非替换 JSON） |
| 多人在线编辑同一场景 | 引入 CRDT 或操作序列化层，OverrideTable 变为共享状态 |
| 需要运行时程序化生成世界 | DataLayer 的 `source_file` 从静态 JSON 切换为程序化生成器函数 |

### 扩展路径

| 里程碑 | 要补全的工业级能力 |
|--------|------------------|
| **M1：资产引用校验** | Prefab JSON 中的 `mesh_path` 在加载时校验是否存在，缺失时报告错误而非崩溃 |
| **M2：Prefab 变体（Prefab Variant）** | 允许 `Slime_Elite.prefab` 继承自 `Slime.prefab` 并追加覆盖，形成继承链而非复制粘贴 |
| **M3：Diff/Merge 工具** | 为场景文件和 Prefab 文件提供可视化的 diff 工具，策划能看懂合并冲突 |
| **M4：Cook 管线** | 发布时自动将 JSON → MessagePack，将纹理 → 压缩 GPU 格式，将 Shader → 平台字节码 |
| **M5：网络同步层** | `OverrideTable` 支持服务器 authoritative 修改，客户端接收增量同步而非全量场景 |
| **M6：版本化资产** | Prefab 和场景文件支持版本号，旧场景加载新 Prefab 时提供自动迁移脚本或兼容性警告 |

---

> **本模块增量**：读完/实现完这篇笔记后，你的引擎新增了：
> - `PrefabRegistry` 与 JSON Prefab 定义
> - `SceneLoader`：从场景文件实例化实体并应用覆盖值
> - `PrefabInstance` 组件：建立运行时实体与源 Prefab 的链接
> - 场景保存与增量 Diff 能力
> - `DataLayer` 分层加载/卸载机制
>
> **下一步**：[[实体生命周期与注册表]]，因为 Prefab 实例化的本质是批量创建实体，需要完善的 EntityID 分配、生成回收和延迟销毁机制作为底层支撑。

> [[Notes/SelfGameEngine/0_RoadMap|← 返回 自研引擎构建手册]]
