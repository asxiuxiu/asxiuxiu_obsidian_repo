---
title: Bevy-bevy_app-源码解析：Startup 与 Update 调度
date: 2026-05-03
tags:
  - bevy-source
  - engine-architecture
  - ecs
  - bevy_app
  - schedule
  - main-loop
aliases:
  - "Bevy bevy_app Startup 与 Update 调度"
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy全解析主索引]]

## 一、模块定位

**物理路径**：`crates/bevy_app/src/main_schedule.rs`、`crates/bevy_app/src/schedule_runner.rs`、`crates/bevy_app/src/sub_app.rs`

**依赖深度**：这些模块直接构建在 `bevy_ecs` 的 Schedule 系统之上，属于 **bevy_app** 的核心调度基础设施。它们不依赖渲染、窗口或输入，但定义了所有上层系统默认挂载的"时隙"（slot）。

**下游影响**：
- `bevy_render` 的提取和渲染系统挂靠在 `PostUpdate` 之后（通过 Render SubApp）。
- `bevy_input` 的事件处理系统通常挂在 `PreUpdate`。
- `bevy_transform` 的层级传播挂在 `PostUpdate`。
- 用户游戏逻辑默认挂在 `Update`。

---

## 二、公共接口梳理（第 1 层）

### 2.1 标准 Schedule 标签（ScheduleLabel）

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 56~196 行

Bevy 预定义了一系列 `ScheduleLabel`，开发者通过它们向不同生命周期阶段注册系统：

**启动阶段（只执行一次）**：
| Label | 用途 |
|-------|------|
| `PreStartup` | 最早的启动钩子，用于在 Startup 之前执行初始化 |
| `Startup` | 应用启动时执行一次的逻辑 |
| `PostStartup` | Startup 之后的清理或后处理 |

**主循环阶段（每帧执行）**：
| Label | 用途 |
|-------|------|
| `First` | 每帧最早执行的时机，消息更新系统默认挂在这里 |
| `PreUpdate` | Update 之前的准备工作，如输入事件读取、状态过渡 |
| `RunFixedMainLoop` | 驱动固定时间步的主循环，内部会多次调用 `FixedMain` |
| `Update` | **默认的游戏逻辑阶段**，大多数用户系统挂在这里 |
| `SpawnScene` | 场景生成 |
| `PostUpdate` | Update 之后的响应工作，如变换层级同步、动画 |
| `Last` | 每帧最后执行的时机 |

**固定时间步阶段（可能一帧执行 0~N 次）**：
| Label | 用途 |
|-------|------|
| `FixedFirst` / `FixedPreUpdate` / `FixedUpdate` / `FixedPostUpdate` / `FixedLast` | 固定频率的逻辑阶段，物理、AI、网络通常挂在这里 |

### 2.2 MainScheduleOrder —— 可定制的执行顺序

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 213~286 行

```rust
#[derive(Resource, Debug)]
pub struct MainScheduleOrder {
    pub labels: Vec<InternedScheduleLabel>,         // 主循环顺序
    pub startup_labels: Vec<InternedScheduleLabel>, // 启动顺序
}
```

这是一个普通的 `Resource`，意味着**用户代码可以在运行时修改它**。例如，你可以插入自定义的 Schedule 到 `PreUpdate` 和 `Update` 之间。

### 2.3 ScheduleRunnerPlugin —— 无窗口应用的运行器

> 文件：`crates/bevy_app/src/schedule_runner.rs`，第 49~176 行

对于无窗口（headless）应用，`ScheduleRunnerPlugin` 提供了一个基于 `std::thread::sleep` 的主循环：

```rust
pub struct ScheduleRunnerPlugin {
    pub run_mode: RunMode,  // Loop { wait: Option<Duration> } | Once
}
```

`RunMode::Loop` 支持固定帧率限制（通过 `wait` 参数），`RunMode::Once` 只执行一帧。

---

## 三、核心数据结构（第 2 层）

### 3.1 MainScheduleOrder 的默认布局

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 221~236 行

```rust
impl Default for MainScheduleOrder {
    fn default() -> Self {
        Self {
            labels: vec![
                First.intern(),
                PreUpdate.intern(),
                RunFixedMainLoop.intern(),
                Update.intern(),
                SpawnScene.intern(),
                PostUpdate.intern(),
                Last.intern(),
            ],
            startup_labels: vec![PreStartup.intern(), Startup.intern(), PostStartup.intern()],
        }
    }
}
```

注意 `StateTransition` **不在默认列表中**。它的插入由 `StatesPlugin`（在 `bevy_state` crate 中）通过 `MainScheduleOrder::insert_after(PreUpdate, StateTransition)` 和 `insert_startup_before(PreStartup, StateTransition)` 完成。这是一种**插件驱动的调度扩展**模式。

### 3.2 FixedMainScheduleOrder —— 固定时间步子调度

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 348~366 行

```rust
pub struct FixedMainScheduleOrder {
    pub labels: Vec<InternedScheduleLabel>,
}

impl Default for FixedMainScheduleOrder {
    fn default() -> Self {
        Self {
            labels: vec![
                FixedFirst.intern(),
                FixedPreUpdate.intern(),
                FixedUpdate.intern(),
                FixedPostUpdate.intern(),
                FixedLast.intern(),
            ],
        }
    }
}
```

固定时间步有自己的子调度顺序，由 `FixedMain::run_fixed_main` 驱动。

### 3.3 RunFixedMainLoopSystems —— 帧率无关系统的锚点

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 412~492 行

```rust
#[derive(Debug, Hash, PartialEq, Eq, Copy, Clone, SystemSet)]
pub enum RunFixedMainLoopSystems {
    BeforeFixedMainLoop,  // 固定更新前的可变步长逻辑
    FixedMainLoop,        // 内部驱动 FixedMain，不应直接挂系统
    AfterFixedMainLoop,   // 固定更新后的可变步长逻辑（如插值）
}
```

这三个系统集被配置为 `.chain()`，确保 `FixedMainLoop` 在中间顺序执行。这解决了"相机跟随物理"这类需要跨越可变/固定边界的数据依赖问题。

---

## 四、关键行为分析（第 3 层）

### 4.1 Main::run_main —— 主调度的心脏

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 288~306 行

```rust
impl Main {
    pub fn run_main(world: &mut World, mut run_at_least_once: Local<bool>) {
        if !*run_at_least_once {
            world.resource_scope(|world, order: Mut<MainScheduleOrder>| {
                for &label in &order.startup_labels {
                    let _ = world.try_run_schedule(label);
                }
            });
            *run_at_least_once = true;
        }

        world.resource_scope(|world, order: Mut<MainScheduleOrder>| {
            for &label in &order.labels {
                let _ = world.try_run_schedule(label);
            }
        });
    }
}
```

**行为要点**：
1. `run_at_least_once` 是一个 `Local<bool>`，确保 **Startup 序列只在第一次调用时执行**。
2. 使用 `world.resource_scope` 获取 `MainScheduleOrder`，同时保持对 `World` 的可变访问权。
3. `try_run_schedule` 会忽略不存在的 Schedule（返回 `Result`），所以即使某个阶段没有系统，也不会 panic。
4. 所有子 Schedule 都在同一个 `World` 上顺序执行，没有显式 barrier。

### 4.2 FixedMain::run_fixed_main —— 固定时间步的执行

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 390~399 行

```rust
impl FixedMain {
    pub fn run_fixed_main(world: &mut World) {
        world.resource_scope(|world, order: Mut<FixedMainScheduleOrder>| {
            for &label in &order.labels {
                let _ = world.try_run_schedule(label);
            }
        });
    }
}
```

逻辑与 `run_main` 类似，但只跑 `FixedMainScheduleOrder`。它被 `RunFixedMainLoop` 中的系统多次调用。

### 4.3 SubApp::run_default_schedule 与 update

> 文件：`crates/bevy_app/src/sub_app.rs`，第 133~150 行

```rust
pub fn run_default_schedule(&mut self) {
    if self.is_building_plugins() {
        panic!("SubApp::update() was called while a plugin was building.");
    }
    if let Some(label) = self.update_schedule {
        self.world.run_schedule(label);
    }
}

pub fn update(&mut self) {
    self.run_default_schedule();
    self.world.clear_trackers();
}
```

- `run_default_schedule` 执行 `update_schedule` 指定的 Schedule（主 App 默认是 `Main`）。
- `update` 在 `run_default_schedule` 之后调用 `world.clear_trackers()`，**重置所有变更检测标记**。这意味着变更检测（`is_changed()` / `is_added()`）的粒度是**一帧**。

### 4.4 SubApps::update —— 跨世界帧同步

> 文件：`crates/bevy_app/src/sub_app.rs`，第 551~570 行

```rust
pub fn update(&mut self) {
    self.main.run_default_schedule();
    for (_label, sub_app) in self.sub_apps.iter_mut() {
        sub_app.extract(&mut self.main.world);
        sub_app.update();
    }
    self.main.world.clear_trackers();
}
```

**执行顺序的严谨性**：
1. 主世界先完整执行一帧（包括所有主循环阶段）。
2. 对每个子世界：
   a. `extract` —— 读取主世界状态，写入子世界（如渲染数据提取）。
   b. `update` —— 子世界执行自己的 schedule。
3. 最后清除主世界的变更追踪器。

这意味着**子世界的 extract 可以看到主世界本帧的所有变更**，但子世界内部的变更不会影响主世界（除非子世界主动写回）。

### 4.5 ScheduleRunnerPlugin 的主循环

> 文件：`crates/bevy_app/src/schedule_runner.rs`，第 73~175 行

```rust
impl Plugin for ScheduleRunnerPlugin {
    fn build(&self, app: &mut App) {
        let run_mode = self.run_mode;
        app.set_runner(move |mut app: App| {
            // 等待插件 ready，执行 finish/cleanup
            while app.plugins_state() == PluginsState::Adding {
                tick_global_task_pools_on_main_thread();
            }
            app.finish();
            app.cleanup();

            match run_mode {
                RunMode::Once => { app.update(); ... }
                RunMode::Loop { wait } => {
                    loop {
                        let start = Instant::now();
                        app.update();
                        if let Some(exit) = app.should_exit() { return exit; }
                        if let Some(wait) = wait {
                            let elapsed = Instant::now() - start;
                            if elapsed < wait { sleep(wait - elapsed); }
                        }
                    }
                }
            }
        });
    }
}
```

对于桌面平台，`RunMode::Loop` 就是一个简单的 `loop { app.update(); sleep(...); }`。WASM 平台则用 `setTimeout` 实现异步循环。

---

## 五、与上下层的关系

```
用户代码
   │ add_systems(Update, ...)
   ▼
App::add_systems ──→ SubApp::add_systems ──→ World::resource_mut::<Schedules>()
   │                                                          │
   ▼                                                          ▼
App::run() ──→ runner 回调 ──→ loop { app.update() } ──→ SubApps::update()
   │                                                          │
   ▼                                                          ▼
ScheduleRunnerPlugin                                  self.main.run_default_schedule()
（或 WinitPlugin 的事件循环）                              │
                                                          ▼
                                                    World::run_schedule(Main)
                                                          │
                                                          ▼
                                                    Main::run_main(world)
                                                          │
                                    ┌─────────────────────┼─────────────────────┐
                                    ▼                     ▼                     ▼
                            First → PreUpdate      RunFixedMainLoop      Update → ...
                                    │                     │
                                    ▼                     ▼
                           StateTransition       FixedMain (0~N次)
                           (由 StatesPlugin 插入)       │
                                                        ▼
                                                FixedUpdate (物理/AI)
```

---

## 六、设计亮点与潜在陷阱

### 亮点 1：ScheduleLabel 作为"时隙"抽象
Bevy 没有硬编码"先渲染后逻辑"或"先逻辑后渲染"的顺序，而是通过可扩展的 `ScheduleLabel` 列表来定义帧结构。用户和插件都可以插入新的阶段。

### 亮点 2：SingleThreadedExecutor 用于 facilitator schedules
`Main`、`FixedMain`、`RunFixedMainLoop` 这三个"facilitator" schedule 被显式配置为 `SingleThreadedExecutor`：

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 314~319 行

```rust
let mut main_schedule = Schedule::new(Main);
main_schedule.set_executor(SingleThreadedExecutor::new());
```

这是因为它们的主要职责是**按顺序调用子 schedule**，本身没有需要并行的系统。使用单线程执行器避免了多线程调度的开销。

### 亮点 3：固定时间步与可变时间步的显式分离
`Update`（可变步长）和 `FixedUpdate`（固定步长）是两个独立的轨道。`RunFixedMainLoop` 作为"变速齿轮"，根据 `Time<Virtual>` 的累积时间决定一帧内调用多少次 `FixedMain`。这让物理模拟的确定性与 UI 渲染的流畅性互不干扰。

### 潜在陷阱 1：clear_trackers 的时机
变更追踪器在 `SubApp::update()` 末尾清除，而不是在 `run_default_schedule` 之后立刻清除。这意味着：
- 如果你在 `Last` 阶段之后、帧结束之前手动调用 `world.clear_trackers()`，会导致下一帧的 `Added`/`Changed` 检测丢失信息。
- 子世界的 `clear_trackers()` 在各自的 `update()` 中独立执行，子世界可以看到主世界 extract 前的变更，但子世界自己的变更不会传递给主世界。

### 潜在陷阱 2：Startup 只在 Main schedule 第一次运行时触发
`run_at_least_once` 是 `Main::run_main` 的 `Local<bool>`，由 System 自己的本地状态持久化。如果你手动创建一个新的 `World` 并运行 `Main` schedule，`Startup` 会再次执行（因为新 World 没有该 Local 状态）。

### 潜在陷阱 3：StateTransition 的位置依赖
`StatesPlugin` 将 `StateTransition` 插入到 `PreUpdate` 之后。这意味着：
- `First` 阶段的系统看不到本帧的状态变化（状态过渡还没执行）。
- `Update` 阶段的系统可以看到本帧的状态变化。
- 如果你在 `PreUpdate` 里修改了 `NextState`，这个变化会在同帧的 `Update` 之前生效。

---

## 七、关键源码片段

### 7.1 MainSchedulePlugin 的构建逻辑

> 文件：`crates/bevy_app/src/main_schedule.rs`，第 309~344 行

```rust
impl Plugin for MainSchedulePlugin {
    fn build(&self, app: &mut App) {
        let mut main_schedule = Schedule::new(Main);
        main_schedule.set_executor(SingleThreadedExecutor::new());
        let mut fixed_main_schedule = Schedule::new(FixedMain);
        fixed_main_schedule.set_executor(SingleThreadedExecutor::new());
        let mut fixed_main_loop_schedule = Schedule::new(RunFixedMainLoop);
        fixed_main_loop_schedule.set_executor(SingleThreadedExecutor::new());

        app.add_schedule(main_schedule)
            .add_schedule(fixed_main_schedule)
            .add_schedule(fixed_main_loop_schedule)
            .init_resource::<MainScheduleOrder>()
            .init_resource::<FixedMainScheduleOrder>()
            .add_systems(Main, Main::run_main)
            .add_systems(FixedMain, FixedMain::run_fixed_main)
            .configure_sets(
                RunFixedMainLoop,
                (
                    RunFixedMainLoopSystems::BeforeFixedMainLoop,
                    RunFixedMainLoopSystems::FixedMainLoop,
                    RunFixedMainLoopSystems::AfterFixedMainLoop,
                ).chain(),
            );
    }
}
```

### 7.2 ScheduleRunnerPlugin 的 Loop 模式（桌面端）

> 文件：`crates/bevy_app/src/schedule_runner.rs`，第 160~170 行

```rust
loop {
    match tick(&mut app, wait) {
        Ok(Some(delay)) => {
            bevy_platform::thread::sleep(delay);
        }
        Ok(None) => continue,
        Err(exit) => return exit,
    }
}
```

### 7.3 SubApp 的 extract 函数类型

> 文件：`crates/bevy_app/src/sub_app.rs`，第 19 行

```rust
type ExtractFn = Box<dyn FnMut(&mut World, &mut World) + Send>;
```

第一个 `World` 是主世界（数据源），第二个是子世界（目标）。`FnMut` 允许 extract 函数维护内部状态（如缓冲上一次提取的数据）。

---

## 八、关联阅读

- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：App 构建与 Plugin 系统|Bevy-bevy_app-源码解析：App 构建与 Plugin 系统]] —— App 容器与生命周期
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：State 状态机与状态过渡|Bevy-bevy_app-源码解析：State 状态机与状态过渡]] —— `PreUpdate` 中插入的 StateTransition
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度|Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]] —— Schedule 的底层构建与执行
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：World 与 Entity 生命周期|Bevy-bevy_ecs-源码解析：World 与 Entity 生命周期]] —— `clear_trackers` 与变更检测

---

**索引状态**：所属阶段：第一阶段-构建与ECS核心；对应计划笔记：[[Notes/Bevy/00-Bevy全解析主索引#1.3 App 生命周期（bevy_app）|Bevy-bevy_app-源码解析：Startup 与 Update 调度]]
