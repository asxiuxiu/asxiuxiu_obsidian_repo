---
title: Prefab与数据层
date: 2026-04-15
tags:
  - self-game-engine
  - serialization
  - prefab
  - data-layer
  - ECS
aliases:
  - 数据持久化层
  - Prefab系统
---

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])

# Prefab 与数据层

> **前置依赖**：[[反射系统]]、[[文件IO与虚拟文件系统]]（概念）
> **本模块增量**：让世界状态可以被保存、加载和跨网络传输；建立 Prefab 模板机制，让场景可以被复用和实例化。
> **下一步**：[[编辑器框架]] — 持久化能力就位后，编辑器才能保存和加载场景。

---

## Why：为什么引擎需要 Prefab 与数据持久化？

### 没有这个系统时，世界有多糟

1. **场景无法保存**
   - 你在编辑器里花两小时摆好了 300 个道具，关闭引擎后一切归零。没有序列化，就没有"工作成果"。

2. **重复劳动**
   - 每个敌人都要手动创建、逐个添加组件、逐个调整参数。一个关卡有 100 个哥布林，你就要重复 100 次同样的操作。

3. **网络同步手动写死**
   - 新增一个组件字段，`Transform` 从 3 个 float 变成 4 个 float，两端代码同时崩溃。没有基于反射的自动序列化，网络协议就是脆弱的纸房子。

### 三个真实场景

| 场景 | 没有持久化的困难 | Prefab+序列化的答案 |
|------|---------------------|------------------|
| **场景 1：保存/加载游戏世界** | 只能把整片内存 fwrite 到磁盘，里面夹杂指针和虚表 | 基于反射元数据自动序列化，跨平台/跨编译器一致 |
| **场景 2：批量创建同类对象** | 手动写脚本逐个 spawn 和 set_component | Prefab 模板 + 一键实例化，支持局部覆盖 |
| **场景 3：网络同步只传变化数据** | 每帧把整个场景广播，带宽爆炸 | 基于 ChangeLog 的增量序列化，只传变更字段 |

> **核心结论**：Prefab 与数据层是引擎的"记忆系统"。没有它，世界只能存在于一帧之内，无法跨时间、跨会话、跨网络延续。

---

## What：最简化版本的序列化长什么样？

下面的代码建立在 [[反射系统]] 的 `TypeRegistry` 基础上，总计约 60 行。它增加了三个关键能力：

1. **JSON 序列化**：人类可读，适合调试和存档
2. **二进制序列化**：紧凑高效，适合网络同步
3. **数据块边界**：每个对象包裹大小前缀，支持快速跳过和版本兼容

```cpp
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

// 前置：来自 [[反射系统]]
enum class FieldType { Float, Int, Bool, Vec3 };
struct FieldDesc { std::string name; FieldType type; size_t offset; size_t size; };
struct TypeDesc { std::string name; size_t size; std::vector<FieldDesc> fields; };
class TypeRegistry { /* ... */ };

// ============================================================
// 1. JSON 序列化（基于反射元数据）
// ============================================================
void serializeJson(const TypeDesc* d, void* inst, std::string& out) {
    out += "{";
    for (size_t i = 0; i < d->fields.size(); ++i) {
        const auto& f = d->fields[i];
        void* ptr = (char*)inst + f.offset;
        out += "\"" + f.name + "\":";
        if (f.type == FieldType::Float) out += std::to_string(*(float*)ptr);
        else if (f.type == FieldType::Int) out += std::to_string(*(int*)ptr);
        else if (f.type == FieldType::Bool) out += *(bool*)ptr ? "true" : "false";
        if (i + 1 < d->fields.size()) out += ",";
    }
    out += "}";
}

// ============================================================
// 2. 二进制序列化
// ============================================================
class BinarySerializer {
public:
    std::vector<uint8_t> data;

    void write(const void* ptr, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), bytes, bytes + size);
    }

    void read(void* ptr, size_t size, size_t& offset) {
        std::memcpy(ptr, data.data() + offset, size);
        offset += size;
    }
};

void serializeObject(BinarySerializer& s, const TypeDesc* d, void* instance) {
    size_t block_start = s.data.size();
    s.write(uint32_t(0));  // 预留 data_block_size

    for (const auto& f : d->fields) {
        void* ptr = static_cast<char*>(instance) + f.offset;
        s.write(ptr, f.size); // 按字段逐个紧密写入，无 padding
    }

    size_t block_end = s.data.size();
    uint32_t block_size = block_end - block_start - sizeof(uint32_t);
    std::memcpy(s.data.data() + block_start, &block_size, sizeof(uint32_t));
}
```

### 这个最小实现已经解决了什么？

1. **通用序列化**：不依赖任何组件的具体类型，只依赖反射元数据。新增组件时，只要注册类型描述，就能自动被序列化。
2. **字段安全访问**：通过编译期确定的 `offset` 和 `size` 读写内存，避免了裸指针转换的未定义行为。
3. **数据块边界**：每个对象包裹 `data_block_size`，支持快速跳过和严格校验。

### 还缺什么？

1. **没有版本兼容**：二进制里没有写类型版本号，字段增删会导致旧存档无法读取。
2. **没有多态/引用解析**：如果组件里存的是 `Entity` 引用或基类指针，最小实现无法自动处理。
3. **没有 Prefab 概念**：还只是一个低级的序列化器，没有模板-实例化的抽象。
4. **没有增量优化**：每次都要序列化整个世界。

---

## How：真实引擎的数据层是如何一步一步复杂起来的？

### 阶段 1：从 JSON 到二进制 + 版本兼容（能用）

#### 触发原因

JSON 适合人类阅读和调试，但对于存档、网络包、资产文件来说太臃肿。阶段 1 的核心工程目标是把序列化格式切换为**紧凑的二进制**，并解决迭代中的版本兼容问题。

#### 1.1 版本号与可选字段机制

严格匹配的二进制格式在迭代期非常痛苦。阶段 1 需要引入**版本号**和**可选字段**机制：

```cpp
struct TypeVersion {
    std::string type_name;
    uint32_t    version{1};
};

struct VersionedFieldDesc : FieldDesc {
    uint32_t since_version{1};  // 从哪个版本开始有这个字段
    bool     optional{false};   // 缺失时是否使用默认值
};
```

**加载策略**：
- 写入时总是带上 `type_version`。
- 读取时如果磁盘版本 < 内存版本：缺失的 optional 字段使用默认值；非 optional 字段报错或触发 Migration 回调。
- 读取时如果磁盘版本 > 内存版本：跳过未知字段（利用 `data_block_size` 边界）。

**Migration 回调**：
```cpp
TypeRegistry::instance().registerMigration("Health", 1, 2, [](void* old_data, void* new_data) {
    // 从版本 1 迁移到版本 2：新增 regen_rate 字段
    auto* h = static_cast<Health*>(new_data);
    h->regen_rate = 0.0f;  // 默认值
});
```

#### 1.2 引用延迟解析（Handle/EntityRef）

ECS 中组件经常引用其他实体（如 `Entity parent` 或 `Handle<Texture> texture`）。这些引用在二进制流中不能写内存地址，而必须写**可重建的标识符**（如 `EntityID` 或 `FileID`）。

```cpp
struct EntityRef {
    uint32_t target_id{0xFFFFFFFF};
};

// 序列化时只写 target_id
// 反序列化时：
// 1. 读取 target_id
// 2. 把 (this_entity, field_index, target_id) 放入 pending_list
// 3. 等所有对象都创建完后，统一把 target_id 解析为实际 Entity 指针/索引
```

**延迟解析的好处**：
- 支持循环引用（A 引用 B，B 引用 A）。
- 支持异步资产加载（纹理还没从磁盘读完，可以先占位）。
- 网络同步时可以只传 `FileID`，接收端按本地资源表映射为实际句柄。

#### 1.3 POD 数组批量读取优化

对于 `std::vector<float>` 这类 POD 数组，不要逐元素调用序列化器，而是直接 `memcpy` 整片内存：

```cpp
if (is_pod_array) {
    s.write(array.data(), sizeof(T) * array.size());
} else {
    for (auto& elem : array) {
        serializeBinary(s, elem_type_name, &elem);
    }
}
```

这个优化让大量数值数据（如顶点缓冲、骨骼权重）的加载性能接近裸 `memcpy`。

---

### 阶段 2：Prefab 与实例化（好用）

#### 触发原因

场景里的敌人、树木、宝箱往往都是"同一类东西的不同实例"。没有 Prefab，编辑器就要为每个对象存储完整的组件数据，文件冗余且难以批量修改。

#### 2.1 Prefab 资产结构

```cpp
struct PrefabAsset {
    std::string prefab_id;
    std::vector<ComponentData> components; // 每个组件的序列化数据
    std::vector<PrefabAsset> children;     // 嵌套 Prefab
};
```

#### 2.2 实例化与局部覆盖

```cpp
struct PrefabInstance {
    std::string prefab_id;
    std::vector<ComponentOverride> overrides; // 局部覆盖的字段
};
```

实例化流程：
1. 从 `PrefabAsset` 反序列化出基础组件。
2. 应用 `overrides` 中记录的局部修改。
3. 生成新的 `Entity`。

> **关键设计**：覆盖只记录"差异"，而不是复制整个 Prefab。修改原始 Prefab 后，所有实例自动继承更新（除非该字段被局部覆盖）。

#### 2.3 场景序列化与反序列化

场景文件本质上是：
- 一组 Prefab 引用 + 覆盖数据
- 一组独立实体（非 Prefab 实例）
- 元数据（场景名、全局配置、相机初始位置等）

```cpp
struct SceneArchive {
    std::vector<PrefabInstance> prefab_instances;
    std::vector<EntityData> standalone_entities;
    SceneMetadata metadata;
};
```

---

### 阶段 3：增量序列化、网络裁剪与跨平台确定性（工业级）

#### 触发原因

当引擎进入生产环境，你需要回答三个问题：
1. 网络同步时，怎么只传**变化了的数据**？
2. 跨平台部署时，怎么保证不同 CPU 架构上的序列化格式一致？
3. AI 怎么能高效地理解世界状态的变化？

#### 3.1 增量序列化与 ChangeLog

不要把整个世界的状态每帧都发给 AI 或网络对端。你需要一个 **ChangeLog（变更日志）**：

```cpp
struct ComponentChange {
    uint32_t    entity_id;
    uint32_t    component_type_id;
    uint32_t    field_offset;
    uint8_t     value[32]; // 固定缓冲，存放变更后的字段值
};

class ChangeLog {
    ComponentChange* m_buffer;
    size_t m_capacity;
    size_t m_count = 0;
public:
    void record(uint32_t e, uint32_t comp, uint32_t off, const void* val, size_t sz) {
        if (m_count >= m_capacity || sz > 32) return;
        auto& c = m_buffer[m_count++];
        c.entity_id = e;
        c.component_type_id = comp;
        c.field_offset = off;
        memcpy(c.value, val, sz);
    }
    void clear() { m_count = 0; }
    const ComponentChange* data() const { return m_buffer; }
    size_t size() const { return m_count; }
};
```

> **注意**：上面的 `ChangeLog` 是一个**概念验证骨架**。工业级实现中，`m_buffer` 应由 [[容器与分配器]] 中的 `FrameArena` 或环形缓冲区分配，而不是 `new[]`。

**AI 上下文压缩**：
- AI 不需要看到"当前状态是什么"，而是"**自上次对话以来，状态发生了什么变化**"。
- 把 ChangeLog 格式化为 Markdown 表格发给 AI，token 消耗大幅降低。
- 如果 AI 需要完整状态，可以提供 `snapshot` 工具让它主动调用。

**网络同步优化**：
- 只序列化 `ChangeLog` 中标记为 `Replicated` 的字段。
- 对高频变化数据（如 Transform）做 delta compression 或量化（把 float 压缩为 16bit 定点数）。

#### 3.2 网络裁剪序列化（Net Saver）

网络包和本地存档对序列化的需求不同。网络序列化需要"裁剪"掉不需要同步的信息：

```cpp
enum class SerializeFlags {
    None       = 0,
    Network    = 1 << 0,  // 只同步标记为 Network 的字段
    SaveGame   = 1 << 1,  // 只保存标记为 Persistent 的字段
    Editor     = 1 << 2,  // 编辑器场景文件：保存所有字段
};

struct FieldDesc {
    std::string name;
    FieldType   type;
    size_t      offset;
    size_t      size;
    uint32_t    flags;  // 该字段在哪些场景下序列化
};
```

序列化器根据 `flags` 过滤字段，自动生成三种不同的二进制流。

#### 3.3 跨平台与确定性保障

**字节序**：网络同步和存档必须在不同平台（x86、ARM、主机）上保持一致。方案有两种：
1. **统一小端**：所有写入都按小端序，读取时根据平台做字节交换。
2. **统一大端**：同上，只是选择大端作为标准格式。

现代游戏引擎几乎都选择**小端统一**（因为 x86/ARM 默认都是小端，只有少量主机和旧网络协议是大端）。

**内存对齐**：
- 序列化格式中不能包含编译器插入的 padding 字节。
- 方案 A：按字段逐个紧凑写入，忽略结构体对齐。
- 方案 B：定义 `#pragma pack(1)` 的序列化镜像结构体。

**确定性**：
- 所有浮点数运算结果在不同平台上可能略有差异（如 x86 80bit 扩展精度 vs SSE 32bit）。
- 对于需要确定性回放的场景（如网络同步），序列化前应对浮点数做**固定精度量化**（如保留 3 位小数），或完全避免在同步路径上使用浮点逻辑状态。

---

## AI 友好设计红线检查

| 检查项 | 本模块的满足方式 |
|--------|-----------------|
| **状态平铺** | 序列化只处理 ECS 组件的平铺字段，不序列化指针或虚表。 |
| **自描述** | `TypeRegistry` 和 `ENGINE_AI.md` Schema 让 AI 无需读头文件即可理解数据结构。 |
| **确定性** | 序列化格式统一小端、无 padding、浮点做量化。同一世界快照在不同平台上反序列化后完全一致。 |
| **工具边界** | AI 通过 MCP 调用 `save_scene(name)`、`load_scene(name)`、`query_entity`。输入输出都是基于 Schema 的结构化 JSON。 |
| **Agent 安全** | Prefab 实例化走受控的工厂路径；AI 不能直接写原始二进制流破坏格式。 |

---

## 如果我要 vibe coding，该偷哪几招？

1. **偷"数据块边界封装"**
   - 每个对象在二进制流中包裹 `data_block_size`。这是版本兼容、快速跳过、严格校验的共同基础。

2. **偷"引用延迟解析"**
   - 对 `EntityRef` 和 `Handle` 采用 pending list 机制。反序列化第一阶段读 ID 占位，第二阶段统一解析为实际索引。支持循环引用和异步加载。

3. **偷"Prefab 只存差异"**
   - 实例文件只记录覆盖字段，不复制整个 Prefab。这是控制场景文件大小的最便宜方式。

4. **偷"ChangeLog 压缩 AI 上下文"**
   - 不要每次对话都把整个世界发给 AI。维护一个 `ChangeLog`，只向 AI 汇报"自上次以来变了什么"。

5. **偷"字段 flags 控制序列化场景"**
   - 用 `Network | Persistent | Editor` 等 flag 标记字段，自动生成三种不同的序列化路径，避免手写三套逻辑。

---

## 下一步预告

现在你的引擎已经能：
- ✅ 用 ECS 组织实体和组件
- ✅ 用反射运行时描述组件结构
- ✅ 用序列化保存、加载和传输世界状态
- ✅ 用 Prefab 批量实例化和复用对象

但还缺最后一块核心拼图：**人类和 AI 如何直观地观察和操作这个世界？**

在 [[编辑器框架]] 中，我们将解决：
- 如何基于反射自动生成 Inspector 面板？
- 如何用 Command Pattern 实现 Undo/Redo？
- 如何隔离编辑态和运行态（PIE 模式）？
- 如何让 AI 通过结构化协议安全地修改场景？

> **下一步**：[[编辑器框架]]

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])
