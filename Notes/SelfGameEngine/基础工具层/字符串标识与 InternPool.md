---
title: 字符串标识与 InternPool
date: 2026-06-01
tags:
  - self-game-engine
  - foundation
  - string
  - stringid
  - intern
aliases:
  - StringId and InternPool
---

> **前置依赖**：[[可变字符串与 SSO]]、[[字符串系统]]
> **本模块增量**：深入理解字符串标识系统的设计空间——从哈希算法选择到 UE FNamePool 的工业级分块分配与无锁读取架构。
>
> 本笔记是 [[字符串系统]] 话题拆分的子笔记之一，聚焦"字符串标识与全局池"这个主题。关于字符串在 ECS 中的角色分工，详见 [[字符串系统#三种字符串角色的精确定义]]。

# 字符串标识与 InternPool

## 问题 0：为什么需要 StringId？

SSO 解决了分配问题，但没有解决比较问题。假设你有一万个组件，每帧都要查找类型为 `"MeshRenderer"` 的实体：

```cpp
// 问题：每次比较都要逐字节遍历
for (auto& c : components) {
    if (c.typeName == "MeshRenderer") { ... } // O(n) 逐字节比较
}
```

即使 SSO 让 `c.typeName` 的内联访问很快，逐字节比较 `"MeshRenderer"`（12 字节）在热路径上依然昂贵——而且分支预测失败时会更慢。

引擎中需要频繁比较的字符串场景：
- 组件类型名匹配
- 资源路径查找
- 事件名订阅过滤
- 配置键查询

这些场景的共性是：**字符串内容在运行期基本不变，但比较操作每帧发生成千上万次**。

---

## 问题 1：编译期哈希——最小可行方案

```cpp
// 编译期计算字符串哈希
constexpr u64 hashFNV1a(const char* str, u64 h = 0xcbf29ce484222325ULL) {
    return *str == '\0' ? h : hashFNV1a(str + 1, (h ^ static_cast<u8>(*str)) * 0x100000001b3ULL);
}

class StringId {
    u64 m_hash;
public:
    constexpr StringId(const char* str) : m_hash(hashFNV1a(str)) {}
    constexpr bool operator==(StringId o) const { return m_hash == o.m_hash; }
};

// 用法
constexpr StringId MESH_RENDERER("MeshRenderer");
for (auto& c : components) {
    if (c.typeId == MESH_RENDERER) { ... } // O(1) 整数比较！
}
```

这个方案已经很好了：比较从 O(n) 降到 O(1)，编译期常量可以在 switch-case 中使用。但运行时构造怎么办？

---

## 问题 2：运行时字符串的哈希冲突与 Intern 池

```cpp
// 运行时从配置文件读到的字符串
std::string path = config.readString("texture_path");
StringId id(path.c_str()); // 运行时哈希
```

FNV-1a 是 64 位哈希，碰撞概率理论上是 $1/2^{64}$，可以认为是"不可能"。但在一个确定性的 ECS 引擎中，**"几乎不可能"不等于"安全"**。如果 AI Agent 通过 MCP 接口修改资源名，恰好触发了哈希碰撞，两个不同的资源会被映射到同一个 ID，导致静默错误。

### Naive 方案：StringId + 简化 Intern 池

```cpp
class StringInternPool {
    HashMap<u64, const char*> m_pool; // hash -> 原始字符串
public:
    StringId intern(const char* str) {
        u64 h = hashFNV1a(str);
        auto it = m_pool.find(h);
        if (it != m_pool.end()) {
            if (std::strcmp(it->second, str) != 0) {
                assert(false && "StringId hash collision detected!");
            }
            return StringId(h);
        }
        char* copy = new char[std::strlen(str) + 1];
        std::strcpy(copy, str);
        m_pool[h] = copy;
        return StringId(h);
    }
};
```

这个简化实现的问题：
- `HashMap` 查找在热点路径上有锁竞争
- `new char[...]` 每次 intern 都触发独立的小分配
- 没有处理哈希冲突的链表/开放寻址
- 内存只增不减

下面来看工业级实现是怎么解决这些问题的。

---

## 工业实现深度：字符串哈希算法选择

主笔记直接使用了 FNV-1a。但 FNV-1a 不是唯一选择，也不是在所有场景下都最优。

| 算法              | 输出位宽      | 小字符串性能  | 大字符串性能   | 质量  | 适用场景       |
| --------------- | --------- | ------- | -------- | --- | ---------- |
| **FNV-1a**      | 32/64     | 极快（单循环） | 慢（逐字节）   | 一般  | 编译期常量、教学实现 |
| **djb2**        | 32        | 极快      | 慢        | 差   | 不推荐（碰撞率高）  |
| **MurmurHash3** | 32/128    | 快       | 很快       | 很好  | 通用哈希表键     |
| **CityHash**    | 32/64/128 | 快       | 极快（SIMD） | 很好  | 大字符串、网络包   |
| **xxHash**      | 32/64     | 很快      | 极快（SIMD） | 极好  | 文件校验、大字符串  |

**为什么 FNV-1a 在小字符串上够用？**

引擎中的字符串大多是短的（组件名、资源短名）。对短字符串（< 32 字节），哈希算法的性能差距很小：

```
字符串长度 = 12 字节（如 "MeshRenderer"）
FNV-1a:  12 次乘法 + 12 次 XOR  ≈  24 条指令
xxHash:  虽然内部用 SIMD，但前端开销（初始化、finalize）比 FNV-1a 大
        对 12 字节字符串，xxHash 可能比 FNV-1a 慢 2~3 倍
```

**结论**：
- **编译期常量哈希**：FNV-1a 是最佳选择——简单、可 constexpr、无依赖
- **运行期小字符串哈希（< 64 字节）**：FNV-1a 或简化版 xxHash
- **运行期大字符串哈希（> 256 字节）**：xxHash 或 CityHash——SIMD 优化明显
- **文件内容校验**：xxHash64——质量高、速度极快

---

## 工业实现深度：UE FNamePool 的架构

UE 的 `FNamePool` 是字符串 intern 系统的工业标杆。它的核心设计可以提炼为四个层次：

### 层次 1：分块分配（Short / Medium / Large）

不是所有字符串都走同一个分配器。UE 按长度分块：

```
FNamePool 内存架构
├── FNameEntryHeader (2 字节：len + flags)
│   └── 每个 entry 的前 2 字节存储长度和标志
├── 分块存储
│   ├── Short Block (≤ 8 字节字符数据)：内联在 entry 中
│   ├── Medium Block (9~32 字节)：内联在 entry 中
│   └── Large Block (> 32 字节)：entry 只存指针，数据在另一处分配
```

**为什么分块？**
- 短字符串（如 `"Transform"`、`"Mesh"`）占引擎字符串的 90% 以上
- 把它们内联在 entry 中意味着 `resolve()` 只需要一次指针跳转
- 每个 block 按固定大小分配（如 4096 字节一页），减少碎片

```cpp
// 简化后的分块 entry 设计
struct FNameEntry {
    uint16_t header;  // [15] = is_wide, [14] = is_large, [13:0] = length
    union {
        char inlineData[NAME_MAX_INLINE_LEN];  // Short/Medium
        struct { void* externalData; size_t capacity; } large;  // Large
    };
    uint32_t hash;    // 缓存的哈希值，避免重复计算
};
```

### 层次 2：无锁读取

FNamePool 的读取路径（`resolve`、`比较`）是**无锁**的。

```cpp
class FNamePool {
    // 分块数组，一旦分配永不移动
    FNameEntryBlock* blocks[MAX_BLOCKS];  // 指针数组，只增不减
    std::atomic<uint32_t> numBlocks;      // 原子计数，只增不减
    
    // 哈希表：hash → entry index（不是指针！）
    std::atomic<uint32_t>* hashTable;     // 开放寻址，CAS 插入
};
```

**关键设计**：
- `blocks` 数组中的指针一旦写入就不再修改。新 block 通过 `numBlocks.fetch_add(1)` 原子地追加
- 读取时：先读 `numBlocks.load()` 获取当前 block 数量，然后遍历这些 block（都是只读的）
- 哈希表使用**开放寻址 + CAS 插入**。读取时不需要锁，因为 entry 一旦写入就不可变

```cpp
const char* FNamePool::resolve(FNameEntryId id) const {
    uint32_t blockIdx = id.blockIndex;
    uint32_t entryIdx = id.entryIndex;
    
    // 读取 block 指针（memory_order_acquire 保证看到完整的 entry）
    FNameEntryBlock* block = blocks[blockIdx].load(std::memory_order_acquire);
    
    // 从 block 中读取 entry（entry 一旦写入就不可变）
    FNameEntry* entry = block->getEntry(entryIdx);
    
    return entry->getData();
}
```

**为什么 entry 一旦写入就不可变？**
- intern 操作是"只增不删"的。一个字符串被 intern 后，它的 entry 地址和字符数据永远不会改变
- 这保证了读取路径上不需要任何同步

### 层次 3：TLS 线程本地缓冲 + 批量提交

多线程同时 intern 新字符串时，即使哈希表用 CAS，高竞争下性能也会下降。UE 的解决方案是**线程本地缓冲**：

```cpp
thread_local FNameThreadLocalCache tlsCache;

StringId StringInternPool::intern(StringView str) {
    // 1. 先在线程本地缓存中查找
    if (auto* cached = tlsCache.find(str.hash())) {
        return cached->id;
    }
    
    // 2. 在全局池中查找（无锁读取）
    if (auto id = findInGlobalPool(str)) {
        tlsCache.insert(str.hash(), id);
        return id;
    }
    
    // 3. 需要创建新 entry——批量提交策略
    tlsCache.pending.push_back(str);
    
    // 4. 当待提交队列达到一定阈值，一次性批量提交到全局池
    if (tlsCache.pending.size() >= 16) {
        bulkCommit(tlsCache.pending);
        tlsCache.pending.clear();
    }
}
```

**批量提交的优势**：
- 减少全局 CAS 的竞争频率（16 次 intern → 1 次全局操作）
- 可以一次性分配一大块内存

### 层次 4：大小写不敏感

UE 的 `FName` 默认是**大小写不敏感**的。`"Texture"` 和 `"texture"` 映射到同一个 ID。

```cpp
uint32_t caseInsensitiveHash(StringView str) {
    uint32_t h = 0;
    for (char c : str) {
        h = h * 31 + tolower(c);  // ASCII fast path
    }
    return h;
}
```

**陷阱**：`tolower` 有 locale 依赖。UE 的做法是只支持 ASCII 大小写转换，非 ASCII 字符按原样处理。

---

## 引擎在字符串标识上的具体方案

| 引擎        | 字符串标识方案                  | Intern 策略                                                               | 关键特征                                                                                                                              |
| --------- | ------------------------ | ----------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **UE**    | `FName` + `FNamePool`    | **分块分配**（Short/Medium/Large）、**无锁读取**（开放寻址+CAS）、**TLS 批量提交**、**大小写不敏感** | 工业级标杆。FName 是 UE 中使用最频繁的类型之一                                                                                                      |
| **chaos** | `StringID` + `StringKey` | **编译期 FNV-1a 哈希**，无运行时 Intern 池                                         | `StringID` 是纯 32 位哈希值包装类，`STRING_ID("camera")` 在编译期计算常量。`StringKey` 由 1~4 个 `StringID` 组合成层级键（如 `"camera.set_track"`），用于属性表、动画状态机 |
| **Bevy**  | **不用字符串标识组件**            | 无 Intern 池（因为不需要）                                                       | Bevy 用 Rust 的 `TypeId`（编译期生成的唯一类型标识）来标识组件类型。资源路径用 `AssetPath<'static>` + `AssetServer`，热路径上全是 `Handle<A>` 整数比较                    |

**关键观察**：
- UE 的 FNamePool 是 OOP 引擎中最完善的字符串 intern 系统，分块+无锁+TLS 三层优化
- chaos 的 `StringID` 是编译期哈希，适合标识符场景，但**没有运行时 Intern 池的去重保证**——碰撞依赖哈希算法的统计安全性
- Bevy 的策略是"在 ECS 架构下，很多字符串问题可以通过不用字符串来解决"——组件类型用 `TypeId`，资源引用用 `Handle<A>`

---

## 自研引擎的 InternPool 推荐实现

基于 UE 的设计，在 ECS 架构下的最小工业级实现：

```cpp
class StringInternPool {
    static constexpr usize BLOCK_SIZE = 4096;
    static constexpr usize MAX_INLINE_LEN = 32;
    
    struct Entry {
        uint16_t len;
        uint16_t hash16;        // 哈希低 16 位（二次校验）
        union {
            char inlineData[MAX_INLINE_LEN];
            struct { char* ptr; usize cap; } external;
        };
    };
    
    struct Block {
        alignas(alignof(Entry)) char data[BLOCK_SIZE];
        uint32_t numEntries;
        Block* next;
    };
    
    Block* blocks[256];
    std::atomic<uint32_t> numBlocks{0};
    
    struct Slot { uint32_t blockIdx; uint32_t entryIdx; };
    std::atomic<Slot>* hashTable;
    uint32_t hashTableSize;
    
    std::mutex insertMutex;

public:
    StringId intern(StringView str);
    StringView resolve(StringId id) const; // 无锁读取
};
```

**关键取舍**：
- 读取路径完全无锁 → 适合热路径
- 写入路径用全局锁 + 批量提交 → 假设 intern 频率远低于读取
- 如果引擎需要运行时频繁 intern（如动态生成的 `"Entity_12345"`），升级到 TLS 缓冲

---

## ECS 映射：字符串标识在 ECS 中怎么用？

```cpp
// ✅ 正确：StringId 是纯值类型，8 字节，可密集存储
struct NameComponent {
    StringId nameId; // u64，纯值，无指针
};

// 实际字符串内容存在 ECS Resource（全局状态）中
struct NameRegistry : public ECSResource {
    StringInternPool pool;
    HashMap<Entity, StringId> entityNames;
};
```

这样设计的收益：
- `NameComponent` 是 8 字节纯值，可以完美放入 ECS 的 SoA/Archetype 存储
- 字符串内容只在需要时通过 `NameRegistry` 反查
- 序列化时，`StringId` 可以直接写成 u64 或反查后的字符串
- AI 观察实体时，看到的是一个扁平的整数数组

关于从 OOP 到 ECS 的完整重构映射，详见 [[字符串系统#ECS 映射]]。

---

> **下一步**：[[字符串格式化]] — 字符串标识解决了比较问题，但日志、调试输出仍然需要把数据转换成人类可读的文本。如何零分配、类型安全地格式化字符串？
