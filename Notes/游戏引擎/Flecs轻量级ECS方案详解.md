> [[索引|← 返回 游戏引擎索引]]

# Flecs 轻量级 ECS 方案详解

## Why：为什么需要 Flecs？

对于追求**编译速度**和**架构纯粹性**的开发者，商业引擎往往过于沉重：

| 痛点 | Unity/UE | Flecs |
|------|----------|-------|
| 编译时间 | 分钟级 | **秒级** |
| 依赖复杂度 | 数百个头文件、宏系统 | **零依赖** |
| 源码可修改性 | 部分封闭/需授权 | **完全开源** |
| 运行时反射开销 | UHT代码生成/运行时虚函数 | **无代码生成** |
| 内存占用 | 编辑器+运行时数GB | **库本身<1MB** |

Flecs 提供了**最纯粹的ECS实现**，不与任何渲染、物理、音频系统绑定，可以嵌入任何C/C++项目。

---

## What：Flecs 核心特性

### 1. 架构定位

```
Flecs = ECS 逻辑框架（非完整引擎）
    ↓ 你需要自行组合
渲染：raylib / SDL / bgfx / 自研
物理：Bullet / PhysX / 自研
音频：SDL_mixer / OpenAL / 自研
输入：GLFW / SDL
```

### 2. 核心设计哲学

| 特性 | 实现方式 | 收益 |
|------|---------|------|
| **数据驱动** | Component只有数据 | 缓存友好、序列化简单 |
| **逻辑分离** | System是纯函数 | 编译隔离、易于测试 |
| **无模板** | C99实现，类型擦除 | 编译极快、无代码膨胀 |
| **模块化** | Module系统 | 可组合、可复用 |
| **运行时反射** | 内省API | 无需代码生成、调试友好 |

### 3. 存储模型：Archetype

Flecs采用**Archetype-based**存储（与Unity DOTS相同）：

```
World
├── Archetype [Position, Velocity]
│   ├── Chunk 0 (1024实体容量)
│   │   ├── Positions: [vec3][vec3][vec3]...
│   │   └── Velocities: [vec3][vec3][vec3]...
│   └── Chunk 1
│       └── ...
│
└── Archetype [Position, Velocity, Health]
    ├── Chunk 0
    │   ├── Positions: [vec3][vec3]...
    │   ├── Velocities: [vec3][vec3]...
    │   └── Healths: [int][int]...
```

**为什么是Archetype？**
- SoA（Structure of Arrays）布局 → 缓存友好
- 相同Archetype的实体在内存中连续 → 批量处理快
- 查询时只需遍历匹配的Archetype → 无需过滤

---

## How：Flecs 使用指南

### 基础示例

```cpp
#include <flecs.h>
#include <raylib.h>  // 渲染库

// 1. 定义组件（纯数据结构）
struct Position { float x, y; };
struct Velocity { float x, y; };
struct Sprite { Color color; float radius; };

// 2. System 实现
void MovementSystem(flecs::iter& it, Position* p, Velocity* v) {
    for (auto i : it) {
        p[i].x += v[i].x * it.delta_time();
        p[i].y += v[i].y * it.delta_time();
    }
}

void RenderSystem(flecs::world& world) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    
    // 查询所有有 Position + Sprite 的实体
    world.query<Position, Sprite>().each(
        [](const Position& p, const Sprite& s) {
            DrawCircle(p.x, p.y, s.radius, s.color);
        }
    );
    
    EndDrawing();
}

int main() {
    // 初始化
    flecs::world world;
    InitWindow(800, 600, "Flecs + Raylib");
    SetTargetFPS(60);
    
    // 注册组件
    world.component<Position>();
    world.component<Velocity>();
    world.component<Sprite>();
    
    // 创建System
    world.system<Position, Velocity>()
        .iter(MovementSystem);
    
    // 创建实体
    world.entity()
        .set<Position>({400, 300})
        .set<Velocity>({100, 50})
        .set<Sprite>({RED, 20});
    
    // 主循环
    while (!WindowShouldClose()) {
        world.progress(GetFrameTime());  // 执行所有System
        RenderSystem(world);
    }
    
    world.cleanup();
    CloseWindow();
    return 0;
}
```

### 核心概念详解

#### 1. Entity（实体）

```cpp
// 实体是轻量ID（64位整数）
flecs::entity e = world.entity();

// 添加组件
e.set<Position>({10, 20});
e.set<Velocity>({1, 0});

// 移除组件
e.remove<Velocity>();

// 删除实体
e.destruct();
```

**Entity ID 结构**：
```
64位 Entity ID：
┌──────────────────────┬─────────────────┐
│     Entity Index     │   Generation    │
│      (32 bits)       │    (16 bits)    │
└──────────────────────┴─────────────────┘

- Index：实体在存储数组中的位置
- Generation：防止ID复用导致的悬垂引用
```

#### 2. Component（组件）

```cpp
// POD（Plain Old Data）结构体
struct Position {
    float x, y;
};

// 可以包含方法（但不推荐）
struct Health {
    int current;
    int max;
    
    float GetPercent() const {  // 辅助方法OK
        return (float)current / max;
    }
};

// 注册时指定行为
world.component<Health>()
    .member<int>("current")
    .member<int>("max");
```

**组件生命周期钩子**：
```cpp
world.component<Position>()
    .ctor([](void* ptr, int count) {  // 构造函数
        std::cout << "Position created\n";
    })
    .dtor([](void* ptr, int count) {  // 析构函数
        std::cout << "Position destroyed\n";
    });
```

#### 3. System（系统）

**定义方式**：
```cpp
// 方式1：Lambda（适合简单逻辑）
world.system<Position, Velocity>("Movement")
    .each([](Position& p, Velocity& v) {
        p.x += v.x;
        p.y += v.y;
    });

// 方式2：迭代器（适合批量处理）
world.system<Position, Velocity>()
    .iter([](flecs::iter& it, Position* p, Velocity* v) {
        for (auto i : it) {
            p[i].x += v[i].x * it.delta_time();
        }
    });

// 方式3：函数指针（适合分离声明和定义）
void Move(flecs::iter& it, Position* p, Velocity* v);
world.system<Position, Velocity>().iter(Move);
```

**执行顺序控制**：
```cpp
// 使用 Phase（执行阶段）
world.system<Position>("EarlyUpdate")
    .kind(flecs::PreUpdate)  // 在Update前执行
    .each([](Position& p) { ... });

world.system<Sprite>("LateUpdate")
    .kind(flecs::PostUpdate)  // 在Update后执行
    .each([](Sprite& s) { ... });

// 自定义依赖
world.system<Position>("A").each(...);
world.system<Position>("B")
    .depends_on("A")  // B在A之后执行
    .each(...);
```

#### 4. Query（查询）

```cpp
// 基础查询
auto q = world.query<Position, Velocity>();

// 带条件的查询
auto q = world.query_builder<Position, Velocity>()
    .term<Enemy>()           // 必须包含Enemy
    .term<Dead>().not_()     // 不能包含Dead
    .term<Health>().oper(flecs::Optional)  // Health可选
    .build();

// 执行查询
q.each([](Position& p, Velocity& v) {
    // 处理每个匹配的实体
});

// 缓存查询（推荐）
// 创建一次，多次使用
```

#### 5. Module（模块）

```cpp
// PhysicsModule.h
namespace PhysicsModule {
    struct Position { float x, y; };
    struct Velocity { float x, y; };
    struct Mass { float value; };
    
    void Init(flecs::world& world) {
        world.module<PhysicsModule>();
        
        world.component<Position>();
        world.component<Velocity>();
        world.component<Mass>();
        
        world.system<Position, Velocity>("Movement")
            .each([](Position& p, Velocity& v) {
                p.x += v.x;
                p.y += v.y;
            });
        
        world.system<Velocity, Mass>("Gravity")
            .each([](Velocity& v, Mass& m) {
                v.y -= 9.8f * m.value;
            });
    }
}

// main.cpp
#include "PhysicsModule.h"

int main() {
    flecs::world world;
    PhysicsModule::Init(world);  // 导入模块
    
    // 使用模块中的组件
    world.entity()
        .set<PhysicsModule::Position>({0, 0})
        .set<PhysicsModule::Velocity>({0, 0})
        .set<PhysicsModule::Mass>({1.0f});
}
```

### 高级特性

#### 1. 观察者（Observer）

```cpp
// 监听组件添加事件
world.observer<Position>()
    .event(flecs::OnAdd)
    .each([](flecs::entity e, Position& p) {
        std::cout << "Position added to " << e.name() << "\n";
    });

// 监听组件删除事件
world.observer<Health>()
    .event(flecs::OnRemove)
    .each([](flecs::entity e, Health& h) {
        if (h.current <= 0) {
            e.add<Dead>();  // 血量归零自动添加死亡标记
        }
    });
```

#### 2. 关系（Relationships）

```cpp
// 定义关系类型
struct Likes { };
struct Eats { };

// 创建关系
flecs::entity apples = world.entity("Apples");
flecs::entity bob = world.entity("Bob");

bob.add<Likes>(apples);  // Bob likes Apples
bob.add<Eats>(apples);   // Bob eats Apples

// 查询关系
world.query<Likes>().each([](flecs::entity e, Likes) {
    // e 是喜欢某物的实体
});

// 查询特定关系目标
world.query_builder<>()
    .term<Likes>(apples)  // 查询喜欢Apples的实体
    .build();
```

#### 3. 预制体（Prefabs）

```cpp
// 创建预制体
flecs::entity enemyPrefab = world.prefab("EnemyPrefab")
    .set<Health>({100, 100})
    .set<Speed>({5})
    .add<AI>();

// 实例化
flecs::entity enemy1 = world.entity().is_a(enemyPrefab);
flecs::entity enemy2 = world.entity().is_a(enemyPrefab);

// 覆盖组件
enemy1.set<Health>({150, 150});  // 这个敌人血量更高
```

#### 4. 序列化（Reflection）

```cpp
// 自动序列化支持
world.component<Position>()
    .member<float>("x")
    .member<float>("y");

// 从JSON创建实体
const char* json = R"({
    "entities": {
        "Player": {
            "Position": {"x": 100, "y": 200},
            "Health": {"current": 100, "max": 100}
        }
    }
})";

world.from_json(json);

// 导出到JSON
std::cout << world.to_json();
```

---

## 完整项目结构示例

```
MyGame/
├── main.cpp
├── Components/
│   ├── Transform.h
│   ├── Physics.h
│   ├── Render.h
│   └── Gameplay.h
├── Systems/
│   ├── PhysicsSystems.cpp
│   ├── RenderSystems.cpp
│   └── GameplaySystems.cpp
├── Modules/
│   ├── PhysicsModule.h
│   ├── RenderModule.h
│   └── GameModule.h
└── third_party/
    ├── flecs/         # 单头文件或库
    └── raylib/        # 渲染
```

**main.cpp**：
```cpp
#include <flecs.h>
#include <raylib.h>
#include "Modules/PhysicsModule.h"
#include "Modules/RenderModule.h"

int main() {
    flecs::world world;
    
    // 初始化模块
    PhysicsModule::Init(world);
    RenderModule::Init(world);
    
    // 初始化Raylib
    InitWindow(1280, 720, "My Flecs Game");
    SetTargetFPS(60);
    
    // 创建初始实体...
    
    while (!WindowShouldClose()) {
        world.progress(GetFrameTime());
        RenderModule::Render(world);  // 渲染在Raylib循环中
    }
    
    world.cleanup();
    CloseWindow();
    return 0;
}
```

---

## 调试工具

### 1. 内置统计

```cpp
// 打印世界统计
std::cout << world.stats() << "\n";

// 输出：
// entities: 1523
// components: 27
// systems: 15
// queries: 8
```

### 2. Web 浏览器（ECS浏览器）

```cpp
// 启用REST API（需要构建时包含flecs-rest模块）
world.set<flecs::Rest>({});

// 打开浏览器访问 http://localhost:27750
// 可实时查看：实体、组件、System、查询
```

### 3. 日志系统

```cpp
// 启用日志
world.set<flecs::Log>({
    .level = flecs::Log::Debug
});

// System自动记录执行时间
world.system<Position>("Move")
    .interval(1.0f)  // 每帧记录
    .each([](Position& p) {
        // 执行时会输出日志
    });
```

---

## 性能提示

### 1. 批量创建实体

```cpp
// ❌ 坏：逐个创建
for (int i = 0; i < 10000; i++) {
    world.entity().set<Position>({...}).set<Velocity>({...});
}

// ✅ 好：批量创建，减少Archetype切换开销
world.defer([&] {  // 延迟到帧末统一执行
    for (int i = 0; i < 10000; i++) {
        world.entity().set<Position>({...}).set<Velocity>({...});
    }
});
```

### 2. 缓存查询

```cpp
// ❌ 坏：每帧创建查询
void Update() {
    world.query<Position, Velocity>().each(...);
}

// ✅ 好：缓存查询
struct Globals {
    flecs::query<Position, Velocity> moveQuery;
};

void Init(Globals& g) {
    g.moveQuery = world.query<Position, Velocity>();
}

void Update(Globals& g) {
    g.moveQuery.each(...);
}
```

### 3. 使用迭代器而非Each（大量数据）

```cpp
// 10万+实体时，iter比each更快
world.system<Position, Velocity>()
    .iter([](flecs::iter& it, Position* p, Velocity* v) {
        // 批量处理，SIMD友好
        for (auto i : it) {
            p[i].x += v[i].x * it.delta_time();
        }
    });
```

---

## 获取与学习资源

| 资源 | 链接 |
|------|------|
| GitHub | https://github.com/SanderMertens/flecs |
| 文档 | https://www.flecs.dev/flecs/ |
| 示例 | https://github.com/SanderMertens/flecs/tree/master/examples |
| Discord社区 | GitHub页面有链接 |

**推荐学习路径**：
1. 阅读官方 Quickstart
2. 运行示例代码（特别是与raylib/SDL的结合）
3. 尝试实现一个小游戏（如贪吃蛇、弹球）
4. 深入学习 Module 和 Relationship
