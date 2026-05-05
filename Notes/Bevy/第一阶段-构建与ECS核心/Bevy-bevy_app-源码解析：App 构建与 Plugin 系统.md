---
title: Bevy-bevy_app-源码解析：App 构建与 Plugin 系统
date: 2026-05-03
tags:
  - bevy-source
  - engine-architecture
  - ecs
  - bevy_app
  - plugin
aliases:
  - "Bevy bevy_app App 构建与 Plugin 系统"
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy全解析主索引]]

## 一、模块定位

**物理路径**：`crates/bevy_app/`

**Cargo.toml 关键信息**：
- 版本：`0.19.0-dev`
- 核心依赖：`bevy_ecs`（ECS 核心）、`bevy_derive`（派生宏）、`bevy_utils`（工具集）、`bevy_tasks`（任务池）、`bevy_platform`（平台抽象）
- 可选依赖：`bevy_reflect`（反射系统）
- 特殊 feature：`std`（标准库支持）、`web`（WASM 浏览器 API）、`hotpatching`（热更新）

**依赖深度**：`bevy_app` 位于 Bevy 引擎的**应用层**，直接构建在 `bevy_ecs` 之上。它本身不依赖渲染、输入、窗口等上层模块，但为所有这些模块提供 Plugin 注册和生命周期管理的统一入口。

**下游依赖**：几乎所有 Bevy 的其他 crate（`bevy_render`、`bevy_winit`、`bevy_input`、`bevy_audio` 等）都通过实现 `Plugin` trait 来向 `App` 注册自身。

---

## 二、公共接口梳理（第 1 层）

`bevy_app` 的公共接口集中在 `app.rs`、`plugin.rs`、`plugin_group.rs` 和 `sub_app.rs` 中。以下是核心类型和方法的地图：

### 2.1 App —— 用户-facing 的主入口

`App` 是开发者写 Bevy 应用时接触最多的类型，典型用法：

```rust
App::new()
    .add_plugins(DefaultPlugins)
    .add_systems(Update, my_system)
    .run();
```

**核心公共方法**：

| 方法 | 职责 |
|------|------|
| `App::new()` / `App::empty()` | 构造 App，前者带默认配置（MainSchedulePlugin、AppTypeRegistry 等） |
| `add_plugins(plugins)` | 注册一个或多个 Plugin/PluginGroup |
| `add_systems(schedule, systems)` | 向指定 Schedule 添加 System |
| `insert_resource<T>` / `init_resource<T>` | 插入/初始化 Resource 到主 World |
| `register_system(system)` | 注册一个可被 `World::run_system` 调用的 System |
| `run()` | 启动 App，调用 runner 函数（通常进入事件循环） |
| `update()` | 手动触发一次所有 SubApp 的更新 |
| `world()` / `world_mut()` | 访问主 SubApp 的 World |
| `sub_app(label)` / `insert_sub_app(label, sub_app)` | 管理子应用 |

### 2.2 Plugin trait —— 模块化配置的标准契约

> 文件：`crates/bevy_app/src/plugin.rs`，第 57~92 行

```rust
pub trait Plugin: Downcast + Any + Send + Sync {
    fn build(&self, app: &mut App);
    fn ready(&self, _app: &App) -> bool { true }
    fn finish(&self, _app: &mut App) {}
    fn cleanup(&self, _app: &mut App) {}
    fn name(&self) -> &str { core::any::type_name::<Self>() }
    fn is_unique(&self) -> bool { true }
}
```

Plugin 的**生命周期**分为四个阶段：
1. **build**：插件被添加到 App 时立即调用，用于注册系统、资源、事件等。
2. **ready**：App 启动前反复检查，直到所有插件都返回 `true`（用于等待异步初始化）。
3. **finish**：所有插件 ready 后调用，可用于依赖其他插件完成后的配置。
4. **cleanup**：finish 之后、schedule 执行之前调用，用于清理构建阶段使用的临时资源。

### 2.3 PluginGroup —— 插件的组合与定制

> 文件：`crates/bevy_app/src/plugin_group.rs`，第 199~535 行

`PluginGroup` 允许将多个 Plugin 打包成一个可定制的集合。Bevy 内置的 `DefaultPlugins` 和 `MinimalPlugins` 就是通过这种方式组织的。

`PluginGroupBuilder` 提供了链式 API 来增删改插件：
- `add(plugin)` / `add_before<Target>(plugin)` / `add_after<Target>(plugin)`
- `disable::<T>()` / `enable::<T>()`
- `set::<T>(plugin)` —— 替换已有插件的配置

### 2.4 SubApp —— 多世界隔离机制

> 文件：`crates/bevy_app/src/sub_app.rs`，第 65~81 行

```rust
pub struct SubApp {
    world: World,
    plugin_registry: Vec<Box<dyn Plugin>>,
    plugin_names: HashSet<String>,
    plugin_build_depth: usize,
    plugins_state: PluginsState,
    update_schedule: Option<InternedScheduleLabel>,
    extract: Option<ExtractFn>,
}
```

每个 `SubApp` 拥有**独立的 `World`**。主 App 有一个默认的 "main" SubApp，额外的 SubApp 通过 `AppLabel` 标识。最典型的用例是 `bevy_render` 创建的 Render SubApp：主世界跑游戏逻辑，渲染世界跑渲染逻辑，两者通过 `extract` 函数在每帧之间同步数据。

---

## 三、核心数据结构（第 2 层）

### 3.1 App 的内存布局

> 文件：`crates/bevy_app/src/app.rs`，第 86~97 行

```rust
pub struct App {
    pub(crate) sub_apps: SubApps,
    pub(crate) runner: RunnerFn,
    fallback_error_handler: Option<ErrorHandler>,
}

pub struct SubApps {
    pub main: SubApp,
    pub sub_apps: HashMap<InternedAppLabel, SubApp>,
}

type RunnerFn = Box<dyn FnOnce(App) -> AppExit>;
```

`App` 本身非常轻量，核心状态全部下沉到 `SubApp` 的 `World` 中。这种设计让 `App` 更像是一个**容器/编排器**，而非数据持有者。

### 3.2 Plugin 注册表的实现细节

Plugin 的注册信息存储在每个 `SubApp` 中：

- `plugin_registry: Vec<Box<dyn Plugin>>` —— 保存所有已添加的插件实例，按**插入顺序**排列。
- `plugin_names: HashSet<String>` —— 用于快速检测重复插件（基于 `Plugin::name()` 的返回值）。
- `plugin_build_depth: usize` —— 记录当前嵌套的 `build` 调用深度，防止在 build 过程中调用 `app.update()` 或 `app.run()`。

> 文件：`crates/bevy_app/src/app.rs`，第 523~566 行

```rust
pub(crate) fn add_boxed_plugin(
    &mut self,
    plugin: Box<dyn Plugin>,
) -> Result<&mut Self, AppError> {
    // 1. 检查唯一性
    if plugin.is_unique() && self.main_mut().plugin_names.contains(plugin.name()) {
        Err(AppError::DuplicatePlugin { ... })?;
    }
    // 2. 预留 registry 位置（保证嵌套添加时的顺序）
    let index = self.main().plugin_registry.len();
    self.main_mut().plugin_registry.push(Box::new(PlaceholderPlugin));
    self.main_mut().plugin_build_depth += 1;
    // 3. 调用 build（支持 panic 安全）
    let f = AssertUnwindSafe(|| plugin.build(self));
    #[cfg(feature = "std")]
    let result = catch_unwind(f);
    // 4. 恢复状态
    self.main_mut().plugin_names.insert(plugin.name().to_string());
    self.main_mut().plugin_build_depth -= 1;
    self.main_mut().plugin_registry[index] = plugin;
    Ok(self)
}
```

**关键设计**：`PlaceholderPlugin` 占位机制。当插件 A 的 `build` 方法里又添加了插件 B 时，B 会先被添加到 registry 的末尾，但 A 的索引已经通过占位符固定。这保证了**无论是否嵌套调用，最终 registry 都反映真实的添加顺序**。

### 3.3 PluginsState —— 插件生命周期状态机

> 文件：`crates/bevy_app/src/plugin.rs`，第 103~113 行

```rust
pub enum PluginsState {
    Adding,    // 插件还在被添加
    Ready,     // 所有已添加插件的 ready() 返回 true
    Finished,  // finish() 已执行完毕
    Cleaned,   // cleanup() 已执行完毕
}
```

App 的 `run()` 方法在真正进入主循环前，会先等待 `plugins_state` 从 `Adding` 推进到 `Cleaned`。

---

## 四、关键行为分析（第 3 层）

### 4.1 App::run() 的执行链路

> 文件：`crates/bevy_app/src/app.rs`，第 188~198 行

```rust
pub fn run(&mut self) -> AppExit {
    if self.is_building_plugins() {
        panic!("App::run() was called while a plugin was building.");
    }
    let runner = core::mem::replace(&mut self.runner, Box::new(run_once));
    let app = core::mem::replace(self, App::empty());
    (runner)(app)
}
```

注意这里的**所有权转移**：`run()` 通过 `mem::replace` 把 `self` 整体移交给 `runner` 函数，runner 消费整个 `App`。这意味着 `run()` 之后原 `App` 变量不可再用（对窗口化应用来说通常不会返回）。

默认的 `run_once` runner（当没有 `WinitPlugin` 或 `ScheduleRunnerPlugin` 时）：

> 文件：`crates/bevy_app/src/app.rs`，第 1517~1528 行

```rust
fn run_once(mut app: App) -> AppExit {
    while app.plugins_state() == PluginsState::Adding {
        bevy_tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();
    app.update();
    app.should_exit().unwrap_or(AppExit::Success)
}
```

**执行时序**：
1. 轮询 `plugins_state`，等待所有插件 ready。
2. 调用 `finish()` —— 每个插件的 `finish()` 按注册顺序执行。
3. 调用 `cleanup()` —— 每个插件的 `cleanup()` 按注册顺序执行。
4. 调用 `update()` —— 执行一帧。
5. 检查 `AppExit` 并返回。

### 4.2 SubApps::update() 的帧更新流程

> 文件：`crates/bevy_app/src/sub_app.rs`，第 551~570 行

```rust
pub fn update(&mut self) {
    #[cfg(feature = "trace")]
    let _bevy_update_span = info_span!("update").entered();
    {
        let _bevy_frame_update_span = info_span!("main app").entered();
        self.main.run_default_schedule();
    }
    for (_label, sub_app) in self.sub_apps.iter_mut() {
        let _sub_app_span = info_span!("sub app", name = ?_label).entered();
        sub_app.extract(&mut self.main.world);  // 从主世界提取数据
        sub_app.update();                       // 运行子世界的 schedule
    }
    self.main.world.clear_trackers();  // 只在主世界清除变更追踪器
}
```

**关键洞察**：
- 主 SubApp 先跑 `run_default_schedule()`（即 `Main` schedule）。
- 然后对每个子 SubApp：先 `extract`（通常是主世界 → 渲染世界的数据拷贝），再 `update`。
- **变更追踪器只在主世界清除**。子世界的 `clear_trackers()` 在各自的 `SubApp::update()` 中调用。这意味着子世界可以独立管理自己的变更检测生命周期。

### 4.3 Plugin 的 finish/cleanup 中的 "Hokey Pokey" 技巧

> 文件：`crates/bevy_app/src/app.rs`，第 262~280 行

```rust
pub fn finish(&mut self) {
    let mut hokeypokey: Box<dyn Plugin> = Box::new(HokeyPokey);
    for i in 0..self.main().plugin_registry.len() {
        core::mem::swap(&mut self.main_mut().plugin_registry[i], &mut hokeypokey);
        hokeypokey.finish(self);
        core::mem::swap(&mut self.main_mut().plugin_registry[i], &mut hokeypokey);
    }
    self.main_mut().plugins_state = PluginsState::Finished;
}
```

这里用了一个精巧的 swap 技巧：用一个零大小类型 `HokeyPokey` 作为临时占位，把 registry 中的插件逐个"取出"调用 `finish`，再放回去。这样做的目的是**避免持有 `&mut Box<dyn Plugin>` 的同时又需要可变借用整个 App**（因为 `finish(&self, app: &mut App)` 需要 `&mut App`）。

---

## 五、与上下层的关系

```
┌─────────────────────────────────────┐
│  上层：bevy_render / bevy_winit      │  ← 提供 WinitPlugin、RenderPlugin 等
│  实现 Plugin，通过 add_plugins 注册   │
├─────────────────────────────────────┤
│  本层：bevy_app                      │
│  - App / SubApp 容器                 │
│  - Plugin / PluginGroup 注册         │
│  - Main / Startup / Fixed 调度       │
├─────────────────────────────────────┤
│  下层：bevy_ecs                      │  ← 提供 World、Schedule、System、Query
│  App 的所有操作最终都委托到 World     │
├─────────────────────────────────────┤
│  更下层：bevy_tasks / bevy_platform  │  ← 任务池、平台抽象（时间、线程）
└─────────────────────────────────────┘
```

**数据流向**：
- `App::add_systems` → `SubApp::add_systems` → `World::resource_mut::<Schedules>()` → Schedule 内部注册 System。
- `App::insert_resource` → `SubApp::insert_resource` → `World::insert_resource`。
- `App::run` → `runner` 回调 → 循环调用 `App::update` → `SubApps::update` → `World::run_schedule`。

---

## 六、设计亮点与潜在陷阱

### 亮点 1：Plugin 作为模块边界
Bevy 的**所有功能**都是插件，包括最核心的 `MainSchedulePlugin` 和 `TaskPoolPlugin`。这种极致的模块化让引擎的每个子系统都可以被替换、禁用或重新排序。

### 亮点 2：SubApp 的多世界隔离
渲染世界与主世界物理隔离，通过显式的 `extract` 函数同步数据。这比单世界共享数据更安全（无意外并发竞争），也让渲染管线可以跑在独立的线程/调度节奏上。

### 亮点 3：PluginGroup 的声明式组合
`plugin_group!` 宏让插件集合的定义变得声明式，支持条件编译（`#[cfg(feature = ...)]`）和文档自动生成。

### 潜在陷阱 1：run() 的所有权转移
`App::run()` 消费自身，如果你在 `main()` 里还需要持有 `App` 的引用，会编译失败。这是有意的设计，但新手容易困惑。

### 潜在陷阱 2：SubApp 的 plugin_registry 独立
每个 `SubApp` 有自己的 `plugin_registry`，但 `App::add_plugins` 默认只向**主** SubApp 添加插件。如果插件需要在子 App 中生效，需要显式处理（如 `RenderPlugin` 在 `build` 中手动创建 Render SubApp）。

### 潜在陷阱 3：Plugin::ready 的轮询开销
默认 runner 在主线程忙等 `plugins_state() == PluginsState::Adding`，期间不断调用 `tick_global_task_pools_on_main_thread()`。如果某个插件的 `ready()` 永远返回 `false`，应用会死循环。

---

## 七、关键源码片段

### 7.1 App 默认初始化流程

> 文件：`crates/bevy_app/src/app.rs`，第 109~137 行

```rust
impl Default for App {
    fn default() -> Self {
        let mut app = App::empty();
        app.sub_apps.main.update_schedule = Some(Main.intern());
        #[cfg(feature = "bevy_reflect")]
        {
            app.init_resource::<AppTypeRegistry>();
        }
        app.add_plugins(MainSchedulePlugin);  // 注册主调度插件
        app.add_systems(First, message_update_system...);  // 注册消息更新系统
        app.add_message::<AppExit>();  // 注册退出消息
        app
    }
}
```

### 7.2 Plugin 的函数式实现

> 文件：`crates/bevy_app/src/plugin.rs`，第 96~100 行

```rust
impl<T: Fn(&mut App) + Send + Sync + 'static> Plugin for T {
    fn build(&self, app: &mut App) {
        self(app);
    }
}
```

任何 `fn(&mut App)` 都自动实现 `Plugin`，这让小型插件可以直接用函数编写，无需定义 struct。

### 7.3 PluginGroupBuilder 的 finish 方法

> 文件：`crates/bevy_app/src/plugin_group.rs`，第 517~534 行

```rust
pub fn finish(mut self, app: &mut App) {
    for ty in &self.order {
        if let Some(entry) = self.plugins.shift_remove(ty)
            && entry.enabled
        {
            if let Err(AppError::DuplicatePlugin { plugin_name }) =
                app.add_boxed_plugin(entry.plugin)
            {
                panic!("Error adding plugin {} in group {}: ...", plugin_name, self.group_name);
            }
        }
    }
}
```

按 `order` 数组中的 `TypeId` 顺序逐个添加启用的插件，通过 `shift_remove` 保持 O(n) 的遍历+删除效率。

---

## 八、关联阅读

- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：World 与 Entity 生命周期|Bevy-bevy_ecs-源码解析：World 与 Entity 生命周期]] —— App 底层的数据容器
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度|Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]] —— `add_systems` 的底层机制
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：Startup 与 Update 调度|Bevy-bevy_app-源码解析：Startup 与 Update 调度]] —— 同 crate，调度细节
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：State 状态机与状态过渡|Bevy-bevy_app-源码解析：State 状态机与状态过渡]] —— 同 crate，状态管理

---

**索引状态**：所属阶段：第一阶段-构建与ECS核心；对应计划笔记：[[Notes/Bevy/00-Bevy全解析主索引#1.3 App 生命周期（bevy_app）|Bevy-bevy_app-源码解析：App 构建与 Plugin 系统]]
