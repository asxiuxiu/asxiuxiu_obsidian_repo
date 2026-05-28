---
title: Plugin 与模块系统
date: 2026-05-28
tags:
  - self-game-engine
  - ecs
  - plugin
  - module
  - lifecycle
aliases:
  - Plugin and Module System
---

# Plugin 与模块系统

> **前置依赖**：[[反射系统]]、[[系统调度与确定性]]、[[Prefab与数据层]]
> **本模块增量**：为 ECS 引擎建立一套声明式、分阶段、可扩展的模块组装机制。新功能不再需要修改核心 `main()`，而是通过 Plugin 注册组件、系统、资源和事件。引擎首次具备"第三方扩展能力"的骨架。
> **下一步**：[[RHI抽象层]]，因为模块系统就位后，我们需要把渲染能力也封装为 Plugin（GraphicsPlugin），让图形后端可替换、可扩展。

---

## 问题 0：为什么 ECS 引擎还需要 Plugin？

在 [[系统调度与确定性]] 中，我们已经让多个 System 能按正确顺序并行执行；在 [[反射系统]] 中，任何新增组件都能自动出现在 Inspector 里。但这里藏着一个沉默的瓶颈——**所有这些能力，都是谁在启动时把它们组装进世界的？**

### 场景绑定：main() 的噩梦

假设你有一个最小 ECS 引擎，没有 Plugin 系统。你的 `main.cpp` 会长成这样：

```cpp
int main() {
    World world;
    
    // 1. 注册所有组件类型（否则反射系统不认识）
    world.register_component<Transform>();
    world.register_component<Mesh>();
    world.register_component<Material>();
    world.register_component<RigidBody>();
    world.register_component<Collider>();
    world.register_component<Health>();
    // ... 新增 10 个组件，这里就要加 10 行
    
    // 2. 插入全局 Resource
    world.insert_resource(Time{});
    world.insert_resource(InputState{});
    world.insert_resource(AssetManager{});
    world.insert_resource(PhysicsConfig{});
    
    // 3. 把 System 注册到 Schedule 的正确阶段
    auto& schedule = world.get_schedule();
    schedule.add_system(PreUpdate, input_system);
    schedule.add_system(Update, movement_system);
    schedule.add_system(Update, collision_system);
    schedule.add_system(PostUpdate, transform_sync_system);
    schedule.add_system(PostUpdate, render_system);
    // ... 新增一个模块，这里又要加 N 行
    
    // 4. 初始化各种子系统
    Renderer::init();
    PhysicsWorld::init();
    
    // 5. 加载初始场景
    load_scene("startup.scene", world);
    
    world.run();
}
```

这还没写 20% 的功能，`main()` 就已经 50 行了。更糟的是：**每新增一个功能模块，你都要回来改 `main.cpp`。** 物理团队写完碰撞检测，需要你在 main 里加 `register_component<Collider>()`；UI 团队做完按钮系统，需要你在 main 里加 `register_system(Update, button_system)`。`main.cpp` 成了整个引擎的**集中式瓶颈**——它本应该是一个简洁的入口，却慢慢膨胀成一本"所有人的修改日志"。

### 根因分析：ECS 解耦了数据与逻辑，但没解耦"组装"

ECS 的核心洞察是"数据（Component）与逻辑（System）分离"。这个洞察解决的是**运行时迭代**问题——System 批量处理同类 Component，cache friendly。但它没有回答一个更前置的问题：**在程序启动的刹那，这些数据类型和逻辑函数，是怎么被知道、被注册、被排序、被初始化的？**

换句话说，ECS 解决的是"零件怎么运转"，但 Plugin 系统解决的是"零件怎么被安装到机器上"。

### 深层矛盾：集中控制 vs 分散扩展

如果不做 Plugin 系统，所有注册代码集中在 `main()`，好处是**一目了然**——你可以单步调试看到所有 System 的注册顺序。代价是**扩展性为零**，新增模块必须侵入核心入口。

如果做 Plugin 系统，注册代码分散到各个模块内部，好处是**新增功能零侵入**——物理团队只改物理目录下的文件。代价是**启动顺序变成黑箱**——你不知道哪个 Plugin 在什么时机注册了什么，调试启动问题变得更困难。

这就是 Plugin 系统的核心矛盾：**你想让模块独立扩展，就必须接受对启动流程的间接控制；你想对启动流程绝对掌控，就必须让所有模块向你汇报。**

后续所有设计——契约形态、生命周期阶段、依赖表达方式——都是在为这个矛盾寻找工程上的平衡点。

---

## 问题 1：Plugin 的契约边界该画在哪里？

确定了"需要 Plugin"之后，第一个具体问题是：**Plugin 作为一个代码单元，向引擎暴露什么、隐藏什么？**

### 场景绑定：暴露太多 vs 暴露太少

假设你设计了一个最 naive 的 Plugin 接口：

```cpp
class IPlugin {
public:
    virtual void initialize(World& world) = 0;
};
```

Plugin 实现者拿到 `World&` 的完全访问权，可以任意注册组件、系统、资源，甚至直接遍历和修改实体。这太自由了——一个恶意或 bug 的 Plugin 可能把反射注册表搞乱，或者把关键 Resource 覆盖掉。但反过来，如果你把接口收得太紧，只给 `register_system()` 一个方法，那 Plugin 就无法注册自己定义的组件类型，也无法初始化自己需要的全局配置。

### 分支 A：OOP 接口继承（UE / chaos 路线）

**核心思路**：定义一个基类接口，Plugin 通过继承实现多态。引擎通过虚函数调用 Plugin 的生命周期方法。

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual void on_load() = 0;
    virtual void on_initialize(World& world) = 0;
    virtual void on_shutdown() = 0;
    virtual const char* name() const = 0;
    virtual std::vector<std::string> dependencies() const { return {}; }
};

// 动态加载时
using CreatePluginFn = IPlugin* (*)();
auto create = lib.get_symbol<CreatePluginFn>("CreatePlugin");
IPlugin* plugin = create();
plugin->on_initialize(world);
```

**适用场景**：需要支持 C++ DLL 动态加载的工业级引擎。虚函数表是 C++ 二进制接口（ABI）的通用语言，不同编译器版本的 DLL 只要能导出 C 函数符号，就能被主程序加载。

**隐藏代价**：
1. **虚函数开销**：虽然启动期调用次数不多，但接口的每个方法都是虚函数，无法内联。
2. **继承耦合**：Plugin 作者必须包含你的头文件、继承你的基类。在 C++ 中，这意味着对引擎头文件的编译依赖。
3. **状态隐藏在 Plugin 对象内部**：`IPlugin* plugin` 是一个有内部状态的对象，它可能持有自己的内存。如果 Plugin 被动态卸载，这些状态的清理责任容易模糊。

**失效条件**：当你想把 Plugin 机制暴露给脚本层（Lua/Python）时，C++ 继承模型完全不适用，必须额外写一层绑定胶。

### 分支 B：纯函数/Trait 契约（Bevy 路线）

**核心思路**：Plugin 不是"对象"，而是一组"行为"。在 C++ 中可以表达为纯虚接口（类似 Rust trait），但核心洞察是——**Plugin 本身不应该有运行时状态**，它只是一张"启动期配置清单"。

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    // 核心：Plugin 只负责"描述"要注册什么，不负责"持有"运行时状态
    virtual void build(App& app) = 0;
    virtual bool ready(const App& app) const { return true; }
    virtual void finish(App& app) {}
    virtual void cleanup(App& app) {}
    virtual std::string name() const = 0;
};

// 一个具体的 Plugin：只声明"我要往世界里加什么"
class PhysicsPlugin : public IPlugin {
public:
    void build(App& app) override {
        app.register_component<RigidBody>();
        app.register_component<Collider>();
        app.insert_resource(Gravity{Vec3{0, -9.8, 0}});
        app.add_system(FixedUpdate, physics_step_system);
        app.add_system(FixedUpdate, collision_detection_system);
    }
    std::string name() const override { return "Physics"; }
};
```

注意这里的微妙但关键的区别：`PhysicsPlugin` **没有成员变量**。`Gravity` 是一个 Resource，被插入到 `World` 里，由 ECS 统一管理生命周期。Plugin 对象本身在 `build()` 结束后就可以被丢弃（或保留在注册表里仅作审计用途）。

**适用场景**：ECS 原生引擎。因为 ECS 本身的设计哲学就是"状态全在 World 里"，Plugin 如果也持有状态，就破坏了这一原则。

**隐藏代价**：
1. **需要外部管理 Plugin 的存储**：虽然 Plugin 本身无状态，但 `std::unique_ptr<IPlugin>` 仍然需要被某个注册表持有。
2. **生命周期回调的语义变弱**：`cleanup()` 只能清理 World 中的数据，无法清理 Plugin 自身的状态（因为本来就没有）。

**失效条件**：如果你的引擎不是纯 ECS（比如有些全局子系统是传统的 OOP 单例），那么"Plugin 无状态"的约束会让这些子系统的初始化变得很别扭。

### 分支 C：声明式配置驱动（UE `.uplugin` / Cargo feature 路线）

**核心思路**：Plugin 的能力不写在 C++ 代码里，而是写在一个外部描述文件（JSON/TOML）中。引擎先解析描述文件，知道"这个 Plugin 依赖谁、暴露什么模块、加载时机是什么"，再决定加载策略。

```json
{
    "name": "PhysicsExtension",
    "version": "1.0.0",
    "modules": [
        {
            "name": "PhysicsExtension",
            "type": "Runtime",
            "loading_phase": "PreDefault"
        }
    ],
    "dependencies": [
        { "name": "Core", "version": "^1.0" },
        { "name": "ECSRuntime", "version": "^1.0" }
    ]
}
```

**适用场景**：大型团队、复杂依赖图、需要工具链（编辑器）自动分析插件兼容性的场景。UE 的 `.uplugin` 让编辑器可以在不加载 DLL 的情况下，判断"这个插件是否兼容当前引擎版本"。

**隐藏代价**：
1. **双真相问题**：声明文件说"我依赖 Core"，但 C++ 代码里可能实际依赖了别的模块。两者不同步时，编译能通过，运行时崩溃。
2. **表达能力有限**：JSON 无法描述"只有在 Windows 下才注册这个 System"这种条件逻辑，最终还是要回退到 C++ 代码。
3. **解析成本**：启动时需要先读文件、解析 JSON，再执行加载。对启动速度敏感的场景不利。

**失效条件**：模块数量 < 10 的默认实现中，维护独立 JSON 文件的工程负担可能超过收益，推荐将元数据内嵌到 C++ 代码中。

### 引擎对照

| 维度 | chaos | UE | Bevy |
|------|-------|-----|------|
| 契约形态 | `BasicModule` 基类 + 虚函数 | `IPlugin` 接口 + `.uplugin` JSON | `Plugin` trait（无状态） |
| 配置驱动 | 弱（C++ 宏 `CHAOS_MODULE`） | 强（`.uplugin` + `.Build.cs`） | 弱（`Cargo.toml` feature 控制编译） |
| 状态归属 | Plugin 对象可持有状态 | Plugin 对象可持有状态 | Plugin 无状态，全在 World |

**默认推荐**：**分支 B（纯函数/Trait 契约）作为核心 Plugin 接口，辅以最小化的声明式元数据（一个 C++ 结构体或可选 JSON）表达依赖关系。**

**推荐依据**：
- 我们的引擎是 ECS 原生架构，"状态全在 World"是核心不变式。Bevy 的 `Plugin` trait 设计与此完全一致。
- UE 的 `.uplugin` JSON 在工业级团队中有价值，但对我们当前阶段是过度设计。我们提取其核心洞察——"依赖关系需要显式声明"——但用 C++ 代码表达（在 `IPlugin::dependencies()` 中返回字符串列表），而非独立文件。
- chaos 的 `BasicModule` 设计接近分支 A，但其可持有状态的特性与 ECS 原则冲突。我们吸收其"分阶段生命周期"思想，但将状态外移到 World Resource。

**简化路径**：如果当前阶段不想引入虚函数开销，可以把 `IPlugin` 退化为一个函数指针 + 元数据结构体：

```cpp
struct PluginEntry {
    const char* name;
    std::vector<const char*> dependencies;
    void (*build)(App&);
};
```

这是分支 B 的最小可行子集，未来补全虚函数接口以支持 `ready()` / `finish()` / `cleanup()` 的细粒度生命周期。

---

## 问题 2：生命周期该分几阶段？

Plugin 的契约确定了"它暴露什么行为"，接下来要回答：**这些行为按什么顺序调用？**

### 场景绑定：顺序错了，世界就崩了

假设你有一个 `GraphicsPlugin` 和一个 `PhysicsPlugin`。`GraphicsPlugin` 在 `build()` 中创建窗口并插入一个 `WindowResource`；`PhysicsPlugin` 在 `build()` 中注册一个 `DebugRenderSystem`，这个 System 每帧读取 `WindowResource` 获取 swap chain 句柄来画调试线。

如果 `PhysicsPlugin::build()` 先执行，`WindowResource` 还不存在，`DebugRenderSystem` 注册时检查资源失败——或者更糟，它注册成功了，但第一帧运行到 `window = world.get_resource<WindowResource>()` 时返回空指针，崩溃。

这不是代码 bug，而是**初始化时序 bug**。两个 Plugin 本身都是正确的，但它们的组合顺序错了。

### 根因分析：隐式数据依赖

`PhysicsPlugin` 并不"依赖" `GraphicsPlugin` 的代码——它不调用 GraphicsPlugin 的任何函数。它依赖的是 **GraphicsPlugin 在 World 中留下的数据**（`WindowResource`）。这种依赖是**隐式的、数据层面的**，不是显式的函数调用图能捕捉的。

在运行时，ECS 的 System 依赖分析可以捕获"A System 读 X，B System 写 X"这种冲突。但在启动期，Plugin 之间还没有 System 在运行，只有"往 World 里放东西"的操作。我们需要一种启动期的依赖表达机制。

### 分支 A：二阶段（build / run）

**核心思路**：Plugin 只有一个 `build()` 方法，在引擎启动时调用一次。之后引擎进入 `run()` 循环，不再区分 Plugin 的初始化阶段。

```cpp
for (auto& plugin : plugins) {
    plugin->build(app);
}
app.run();
```

**适用场景**：模块数量少、无异步初始化、无复杂依赖图的最小引擎。

**隐藏代价**：
1. **隐式时序依赖完全暴露给用户**：用户必须自己保证 `add_plugin(GraphicsPlugin)` 在 `add_plugin(PhysicsPlugin)` 之前调用。一旦顺序错了，调试困难。
2. **无法表达异步初始化**：如果某个 Plugin 需要异步加载配置（从网络或慢速磁盘），`build()` 会被阻塞，或者引擎必须先跑起来再"半初始化"。

**失效条件**：当 Plugin 数量 > 5 或存在跨模块数据依赖时，手工维护顺序不可持续。

### 分支 B：四阶段（build → ready → finish → cleanup）

**核心思路**：Bevy 的方案。`build()` 做主要注册；`ready()` 检查依赖是否满足（返回 bool）；`finish()` 在所有 Plugin 的 `build()` 完成后执行，适合做跨 Plugin 的二次配置；`cleanup()` 在 App 退出时执行。

```cpp
class IPlugin {
public:
    virtual void build(App& app) = 0;
    virtual bool ready(const App& app) const { return true; }
    virtual void finish(App& app) {}
    virtual void cleanup(App& app) {}
};

// 引擎启动流程
for (auto& plugin : plugins) plugin->build(app);

// 等待所有 ready()
bool all_ready = false;
while (!all_ready) {
    all_ready = true;
    for (auto& plugin : plugins) {
        if (!plugin->ready(app)) all_ready = false;
    }
}

for (auto& plugin : plugins) plugin->finish(app);
```

**关键洞察**：`finish()` 解决了一个非常具体但高频的问题——Plugin A 想注册一个 System，这个 System 查询的组件是由 Plugin B 注册的。如果 A 的 `build()` 在 B 的 `build()` 之前执行，A 注册 System 时组件类型还不存在。`finish()` 给了 A 一个"等所有人都注册完组件后，再注册 System"的机会。

**适用场景**：模块间存在注册依赖（A 依赖 B 注册的类型），或有异步初始化需求（如着色器异步编译）。

**隐藏代价**：
1. `ready()` 的轮询在启动期引入忙等循环，如果某个 Plugin 的 `ready()` 永远返回 false，引擎会死循环。
2. 用户容易混淆 `build()` 和 `finish()` 的语义边界——"我到底该把代码写在哪里？"

**失效条件**：如果所有初始化都是同步的、无跨 Plugin 类型依赖，四阶段是过度设计。

### 分支 C：N 阶段（UE LoadingPhase 路线）

**核心思路**：不是给每个 Plugin 一套生命周期回调，而是给引擎定义一套全局的"加载阶段"（LoadingPhase）。每个 Plugin（或模块）声明自己属于哪个阶段。引擎按阶段顺序执行，同一阶段内的模块可以并行加载。

```cpp
enum class LoadingPhase {
    Earliest,      // 日志、内存分配器
    PreInitialize, // 类型系统、反射注册表
    Initialize,    // 核心子系统：ECS World、Schedule
    PostInitialize,// 游戏逻辑模块：物理、渲染、AI
    Runtime        // 运行时动态加载的 Mod
};

struct PluginDescriptor {
    const char* name;
    LoadingPhase phase;
    std::vector<const char*> dependencies;
};

// 引擎按阶段分组，组内拓扑排序
for (auto phase : {Earliest, PreInitialize, Initialize, PostInitialize}) {
    auto group = collect_plugins_in_phase(phase);
    auto sorted = topological_sort(group); // 按 dependencies 排序
    for (auto& plugin : sorted) plugin->build(app);
}
```

UE 的实际阶段更多：`PreEarlyLoadingScreen → EarlyLoadingScreen → PreLoadingScreen → PreDefault → Default → PostDefault → PostEngineInit`。chaos 也采用了类似的分层：`Earliest → PreInitialize → Initialize → PostInitialize`。

**适用场景**：超大规模引擎，模块数量 > 50，启动流程需要严格控制（如先显示加载画面再初始化渲染）。

**隐藏代价**：
1. **阶段爆炸**：随着引擎发展，阶段会越来越多。UE 的 7 个阶段已经让开发者困惑——"我的模块到底该放 PostDefault 还是 PostEngineInit？"
2. **阶段之间的顺序是硬编码的**：你无法在不修改引擎核心的情况下插入一个新阶段。
3. **同一阶段内的模块仍然可能有隐式依赖**：阶段只保证了"A 阶段在 B 阶段之前"，不保证同一阶段内的顺序。

**失效条件**：模块数量 < 20 时，N 阶段的维护负担超过收益。

### 决策分析

| 条件 | 推荐方案 |
|------|---------|
| 模块 < 5，无复杂依赖 | 分支 A（二阶段），手工保证顺序 |
| 模块 5~30，存在跨模块类型依赖，全同步初始化 | 分支 B（四阶段），`finish()` 解决注册依赖 |
| 模块 > 30，需要加载画面，有异步初始化 | 分支 C（N 阶段），工业级控制 |

**默认推荐**：**分支 B（四阶段）作为基础，当引擎规模增长时平滑升级到分支 C 的"阶段"概念。**

**推荐依据**：
- 四阶段的 `build/ready/finish/cleanup` 足够解决我们当前会遇到的所有启动时序问题。
- `finish()` 的"等所有人注册完再做二次配置"是 ECS 插件系统的核心机制——UE 虽然没有 `finish()` 这个具体函数，但其 `PostEngineInit` 阶段的本质就是"等引擎核心全初始化完再做游戏层配置"。我们在 ECS 中以显式回调表达这一设计意图。
- Bevy 的 `ready()` 支持异步初始化（如渲染后端等待 GPU 就绪），这对未来支持 WebGPU / Vulkan 异步管线编译很重要。
- 我们吸收 UE 的"阶段"思想，但不硬编码 7 个阶段，而是允许 Plugin 声明 `LoadingPhase` 枚举。当未来需要更细粒度时，只需扩展枚举值，无需改 Plugin 接口。

**在 ECS 中的具体表达**：

```cpp
enum class LoadingPhase {
    Core,       // 注册组件类型、反射类型
    Systems,    // 注册 System 到 Schedule
    Services,   // 初始化需要组件和系统都已就位的外部服务（渲染器、物理世界）
    Gameplay    // 加载场景、生成初始实体
};

class IPlugin {
public:
    virtual LoadingPhase phase() const { return LoadingPhase::Systems; }
    virtual void build(App& app) = 0;
    virtual bool ready(const App& app) const { return true; }
    virtual void finish(App& app) {}
};
```

---

## 问题 3：模块依赖如何表达与解析？

生命周期阶段解决了"宏观时序"问题，但同一阶段内的 Plugin 仍然需要精细的先后顺序。这就是依赖管理的问题。

### 场景绑定：循环依赖与启动崩溃

假设 `NetworkPlugin` 依赖 `SerializationPlugin`（网络消息需要序列化），`SerializationPlugin` 又依赖 `ReflectionPlugin`（序列化需要类型信息），而某个新手程序员不小心让 `ReflectionPlugin` 依赖了 `NetworkPlugin`（想在反射系统里通过网络同步类型定义）。这就是一个循环依赖。

如果没有显式依赖检查，引擎启动时会按某种默认顺序加载 Plugin，`ReflectionPlugin` 可能在 `NetworkPlugin` 之前执行，导致 `NetworkPlugin` 需要的序列化器还没注册，崩溃。

### 分支 A：显式声明依赖（拓扑排序）

**核心思路**：每个 Plugin 显式声明它依赖哪些其他 Plugin。引擎在启动时构建依赖图，做拓扑排序，检测循环依赖。

```cpp
class PhysicsPlugin : public IPlugin {
public:
    std::vector<const char*> dependencies() const override {
        return {"Transform", "Time"}; // 依赖 TransformPlugin 和 TimePlugin
    }
    void build(App& app) override {
        app.register_component<RigidBody>();
        app.add_system(FixedUpdate, physics_step);
    }
};

// 引擎启动时
bool App::add_plugins(std::vector<std::unique_ptr<IPlugin>> plugins) {
    // 1. 构建依赖图
    DirectedGraph graph;
    for (auto& p : plugins) {
        graph.add_node(p->name());
        for (auto& dep : p->dependencies()) {
            graph.add_edge(p->name(), dep); // PhysicsPlugin 依赖 dep
        }
    }
    
    // 2. 检测循环依赖
    if (auto cycle = graph.find_cycle()) {
        log_error("Plugin dependency cycle detected: {}", *cycle);
        return false;
    }
    
    // 3. 拓扑排序
    auto sorted = graph.topological_sort();
    // 按 sorted 顺序执行 build()
}
```

**适用场景**：模块数量中等（10~100），依赖关系相对稳定。

**隐藏代价**：
1. **字符串比较开销**：依赖声明用 Plugin 名称字符串，启动期需要字符串匹配。对启动速度极端敏感的场景可优化为 ID 或哈希。
2. **版本兼容盲区**：声明了"依赖 SerializationPlugin"，但没说明依赖哪个版本。未来 SerializationPlugin 改了接口，依赖方可能编译通过但行为异常。
3. **过度声明**：开发者可能为了"保险"而声明过多依赖，导致依赖图过于稠密，并行加载机会减少。

**失效条件**：如果模块间依赖关系变化极快（如快速原型期每周重构），维护依赖声明会成为负担。

### 分支 B：隐式探测（运行时检查）

**核心思路**：Plugin 不声明依赖，而是在 `build()` 或 `ready()` 中检查 World 里是否已有自己需要的东西。如果没有，报错或等待。

```cpp
void PhysicsPlugin::build(App& app) {
    if (!app.world().has_component_registered<Transform>()) {
        panic("PhysicsPlugin requires Transform component. Did you forget to add TransformPlugin?");
    }
    app.register_component<RigidBody>();
}
```

**适用场景**：模块数量少、团队规模小、想减少显式声明的维护成本。

**隐藏代价**：
1. **失败晚**：编译期不检查依赖，启动时才报错。如果某个依赖的缺失只在特定代码路径触发（如只在打开调试绘制时才需要 `LineRenderer`），可能测试阶段发现不了。
2. **错误信息质量低**：`panic("Transform component not registered")` 对新手指引不足——它不知道是哪个 Plugin 负责注册 Transform。

**失效条件**：模块数量 > 10 时，隐式探测的调试成本会超过显式声明的维护成本。

### 分支 C：延迟绑定（Command 缓冲 + finish 阶段）

**核心思路**：不在 `build()` 中立即执行所有注册，而是把操作记录到 Command 缓冲区，等依赖满足后再真正执行。这是 ECS "延迟执行"思想在启动期的延伸。

```cpp
class PhysicsPlugin : public IPlugin {
public:
    void build(App& app) override {
        // 不直接注册，而是发"命令"
        app.commands().register_component<RigidBody>();
        app.commands().add_system(FixedUpdate, physics_step);
    }
    
    void finish(App& app) override {
        // 在 finish 阶段，所有 Plugin 的 build() 都执行完了
        // Command 缓冲区在这里统一 flush，失败时已知所有上下文
        app.commands().flush();
    }
};
```

**适用场景**：模块间存在大量"注册时不知道目标是否存在"的交叉依赖。

**隐藏代价**：
1. **调试困难**：注册操作的实际执行点从 `build()` 转移到了 `finish()`，堆栈跟踪变深。
2. **错误延迟**：如果一个 Plugin 的注册操作非法（如注册同名组件），错误要到 `finish()` 才报出，而不是 `build()` 的调用现场。

**失效条件**：如果大多数注册操作都是独立的、无交叉依赖，Command 缓冲是过度设计。

### 引擎对照

- **chaos**：`BasicModule` 的依赖是通过 `ModuleManager` 的加载顺序隐式控制的，没有显式依赖图。模块之间的依赖靠代码审查和文档约定。这在 proven_ground 这种固定模块集合的项目中可行，但扩展性受限。
- **UE**：`IPluginManager` 通过 `.uplugin` 的 `Dependencies` 字段做显式声明，结合 `LoadingPhase` 做两层过滤（先按阶段分组，再在同阶段内拓扑排序）。这是工业级方案，但依赖 JSON 文件与 C++ 代码的同步。
- **Bevy**：Bevy 没有显式的 Plugin 依赖声明机制。它靠 `PluginGroup` 的顺序和 `finish()` 的延迟注册来间接处理。如果 Plugin A 真的依赖 Plugin B 的 Resource，开发者通常在 `build()` 中 `panic`（隐式探测）。

**默认推荐**：**分支 A（显式声明依赖）作为核心机制，分支 B（隐式探测）作为运行时二次验证。**

**推荐依据**：
- UE 的工业级实践表明，当模块数量增长到两位数时，显式依赖图是维护启动顺序的唯一可持续方式。
- 我们吸收 UE 的两层过滤思想：先按 `LoadingPhase` 分组（粗粒度时序），再在组内做拓扑排序（细粒度依赖）。
- 隐式探测作为"安全网"——即使显式声明正确，运行时仍然检查关键 Resource/Component 是否存在，给出更友好的错误信息。
- 不采用分支 C 的 Command 缓冲作为默认，因为启动期的注册操作数量远小于运行时的实体操作，Command 缓冲的收益不大，代价明显。

---

## 问题 4：动态加载是否必要？何时该用？

前面的讨论默认 Plugin 是**编译期已知、启动时加载**的。但工业引擎还需要回答另一个问题：**能不能在程序运行起来之后，再加载新模块？**

### 场景绑定：Mod 社区与编辑器扩展

想象你的游戏发售后，社区想做一个"新武器包"Mod。如果所有代码必须编译进主程序，玩家就没办法安装 Mod。同样，你的编辑器想支持第三方开发者写插件——比如一个"自动 LOD 生成"工具——这个插件不应该要求重新编译整个编辑器。

### 根因分析：代码加载的两种时间线

编译型语言（C++）的代码要么在链接期进入可执行文件，要么在运行时通过动态链接库（DLL/so/dylib）加载。这两种方式不是"性能好坏"的区别，而是**工程模型的根本差异**：
- **静态链接**：所有符号在编译期解析，类型安全由编译器保证，无运行时加载开销。
- **动态加载**：符号在运行时解析，需要显式的 ABI 边界（C 函数或虚函数），面临类型系统断裂风险。

### 分支 A：静态链接（编译期全集）

**核心思路**：所有 Plugin 在编译时链接到同一个可执行文件。通过条件编译（C++ `#ifdef` / Cargo feature）控制哪些 Plugin 被包含。

```cpp
// 通过宏或条件编译选择插件
#ifdef ENABLE_PHYSICS
    app.add_plugin(PhysicsPlugin{});
#endif
```

**适用场景**：单机游戏、模块数量固定、不需要第三方扩展、追求极致启动速度和部署简单性。

**隐藏代价**：
1. **可执行文件膨胀**：即使某个功能玩家从不使用，它的代码也躺在二进制里。
2. **无法支持 Mod**：第三方无法在不获取你完整源码的情况下扩展功能。
3. **热重载不可能**：修改任何代码都需要重新编译链接，迭代速度慢。

**失效条件**：当你需要 Mod 支持、编辑器插件、或 A/B 测试（灰度发布不同功能组合）时，静态链接完全不可用。

### 分支 B：DLL/so 动态加载（UE / chaos 路线）

**核心思路**：Plugin 编译为独立的动态链接库。主程序在运行时通过操作系统 API（`LoadLibrary` / `dlopen`）加载，通过符号表获取入口函数，创建 Plugin 实例。

```cpp
class DynamicPluginLoader {
public:
    std::unique_ptr<IPlugin> load(const char* path) {
        void* handle = dlopen(path, RTLD_NOW);
        auto create = (IPlugin* (*)())dlsym(handle, "CreatePlugin");
        return std::unique_ptr<IPlugin>(create());
    }
};
```

**适用场景**：需要 Mod 支持、编辑器插件、大型团队并行开发（不同团队编译不同 DLL）。

**隐藏代价**（这是 DLL 动态加载在 ECS 引擎中**最痛苦的部分**）：

1. **Archetype 迁移问题**：
   ECS 中，实体的组件集合决定它属于哪个 Archetype（表）。如果动态加载的 Plugin 注册了一个新组件类型 `ModComponent`，已有实体无法"自动获得"这个新组件。更严重的是，如果 Plugin 卸载了，所有带有该 Plugin 注册组件的实体都必须被清理或迁移——这在运行时是极复杂的操作。

2. **反射注册表同步**：
   在 [[反射系统]] 中，我们建立了 `TypeRegistry`，把 C++ 类型映射到运行时 ID。DLL 里的类型有它自己的静态初始化顺序，可能主程序的 `TypeRegistry` 已经冻结后，DLL 才尝试注册新类型。这会导致类型 ID 冲突或注册失败。

3. **Resource 生命周期断裂**：
   Plugin 在 `build()` 中插入了一个 `ModResource`。Plugin 卸载时，谁负责从 World 中移除这个 Resource？如果其他 System 正在持有这个 Resource 的引用，卸载会导致悬垂指针。

4. **虚函数 ABI 脆弱性**：
   C++ 的虚函数表布局依赖编译器版本、编译选项、结构体对齐方式。如果 Plugin 用 MSVC 编译，主程序用 Clang 编译，虚函数调用可能直接崩溃。UE 通过强制统一工具链解决；独立维护时难以保证。

**失效条件**：如果引擎架构没有为动态卸载设计（如没有 Archetype 迁移能力、没有 Resource 引用计数），DLL 动态加载只能是"加载后永不卸载"的单向操作。这在 Mod 场景下尚可接受（Mod 加载后玩到关机），但在编辑器热重载场景下不够。

### 分支 C：脚本/字节码层（Lua / WASM）

**核心思路**：不在 C++ 层动态加载，而是内嵌一个脚本虚拟机或 WASM 运行时。Plugin 用脚本语言编写，通过 C API 与引擎交互。

```cpp
// Lua 示例
lua_State* L = luaL_newstate();
luaL_dofile(L, "mods/weapon_pack.lua");
// 脚本通过 C API 注册系统和组件
```

**适用场景**：需要第三方 Mod、对安全性要求高（防止 Mod 代码破坏玩家系统）、快速迭代（脚本无需编译）。

**隐藏代价**：
1. **性能鸿沟**：脚本语言的执行速度比 C++ 慢 10~100 倍。对于每帧调用的 System，这不可接受。
2. **绑定层工作量**：每个要暴露给脚本的 C++ 函数都需要写绑定代码（如 Lua 的 `lua_pushinteger`、WASM 的 `extern "C"` 包装）。
3. **调试困难**：脚本层的崩溃堆栈与 C++ 层割裂。

**失效条件**：需要 Plugin 实现高性能 System（如自定义渲染 Pass、物理求解器）时，脚本层不适用。

### 决策分析

| 需求 | 推荐方案 |
|------|---------|
| 无 Mod 需求，固定模块集合 | 分支 A（静态链接） |
| 需要 Mod，Mod 只做 Gameplay 逻辑 | 分支 C（脚本/WASM） |
| 需要 Mod，Mod 涉及 ECS 组件定义 | 分支 B（DLL），但限制"加载后不可卸载" |
| 编辑器热重载 C++ 代码 | 分支 B + 特殊设计（如 UE 的 Live Coding） |

**默认推荐**：**阶段 4 采用分支 A（静态链接）为主，但 Plugin 系统的接口设计预留分支 B（DLL）的扩展路径。**

**推荐依据**：
- 我们的当前阶段是"核心运行时闭环"，目标是让引擎能描述世界并运转。Mod 支持和编辑器热重载是后续阶段（阶段 8：工具链与 AI 桥接）的目标。
- 但 Plugin 接口如果设计为"只能是静态链接"，未来升级到 DLL 时需要大规模重构。因此我们选择"接口兼容 DLL，但当前实现用静态链接"。
- 具体预留方式：
  - `IPlugin` 的虚函数接口本身就是 DLL ABI 兼容的（通过 C++ 虚函数或纯 C 函数桥）。
  - `App::add_plugin()` 内部维护 `std::vector<std::unique_ptr<IPlugin>>`，未来可以替换为 `DynamicPluginLoader` 加载的 Plugin，接口不变。
- UE 的 `IPluginManager` 同时支持内置插件（静态链接）和外部插件（DLL），我们通过一致的接口设计提前吸收这一工业级洞察。

**关于热插拔的诚实说明**：
DLL 热卸载在 ECS 引擎中是一个**未完全解决的开放问题**。即使 UE 的 Live Coding 也做不到"卸载一个 DLL 并清理它注册的所有 UObject"——它做的是"重新加载代码，但对象状态保留"。Bevy 因为 Rust 的编译模型，几乎不支持 DLL Plugin。我们在笔记中**不假装这个问题有简单方案**，而是明确标注：
> "当前阶段支持动态加载（加载 DLL），不支持热卸载。卸载一个 Plugin 需要停止世界、迁移或销毁所有带该 Plugin 组件的实体、清理反射注册表——这在运行时是极高风险操作，留待阶段 8 专门设计。"

---

## 问题 5：ECS 架构下 Plugin 的具体映射与 AI 友好设计

前面四个问题分别回答了"为什么需要 Plugin"、"契约边界"、"生命周期"、"依赖与动态加载"。最后一个问题是：**在我们已有的 ECS + 反射 + 调度框架中，Plugin 系统具体长什么样？** 以及，从 AI 协作的角度，Plugin 应该满足哪些可观测性约束？

### 场景绑定：一个 Plugin 到底在做什么？

当你的 AI Agent 读取引擎状态，它看到 World 里有 20 个组件类型、50 个 System、10 个 Resource。它想问："这些是谁注册的？如果我想禁用物理模拟，我该关掉什么？" 如果没有 Plugin 级别的元数据，AI 只能看到一堆零散的类型和函数，无法理解"物理功能"作为一个整体的存在。

### ECS 映射：Plugin 是 World 的"增量描述符"

在 ECS 架构下，Plugin 本质上是一个**世界增量描述符**——它描述"我要往这个空白世界添加什么"。具体到我们的引擎，一个 Plugin 可以注册四类东西：

```cpp
class IPlugin {
public:
    virtual std::string name() const = 0;
    virtual std::vector<std::string> dependencies() const { return {}; }
    virtual LoadingPhase phase() const { return LoadingPhase::Systems; }
    
    virtual void build(App& app) = 0;           // 注册组件、系统、事件
    virtual bool ready(const App& app) const { return true; }
    virtual void finish(App& app) {}            // 二次配置
    virtual void cleanup(App& app) {}           // 清理
};

// 一个典型 Plugin 的 build() 内容
class InputPlugin : public IPlugin {
public:
    void build(App& app) override {
        // 1. 注册组件类型（进入反射注册表）
        app.register_component<KeyboardInput>();
        app.register_component<MouseInput>();
        
        // 2. 注册事件类型
        app.register_event<InputEvent>();
        
        // 3. 插入 Resource（全局状态）
        app.insert_resource(InputState{});
        
        // 4. 注册 System 到 Schedule 的指定阶段
        app.add_system(PreUpdate, poll_input_system);
        app.add_system(Update, process_input_system);
    }
    
    std::string name() const override { return "Input"; }
    std::vector<std::string> dependencies() const override {
        return {"Window"}; // 需要窗口系统提供事件源
    }
};
```

注意这四类注册操作如何与已有的 ECS 子系统协作：
- **组件注册** → 调用 [[反射系统]] 的 `TypeRegistry::register<T>()`，让 Inspector 能识别新组件。
- **系统注册** → 调用 [[系统调度与确定性]] 的 `Schedule::add_system()`，进入依赖图和并行调度分析。
- **Resource 插入** → 直接进入 World 的 Resource 存储，被 System 通过 `Res<T>` / `ResMut<T>` 访问。
- **事件注册** → 进入 [[事件总线]] 的事件类型表，支持 Pub-Sub 通信。

Plugin 自己没有状态，它只是**把这些东西介绍给世界**。这正是 Bevy 的设计精髓——Plugin 是 World 的"介绍人"，不是 World 的"租户"。

### AI 友好设计红线检查

按照本引擎"为 AI 协作而设计"的核心原则，我们对 Plugin 系统进行以下检查：

**1. 状态平铺**
> Plugin 本身没有状态，所有可变状态都以 Resource 或 Component 形式存在 World 中。✅ 满足。
> 如果未来某个 Plugin 需要配置参数（如 `PhysicsConfig { float gravity; }`），这个参数也是作为 Resource 插入 World，可以被 Inspector 编辑、被 AI Schema 描述。

**2. 自描述**
> AI 应该能在不读 C++ 头文件的情况下，知道"这个 Plugin 为世界增加了什么能力"。
> 实现方式：Plugin 注册的所有内容（组件类型、系统、Resource 类型、事件类型）都进入反射注册表。AI 通过查询注册表，获得每个 Plugin 的"能力清单"。

```cpp
struct PluginManifest {
    std::string name;
    std::vector<std::string> dependencies;
    std::vector<TypeId> registered_components;
    std::vector<TypeId> registered_resources;
    std::vector<SystemId> registered_systems;
    std::vector<TypeId> registered_events;
};

// AI 可通过 API 获取
PluginManifest manifest = app.get_plugin_manifest("Physics");
// manifest.registered_components 包含 RigidBody, Collider 的类型 ID
// manifest.registered_systems 包含 physics_step 的系统 ID
```

**3. 确定性**
> 给定相同的 Plugin 加载顺序和配置，World 的初始状态必须是确定性的。
> 实现方式：拓扑排序是确定性的（相同输入图产生相同输出顺序）；`TypeRegistry` 的类型 ID 分配不依赖静态初始化顺序（我们在 [[反射系统]] 中使用显式注册而非 `__attribute__((constructor))`）。

**4. 工具边界（MCP 接口）**
> 如果 AI 通过 MCP/Tool 接口操作 Plugin，接口输入输出必须是结构化 JSON。
> 设计示例：

```json
// AI 请求：列出所有已加载 Plugin
// 响应
{
  "plugins": [
    {
      "name": "Physics",
      "phase": "Systems",
      "dependencies": ["Transform", "Time"],
      "capabilities": {
        "components": ["RigidBody", "Collider"],
        "resources": ["Gravity", "PhysicsConfig"],
        "systems": ["physics_step", "collision_detection"]
      }
    }
  ]
}

// AI 请求：添加一个 Plugin
{
  "action": "add_plugin",
  "plugin_name": "ParticleEffects",
  "config": {
    "max_particles": 10000
  }
}
```

**5. Agent 安全**
> 多 Agent 同时操作不同 Plugin 时，如何防止冲突？
> 设计约束：
> - Plugin 的 `build()` 是启动期执行的，运行时不允许新增 Plugin（除非走特殊的动态加载通道）。
> - 如果未来支持运行时加载，每个 Plugin 的变更必须先经过 `PluginRegistry` 的事务机制——"准备变更 → 验证无冲突 → 原子提交"。
> - Plugin 白名单：AI Agent 只能操作属于其职责范围的 Plugin。例如，"物理调优 Agent"只能修改 `PhysicsConfig` Resource，不能卸载 `GraphicsPlugin`。

### 多 Agent 编排视角

在 [[系统调度与确定性]] 中，我们讨论了多 Agent 对同一 World 的并行修改。Plugin 系统增加了另一个维度：**不同 Agent 可能负责维护不同的 Plugin**。

```
Agent A（Gameplay 专家） ←→ 维护 CombatPlugin, QuestPlugin
Agent B（物理专家）     ←→ 维护 PhysicsPlugin
Agent C（渲染专家）     ←→ 维护 GraphicsPlugin
```

这些 Agent 可以并行工作，因为：
- 它们的修改落在不同的源码目录（`plugins/combat/`, `plugins/physics/`）。
- 只要遵守 Plugin 接口契约（`build()` 只操作 App API，不直接访问其他 Plugin 的内部），Git 合并冲突率极低。
- 启动期的依赖检查（拓扑排序）在集成阶段自动捕获"A 的新系统依赖了 B 已移除的组件"这类跨 Agent 冲突。

这是 Plugin 系统对多 Agent 编排的核心价值：**它把引擎的物理边界（目录、编译单元）与 ECS 的逻辑边界（System 访问的 Component 集合）对齐了。**

---

## 默认推荐路径总结

### 完整设计清单

| 设计点 | 默认推荐 | 条件切换 |
|--------|---------|---------|
| 契约形态 | 无状态 `IPlugin` 接口（`build/ready/finish/cleanup`） | 模块极少的原型可退化为函数指针 |
| 生命周期 | 四阶段 + `LoadingPhase` 枚举（Core/Systems/Services/Gameplay） | < 5 个 Plugin 可退化为二阶段 |
| 依赖表达 | 显式声明 + 拓扑排序 + 运行时隐式探测验证 | 原型期可只用手工顺序 |
| 动态加载 | 当前静态链接为主，接口预留 DLL 扩展 | 阶段 8 再引入 DLL |
| 状态归属 | 全在 World（Resource/Component），Plugin 无状态 | — |
| AI 可观测 | 每个 Plugin 产出 `PluginManifest`，可被序列化查询 | — |

### ECS 映射

Plugin 不是 World 的"租户"，而是**增量描述符**——它只描述"我要往这个空白世界添加什么"，本身不持有任何运行时状态。在 ECS 架构下，一个 Plugin 的 `build()` 通常只执行四类注册操作，所有状态最终都进入 World：

- **组件注册** → 进入反射系统的 `TypeRegistry`，供 Inspector 和序列化使用。
- **系统注册** → 进入 `Schedule` 的依赖图，接受并行调度分析。
- **Resource 插入** → 进入 World 的全局存储，被 System 通过 `Res<T>` / `ResMut<T>` 访问。
- **事件注册** → 进入事件总线，支持 Pub-Sub 通信。

配置参数（如 `PhysicsConfig`）也以 Resource 形式存入 World，确保状态平铺、AI 可观测、Inspector 可编辑。Plugin 对象在 `build()` 结束后即可被丢弃（或仅保留在注册表里作审计用途），这正是 Bevy "Plugin 是 World 的介绍人，不是租户"的设计精髓。

### 条件切换路径

"默认推荐"不是绝对真理。以下是在特定约束下的合法退化路径：

| 当前约束 | 从默认推荐退化为 | 回退触发条件 |
|---------|----------------|------------|
| 契约形态 | 函数指针 + 元数据结构体 | 模块数量 < 3，且确定短期内不增长 |
| 生命周期 | 二阶段（build → run） | 模块数量 < 5，无跨模块类型依赖，全同步初始化 |
| 依赖表达 | 手工顺序 + 运行时隐式探测 | 原型期快速迭代，模块依赖每周重构 |
| 动态加载 | 纯静态链接，不预留 DLL 接口 | 确定游戏为纯单机、无 Mod、无编辑器扩展 |
| 配置驱动 | 元数据内嵌到 C++ 代码 | 模块数量 < 10，无需工具链离线分析兼容性 |

关键原则：**退化路径是"暂时简化"，不是"放弃设计"**。当约束条件改变（模块数量增长、团队扩大、需要 Mod），应平滑升级回默认推荐，而非重写架构。

### 扩展路径

阶段 4 的 Plugin 系统为以下未来能力预留了接口，但当前不实现：

1. **DLL 动态加载**：`IPlugin` 的虚函数接口天然兼容 DLL ABI。未来只需将 `App::add_plugin()` 的内部存储从 `std::vector<std::unique_ptr<IPlugin>>` 替换为 `DynamicPluginLoader`，上层接口不变。
2. **N 阶段生命周期**：`LoadingPhase` 枚举可随时扩展新值（如 `PreEarlyLoadingScreen`、`PostEngineInit`），无需修改 `IPlugin` 接口。
3. **脚本/字节码层**：在阶段 8 引入 Lua / WASM 运行时后，脚本 Plugin 通过同样的 `IPlugin` 代理向 World 注册 System，C++ 核心层无感知。
4. **AI 可观测增强**：`PluginManifest` 未来可扩展为包含 System 的读写组件集合、Resource 依赖图，供 AI Agent 做变更影响分析。

这些扩展路径的共同特点是**接口先行，实现后置**。阶段 4 只承担接口设计成本，避免后期大规模重构。

### 在 ECS 兼容前提下吸收的 UE 工业级设计

1. **分层加载阶段**：UE 的 `LoadingPhase` 思想被提取为 `Core → Systems → Services → Gameplay` 四阶段。不是硬编码 7 个阶段，而是用枚举允许未来扩展。
2. **显式依赖图**：UE 的 `.uplugin` Dependencies 被内化为 `IPlugin::dependencies()` 虚函数，启动期做拓扑排序和环检测。
3. **内置/外置插件统一接口**：UE 的 `IPluginManager` 同时支持静态和动态插件。我们用统一的 `IPlugin` 接口预留这一能力，当前实现以静态为主。

### 与 Bevy 的对照与超越

| 维度 | Bevy | 本引擎推荐 |
|------|------|-----------|
| Plugin 状态 | 无状态 ✅ | 无状态 ✅ |
| 生命周期 | build/ready/finish/cleanup | build/ready/finish/cleanup + `LoadingPhase` |
| 依赖声明 | 无显式机制 | 显式拓扑排序 ✅ |
| 动态加载 | 不支持 | 接口预留 ✅ |
| Plugin 组合 | `PluginGroup` | `PluginGroup` 等价物 + 依赖图自动排序 |

我们在 Bevy 的优雅基础上，补足了两个工业级能力：**显式依赖管理**（解决启动时序黑箱问题）和 **动态加载预留**（支持未来 Mod 和编辑器扩展）。

### 简化路径（当前阶段可立即落地）

如果你现在就想让引擎跑起来，不需要一次性实现全部功能。最小可行实现（MVP）只需：

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual void build(App& app) = 0;
    virtual std::string name() const = 0;
};

class App {
    std::vector<std::unique_ptr<IPlugin>> plugins_;
    World world_;
public:
    void add_plugin(std::unique_ptr<IPlugin> plugin) {
        plugins_.push_back(std::move(plugin));
    }
    void run() {
        for (auto& p : plugins_) p->build(*this);
        world_.run();
    }
    World& world() { return world_; }
};
```

这个 MVP **缺少** `LoadingPhase`、拓扑排序、`ready/finish`、动态加载。但它能立即解决"main.cpp 膨胀"问题。未来补全的具体差距：
- 增加 `dependencies()` 和拓扑排序（约 50 行代码）
- 增加 `ready()` / `finish()`（约 30 行代码）
- 增加 `LoadingPhase` 分组（约 40 行代码）
- 增加 `PluginManifest` 和 AI 查询接口（约 60 行代码，依赖反射系统）

---

> **下一步**：[[RHI抽象层]]，因为模块系统就位后，我们需要把渲染能力也封装为 `GraphicsPlugin`，让图形后端（OpenGL / Vulkan / bgfx）成为可替换、可扩展的模块——而不是一堆散落在 `main()` 里的初始化代码。

---

## 参考与三角验证

| 信息来源 | 关键结论 | 验证状态 |
|---------|---------|---------|
| chaos `BasicModule` + `CHAOS_MODULE` | 分阶段生命周期（Earliest/PreInit/Init/PostInit）+ 动态加载入口 | ✅ 已阅读源码分析笔记 |
| UE `IPluginManager` / `FModuleManager` / `.uplugin` | 7 阶段 LoadingPhase + 显式依赖声明 + 内置/外置统一接口 | ✅ 已阅读源码分析笔记 |
| Bevy `Plugin` trait / `PluginGroup` / `DefaultPlugins` | 无状态 Plugin + build/ready/finish/cleanup + 声明式组合 | ✅ 已阅读源码分析笔记 |
| Modulith paper (FDG 2023) | ECS 作为模块间通用数据组织层，DLL 动态链接 + 运行时加载 | ✅ 已搜索验证 |
| Flecs / EnTT 开源实现 | 模块系统较弱（主要靠用户层组织），验证了"ECS 核心库不提供 Plugin 层"的设计趋势 | ✅ 已搜索验证 |
| Ark.jl / ReactiveECS.jl (2025) | ECS 模块化被确认为现代引擎设计原则，但 Plugin 层仍多在用户空间实现 | ✅ 已搜索验证 |

**搜索关键词**：`game engine plugin system modular architecture ECS design 2024`, `Bevy plugin system ECS App build lifecycle design pattern`, `Modulith game engine modding ECS plugin architecture`
