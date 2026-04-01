---
title: 守望先锋ECS架构与网络同步
date: 2026-04-01
tags:
  - ECS
  - 网络同步
  - 游戏架构
  - Overwatch
  - GDC
aliases:
  - Overwatch ECS Architecture
---

> **Source**: GDC 2017 - Overwatch Gameplay Architecture and Netcode
> **Speaker**: Tim Ford (Lead Gameplay Programmer)
> **视频**: https://www.bilibili.com/video/BV1p4411k7N8

---

## Why：为什么要学习这个？

### 问题背景

在大型游戏项目中，代码复杂度随规模指数增长：

- **传统OOP组件模型**：类同时包含行为和状态，导致紧耦合
- **复杂交互**：游戏对象之间的关系错综复杂，修改一处可能影响全局
- **网络同步**：动作游戏需要"即时响应"，但服务器权威和延迟之间存在矛盾

### 不用ECS会怎样？

守望先锋团队早期有过惨痛教训：

| 问题 | 后果 |
|------|------|
| 系统间直接调用 | 编译时间剧增，修改一个头文件导致大量重编译 |
| 全局状态访问 | Kill Cam功能需要第二套ECS世界时，全局访问模式完全失效 |
| 行为分散 | 同一逻辑散落在多个系统，难以追踪和维护 |

### 应用场景

- ✅ 需要快速迭代的大型代码库
- ✅ 复杂物理交互的游戏（如英雄射击）
- ✅ 需要客户端预测的网络游戏
- ✅ 多平台确定性模拟需求

---

## What：ECS架构是什么？

### 核心定义

**ECS (Entity Component System)** 是一种严格分离数据和行为的架构：

```
┌─────────────────────────────────────────────────────────────┐
│                        World (Entity Admin)                  │
├─────────────────┬─────────────────┬─────────────────────────┤
│    Systems      │    Entities     │      Components         │
│   (行为/逻辑)    │    (实体ID)      │       (纯数据)           │
├─────────────────┼─────────────────┼─────────────────────────┤
│ • 有函数无字段   │ • 仅是一个32位ID │ • 有字段无函数           │
│ • 遍历组件执行   │ • 关联一组组件   │ • 存储游戏状态           │
│   行为          │                 │ • 无多态（除生命周期）    │
└─────────────────┴─────────────────┴─────────────────────────┘
```

### 关键概念

#### 1. Component（组件）

```cpp
// 组件 = 纯数据结构
struct ConnectionComponent {
    NetworkHandle connectionHandle;
    InputStream inputStream;
    float afkTimer;
    // 只有数据，没有Update()等函数
};
```

#### 2. System（系统）

```cpp
// 系统 = 行为逻辑
class PlayerConnectionSystem : public System {
public:
    void Update(EntityAdmin* admin) {
        // 遍历所有有ConnectionComponent的实体
        for (auto& conn : admin->Iterate<ConnectionComponent>()) {
            // 检查AFK状态
            if (!CheckInput(conn.inputStream) && !CheckStats(conn.stats)) {
                conn.afkTimer += deltaTime;
                if (conn.afkTimer > AFK_THRESHOLD) {
                    KickPlayer(conn.connectionHandle);
                }
            }
        }
    }
};
```

#### 3. Entity（实体）

实体只是一个ID，对应一组组件：

```
Entity 0x1234:
  - TransformComponent
  - HealthComponent  
  - MovementStateComponent
  - ConnectionComponent

Entity 0x5678:
  - TransformComponent
  - PhysicsComponent
  (没有Health，不是可伤害对象)
```

### 守望先锋的实现

| 指标 | 数值 |
|------|------|
| 客户端系统数 | 46个 |
| 客户端组件类型 | 103种 |
| 服务器系统数 | （更多，包含权威模拟） |
| Singleton组件占比 | ~40% |

### 核心优势：观察者模式类比

Tim Ford用"樱花树"类比ECS：

> 同一棵树，对不同观察者意味着不同东西：
> - **房主**：美景
> - **物业**：落叶清理
> - **园丁**：修剪工作  
> - **白蚁**：食物来源
>
> 树（Entity+Components）是纯粹的**状态**，不同系统（观察者）按需取用。

```
┌────────────────────────────────────────────────────────────┐
│                    Connection Component                     │
│                    (玩家连接组件)                           │
└────────────────────────────────────────────────────────────┘
         │              │              │              │
         ▼              ▼              ▼              ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ PlayerConn   │ │ Connection   │ │ UX Game      │ │ ...          │
│ System       │ │ Util         │ │ System       │ │              │
│              │ │              │ │              │ │              │
│ AFK检测      │ │ 网络消息广播  │ │ 计分板显示    │ │              │
└──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘
```

---

## How：如何实践ECS？

### 模式1：Singleton组件（单例组件）

#### 问题

传统ECS规则"Systems have no state"在实践中遇到挑战：

```cpp
// ❌ 错误做法：System存储状态
class InputSystem {
    InputState m_state;  // 全局输入状态
public:
    InputState* GetState() { return &m_state; }  // 全局访问点
};

// 其他系统需要这样访问：
auto input = g_InputSystem->GetState();  // 编译耦合！
```

问题：
1. **编译耦合**：修改InputSystem头文件导致所有依赖系统重编译
2. **Kill Cam灾难**：需要第二套ECS世界时，全局访问完全失效

#### 解决方案

```cpp
// ✅ 正确做法：Singleton Component
struct SingletonInput {
    ButtonState buttons[MAX_BUTTONS];
    float axisValues[MAX_AXES];
};

// 存储在匿名实体上，通过EntityAdmin访问
class InputSystem : public System {
public:
    void Update(EntityAdmin* admin) {
        auto* input = admin->GetSingleton<SingletonInput>();
        // 读取OS输入填充input...
    }
};

// 其他系统：
class CommandSystem : public System {
    void Update(EntityAdmin* admin) {
        auto* input = admin->GetSingleton<SingletonInput>();
        // 读取输入构建命令...
    }
};
```

#### 关键洞察

> "几乎每一个我们创建的Singleton，最终都有多个系统需要访问它"

这提前解决了系统间的隐式耦合。

### 模式2：Utility函数（共享行为）

#### 原则

| 调用点数量 | 组件读取数 | 副作用 | 示例 |
|-----------|-----------|--------|------|
| 多 | 少 | 无/少 | ✅ `IsHostileTo()` - 读取3个组件，纯函数 |
| 少 | 多 | 有 | ✅ `CharacterMove()` - 2个调用点，复杂移动逻辑 |
| 多 | 多 | 有 | ❌ 避免！隐藏复杂副作用 |

#### 实战示例：敌对检测

```cpp
// 纯函数，多个系统调用
bool CombatUtil::IsHostileTo(EntityAdmin* admin, Entity a, Entity b) {
    auto* filterA = admin->GetComponent<FilterBitsComponent>(a);
    auto* filterB = admin->GetComponent<FilterBitsComponent>(b);
    auto* petMasterA = admin->GetComponent<PetMasterComponent>(a);
    auto* petMasterB = admin->GetComponent<PetMasterComponent>(b);
    
    // 无队伍 = 不敌对（两扇门不会互打）
    if (!filterA || !filterB) return false;
    
    // 同队伍 = 不敌对
    if (filterA->teamIndex == filterB->teamIndex) return false;
    
    // 检查宠物主人关系（避免托比昂炮台攻击自己）
    if (petMasterA && petMasterB && 
        petMasterA->uniqueKey == petMasterB->uniqueKey) {
        return false;
    }
    
    return true;
}
```

### 模式3：Deferment（延迟执行）

#### 核心原则

> **如果一个大副作用必须被执行，问自己：必须现在执行吗？**

#### 问题场景：特效生成

多个调用点都想创建击中特效：

```cpp
// ❌ 错误：到处创建特效
void ProjectileSystem::Update() {
    if (hit) {
        SpawnImpactEffect(hitInfo);  // 大副作用！
    }
}

void BeamWeaponSystem::Update() {
    if (contact) {
        SpawnImpactEffect(contactInfo);  // 重复逻辑
    }
}
```

问题：
- 创建实体是重量级操作（生命周期、场景管理、资源加载）
- LOD规则、Z-fighting处理逻辑重复
- 调用点越多，认知负担越重

#### 解决方案

```cpp
// 1. 定义延迟记录结构
struct PendingContact {
    Vector3 position;
    SurfaceType surface;
    ImpactType impactType;
    MaterialID material;
};

// 2. Singleton存储待处理列表
struct SingletonContact {
    Array<PendingContact> pendingContacts;
};

// 3. 各系统只添加记录
void ProjectileSystem::Update(EntityAdmin* admin) {
    auto* contacts = admin->GetSingleton<SingletonContact>();
    if (hit) {
        contacts->pendingContacts.push_back({hitPos, surface, ...});
    }
}

// 4. 单一系统统一处理
class ResolveContactSystem : public System {
public:
    void Update(EntityAdmin* admin) {
        auto* contacts = admin->GetSingleton<SingletonContact>();
        
        // 统一处理所有特效创建
        for (auto& pending : contacts->pendingContacts) {
            // LOD规则
            // 优先级排序
            // Z-fighting处理（弹孔覆盖逻辑）
            // 多线程Fork-Join
            SpawnImpactEffect(pending);
        }
        contacts->pendingContacts.clear();
    }
};
```

#### 延迟执行的优势

1. **降低复杂度**：大副作用只在单一调用点
2. **性能优化**：数据局部性更好，可批量处理
3. **性能预算**：可控制每帧特效数量，平滑峰值
4. **多线程友好**：可Fork-Join并行处理

---

## 网络同步（Netcode）

### 核心挑战

| 需求 | 约束 |
|------|------|
| 即时响应 | 必须等待服务器确认 |
| 客户端预测 | 不能信任客户端计算 |
| 反作弊 | 服务器必须权威 |

### 关键技术

#### 1. 确定性模拟基础

```
┌──────────────────────────────────────────────────────────────┐
│                     确定性三要素                              │
├───────────────┬───────────────┬──────────────────────────────┤
│ 同步时钟       │ 固定时间步     │ 数值量化                      │
├───────────────┼───────────────┼──────────────────────────────┤
│ Client和Server│ Command Frame │ 位置分辨率: 1m/1024          │
│ 共享同一时钟   │ 16ms (60Hz)   │ 时间、速度全部量化            │
│               │ Tournament: 7ms│ 避免浮点差异                 │
└───────────────┴───────────────┴──────────────────────────────┘
```

#### 2. 客户端领先策略

```
时间轴:
─────────────────────────────────────────────────────────────►

Server:     |←──── 1/2 RTT ────→|←──── 1/2 RTT ────→|
            [Sim Frame 10]       [Sim Frame 20]       [Sim Frame 30]
            ▲                                         
            │                                          
Client:     │←── Buffer ──→|←──── 1/2 RTT ────→|      
[Sim Frame 19]                                   [Render]
            ▲
            客户端始终领先 Server: 1/2 RTT + 1 Buffer Frame
```

#### 3. 预测与回滚

```cpp
// 客户端维护两个环形缓冲区
class MovementPrediction {
    RingBuffer<MovementState> stateHistory;  // 历史状态
    RingBuffer<PlayerInput> inputHistory;    // 历史输入
    
public:
    void OnServerCorrection(MovementState serverState, FrameNumber frame) {
        // 1. 回滚到服务器确认的状态
        stateHistory[frame] = serverState;
        
        // 2. 重播所有后续输入
        for (FrameNumber f = frame; f < currentFrame; ++f) {
            SimulateTick(stateHistory[f], inputHistory[f]);
        }
        
        // 3. 现在与服务器同步了
    }
};
```

#### 4. 时间膨胀（Time Dilation）

应对丢包的自适应算法：

```
正常情况:    ├─16ms─┼─16ms─┼─16ms─┼─16ms─┤  (匀速)

检测到丢包:  ├─15ms─┼─15ms─┼─15ms─┼─15ms─┤  (加速模拟)
             ↑ 客户端加快模拟速度
             
恢复后:      ├─17ms─┼─17ms─┼─17ms─┼─17ms─┤  (减速回到正常)
             ↑ 放慢以减小Buffer

目标: 保持Buffer在"剃刀边缘"，既防丢包又最小化延迟
```

#### 5. 滑动窗口输入

```cpp
// 不是只发当前帧输入
struct InputPacket {
    FrameNumber lastAckedFrame;  // 上次确认帧
    PlayerInput inputs[WINDOW_SIZE];  // 从lastAcked到现在的所有输入
};

// 优势：丢包后下一包自动补全历史输入
```

### 命中判定（Hit Registration）

#### ECS组件组合

```
命中判定需要的组件组合:

┌─────────────────────────────────────────────────────────────┐
│ 远程玩家 (可被命中)                                          │
│   - MovementStateComponent (用于Rewind)                      │
│   - HostileComponent         (阵营检测)                       │
│   - ModifiedHealthQueue      (伤害队列)                      │
├─────────────────────────────────────────────────────────────┤
│ 本地玩家 (射击者)                                            │
│   - ConnectionComponent      (识别为玩家)                     │
│   - WeaponComponent          (射击逻辑)                      │
└─────────────────────────────────────────────────────────────┘
```

#### Rewind流程

```
1. 客户端射击时，记录当前Command Frame
2. 服务器收到射击请求，Rewind所有玩家到对应Frame
3. 在那个时间点做射线检测
4. 确认命中后，将伤害加入ModifiedHealthQueue
5. 伤害在帧末统一处理（Deferment）
```

#### 高延迟处理

| Ping | 策略 |
|------|------|
| < 220ms | 预测命中，即时反馈 |
| > 220ms | 延迟命中反馈，使用外插(Extrapolation) |

```
低延迟 (0ms):   预测命中 → 立即显示命中效果

高延迟 (300ms): 外插目标位置 → 等待服务器确认 → 显示效果
                (避免受害者感觉被"拉回"墙后中弹)
```

### 技能系统（State Script）

守望先锋使用声明式脚本语言State Script：

```
技能特性：
- 可时间轴前后擦洗（Scrubbable）
- 客户端预测执行
- 服务器验证
- 预测错误时回滚重播
```

```
死神幽灵形态示例:

Frame X:   预测退出幽灵形态 → 播放动画、移除无敌
           ↓
收到服务端: 发现实际还有0.1秒才结束
           ↓
回滚:      恢复幽灵形态状态
           重播输入 → 再次退出
           ↓
最终:      与服务器同步
```

---

## 经验与教训

### 1. 系统并行化

```
// 使用Tuple明确声明组件访问
class MovementSystem {
    // 声明：我只读Input，写Transform
    using Tuple = std::tuple<
        ReadOnly<InputComponent>,
        Write<TransformComponent>
    >;
};

// 不冲突的系统可并行执行
// System A: 读A写B
// System B: 读C写D  → 可并行
```

### 2. Entity生命周期

```
创建: 延迟到帧末创建（避免迭代器中插入）
销毁: 立即销毁（标记为Dead，跳过处理）
```

### 3. 遗留系统集成

```
┌─────────────────────────────────────────────────────────────┐
│                     "冰山"组件                              │
├─────────────────────────────────────────────────────────────┤
│  水面之上：Component接口（ECS可见）                          │
│  水面之下：复杂内部状态（遗留系统管理）                       │
├─────────────────────────────────────────────────────────────┤
│  示例：AI路径系统                                            │
│  - 只有Proxy Component暴露给ECS                             │
│  - 实际路径计算在独立线程                                    │
│  - ECS仅读取最终路径点                                       │
└─────────────────────────────────────────────────────────────┘
```

### 4. 不要过度设计

> "我们从简化的决心中学到的，比从任何复杂方案中学到的都多"

- 火箭预测：行业说"不要这么做"，但他们做了，尽管有Bug但值得
- 伤害归属：最终决定不存储在投射物中（简化优先于极端边界情况）

---

## 总结

### ECS核心收益

1. **解耦**：行为按"主观观察"分离，而非按对象类型
2. **可维护**：所有状态修改集中在单一调用点
3. **可测试**：纯函数Utility易于单元测试
4. **可扩展**：新增系统不影响现有系统

### 网络同步核心洞见

1. **客户端预测** + **服务器权威** + **确定性回滚** = 即时响应
2. **时间膨胀** 自适应网络波动
3. **ECS简化Netcode**：只有拥有特定组件组合的对象才参与网络逻辑

### 一句话总结

> "ECS把我们从坑里扔进了成功的坑"（Pit of Success）

约束迫使你用特定方式解决问题，结果是简单、一致、可维护的代码。

---

## 参考资源

- [[ECS架构模式]]
- [[客户端预测与回滚]]
- [[游戏网络同步基础]]
- [[确定性模拟技术]]
