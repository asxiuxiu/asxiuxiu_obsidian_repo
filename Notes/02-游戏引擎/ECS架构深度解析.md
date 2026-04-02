# ECS架构深度解析

## Why：为什么要用ECS？

### 解决的问题

传统OOP在游戏开发中会遇到三个核心痛点：

**1. 缓存不友好（Cache Miss）**
```cpp
// OOP：对象分散在内存堆上
class Enemy {
    Transform transform;  // 可能相距很远
    Health health;        // 导致CPU缓存不断失效
    AI ai;
};
```

**2. 并行困难**
```cpp
// OOP：修改敌人位置需要持有整个Enemy对象
void Update(Enemy& e) { 
    e.transform.position += velocity; 
    // 无法安全并行，因为Enemy包含太多状态
}
```

**3. 编译耦合**
```cpp
// OOP：修改基类接口，所有派生类重编译
class Character { virtual void Jump() = 0; };
class Player : public Character { ... };  // 必须重新编译
class Enemy : public Character { ... };   // 必须重新编译
```

### ECS的优势

| 问题 | OOP表现 | ECS表现 |
|------|---------|---------|
| 缓存效率 | 低（指针跳转） | 高（连续内存） |
| 并行性 | 困难（状态耦合） | 天然（数据隔离） |
| 编译隔离 | 差（继承依赖） | 优（仅数据依赖） |
| 运行时组合 | 困难（继承固定） | 灵活（动态添加组件） |

---

## What：ECS的核心机制

### 1. 内存布局：SoA vs AoS

**传统OOP（AoS - Array of Structures）**
```cpp
struct Enemy {
    Vector3 position;   // 12 bytes
    int health;         // 4 bytes  
    AIState ai;         // 8 bytes
}; // 24 bytes/实体

// 内存布局：[pos1|health1|ai1][pos2|health2|ai2][pos3|health3|ai3]
// 问题：position不连续，CPU无法批量预取
```

**ECS（SoA - Structure of Arrays）**
```cpp
// Component = 纯数据
struct Position { float x, y, z; };
struct Health { int value; };
struct AIState { int behavior; };

// 内存布局：
// Positions: [pos1][pos2][pos3][pos4]...  // 连续！
// Healths:   [hp1][hp2][hp3][hp4]...      // 连续！
// AIStates:  [ai1][ai2][ai3][ai4]...      // 连续！
```

**为什么SoA更快？**

现代CPU有**缓存行（Cache Line，通常64字节）**：
- AoS：读取`position`时，缓存行混入了`health`和`ai`，浪费带宽
- SoA：读取`Positions`数组时，一次缓存行加载包含4-5个`Position`，无浪费

```
CPU读取 Position[0]:
┌────────────────────────────────────────┐
│ AoS (传统)                             │
│ [pos0|hp0|ai0][pos1|hp1|ai1]...       │
│  ↑ 只需要这个                           │
│  └── 但缓存行包含 hp0, ai0（浪费24字节） │
├────────────────────────────────────────┤
│ SoA (ECS)                              │
│ [pos0][pos1][pos2][pos3][pos4]...     │
│  ↑ 缓存行全是Position                    │
│  └── 64字节包含5个Position，0浪费        │
└────────────────────────────────────────┘
```

### 2. Archetype 存储模型

**Archetype = 组件组合签名**

```cpp
// 实体1：有 Position + Velocity
Entity e1 = world.create();
e1.add<Position>({0, 0, 0});
e1.add<Velocity>({1, 0, 0});
// Archetype: [Position, Velocity]

// 实体2：有 Position + Velocity + Health
Entity e2 = world.create();
e2.add<Position>({10, 0, 0});
e2.add<Velocity>({0, 1, 0});
e2.add<Health>(100);
// Archetype: [Position, Velocity, Health]
```

**存储结构**：
```
World
├── Archetype [Position, Velocity]
│   ├── Chunk 0 (容纳100个实体)
│   │   ├── Positions: [vec3][vec3][vec3]...
│   │   └── Velocities: [vec3][vec3][vec3]...
│   └── Chunk 1 (容纳100个实体)
│       └── ...
│
└── Archetype [Position, Velocity, Health]
    ├── Chunk 0
    │   ├── Positions: [vec3][vec3]...
    │   ├── Velocities: [vec3][vec3]...
    │   └── Healths: [int][int]...
```

**查询过程**：
```cpp
// 查询所有有 Position + Velocity 的实体
world.query<Position, Velocity>().each([](Position& p, Velocity& v) {
    p += v * dt;
});

// 实际执行：
// 1. 找到匹配 Archetype [Position, Velocity]
// 2. 找到匹配 Archetype [Position, Velocity, Health]（包含关系）
// 3. 顺序遍历每个 Chunk 的数组 → 缓存友好！
```

### 3. System 的执行模型

**System = 纯函数**
```cpp
// System 不持有状态，只处理输入数据
void MovementSystem(World& world, float dt) {
    // 只读取 Position 和 Velocity，不影响其他组件
    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        pos.value += vel.value * dt;
    }
}

void RenderSystem(World& world) {
    // 只读取 Position 和 Sprite
    for (auto [pos, sprite] : world.query<Position, Sprite>()) {
        DrawSprite(pos.value, sprite.texture);
    }
}
```

**并行调度**：
```
帧更新：
┌─────────────────────────────────────────┐
│  MovementSystem (写 Position)           │
│  依赖：Velocity (读)                    │
├─────────────────────────────────────────┤
│  CollisionSystem (写 Velocity)          │  ← 可与Movement并行？
│  依赖：Position (读)                    │     不行！数据竞争
├─────────────────────────────────────────┤
│  RenderSystem (只读)                    │  ← 可与上面并行！
│  依赖：Position, Sprite (只读)          │     无数据竞争
└─────────────────────────────────────────┘
```

---

## How：ECS实践指南

### 1. 组件设计原则

**DO：细粒度组件**
```cpp
// ✅ 好：独立、可复用
struct Position { vec3 value; };
struct Rotation { vec3 value; };
struct Scale { vec3 value; };
struct Velocity { vec3 value; };
struct Health { int value; };
```

**DON'T：上帝组件**
```cpp
// ❌ 坏：臃肿、耦合
struct Transform {  // 合并了位置、旋转、缩放
    vec3 position;
    vec3 rotation;  
    vec3 scale;
};

struct CharacterStats {  // 合并了所有属性
    int health;
    int mana;
    int strength;
    int agility;
    // ... 50个字段
};
```

### 2. System 设计原则

**DO：单一职责**
```cpp
// ✅ 好：每个System只做一件事
class MovementSystem : public System {
    void Update(World& w) {
        w.query<Position, Velocity>().each([](auto& p, auto& v) {
            p.value += v.value * dt;
        });
    }
};

class GravitySystem : public System {
    void Update(World& w) {
        w.query<Velocity>().each([](auto& v) {
            v.value.y -= 9.8f * dt;
        });
    }
};
```

**DON'T：万能System**
```cpp
// ❌ 坏：一个System做所有事
class GameplaySystem : public System {
    void Update(World& w) {
        // 处理移动
        // 处理重力
        // 处理输入
        // 处理AI
        // 处理碰撞
        // ... 500行代码
    }
};
```

### 3. 实体创建模式

**原型模式（Prototype Pattern）**
```cpp
// 创建"玩家"原型
Entity playerPrefab = world.create();
playerPrefab.add<Position>({0, 0, 0});
playerPrefab.add<Velocity>({0, 0, 0});
playerPrefab.add<Health>(100);
playerPrefab.add<PlayerTag>();

// 实例化玩家
Entity player1 = world.instantiate(playerPrefab);
Entity player2 = world.instantiate(playerPrefab);
```

### 4. 常见陷阱

| 陷阱 | 说明 | 解决 |
|------|------|------|
| **过度ECS化** | 用ECS做UI、状态机 | UI用事件驱动，状态机用OOP |
| **频繁增删组件** | 导致Archetype切换，内存复制 | 用Tag组件标记状态，而非删除 |
| **System顺序依赖** | 隐式依赖导致bug | 显式声明依赖关系 |
| **指针失效** | 组件数组重分配 | 用Entity ID而非指针 |

---

## 性能对比数据

基于2024-2025年基准测试（100万实体）：

| 操作 | OOP | ECS (Archetype) | 提升 |
|------|-----|-----------------|------|
| 遍历更新位置 | 85ms | 25ms | **3.4x** |
| 批量添加组件 | 45ms | 120ms | 0.4x（劣势） |
| 单实体查询 | 5ns | 13ns | 0.4x（劣势） |
| 并行System执行 | 不支持 | 线性加速 | N/A |

**结论**：ECS在**批量迭代**场景优势明显，在**随机访问**和**频繁结构变化**场景劣势。
