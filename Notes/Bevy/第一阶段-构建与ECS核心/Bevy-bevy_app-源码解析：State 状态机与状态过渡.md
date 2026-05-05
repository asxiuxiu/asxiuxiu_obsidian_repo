---
title: Bevy-bevy_app-源码解析：State 状态机与状态过渡
date: 2026-05-03
tags:
  - bevy-source
  - engine-architecture
  - ecs
  - bevy_app
  - bevy_state
  - state-machine
aliases:
  - "Bevy bevy_app State 状态机与状态过渡"
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy全解析主索引]]

## 一、模块定位

**物理路径**：`crates/bevy_state/src/`

> 注：State 系统的核心实现已从早期版本的 `bevy_app/src/state/` 迁移到独立的 `bevy_state` crate 中，但通过 `bevy_app` 的 `AppExtStates` trait 和 `StatesPlugin` 与 App 生命周期深度集成。笔记标题遵循索引约定。

**Cargo.toml 关键信息**：
- 版本：`0.19.0-dev`
- 核心依赖：`bevy_ecs`（ECS 核心）、`bevy_state_macros`（`States`/`SubStates` 派生宏）、`bevy_utils`
- 可选依赖：`bevy_app`（App 集成，默认启用）、`bevy_reflect`（反射支持）

**依赖深度**：`bevy_state` 位于 **bevy_app 之上、上层 gameplay 模块之下**。它依赖 `bevy_ecs` 的 Resource、Schedule、System 机制，并通过 `bevy_app` 的 `MainScheduleOrder` 将 `StateTransition` schedule 嵌入到标准帧循环中。

**典型用途**：
- 管理游戏的全局阶段：`MainMenu`、`Loading`、`InGame`、`Paused`
- 驱动菜单切换、加载画面、暂停逻辑
- 通过 `OnEnter`/`OnExit` 自动触发生命周期系统（如进入游戏时生成玩家，退出时销毁）

---

## 二、公共接口梳理（第 1 层）

### 2.1 核心 trait：States 与 FreelyMutableState

> 文件：`crates/bevy_state/src/state/states.rs`，第 64~69 行

```rust
pub trait States: 'static + Send + Sync + Clone + PartialEq + Eq + Hash + Debug {
    const DEPENDENCY_DEPTH: usize = 1;
}
```

`States` 是状态类型的核心约束。任何可作为状态机的类型都必须实现它（通常通过 `#[derive(States)]` 自动派生）。`DEPENDENCY_DEPTH` 用于排序和防止 `ComputedStates` 的循环依赖。

> 文件：`crates/bevy_state/src/state/freely_mutable_state.rs`，第 15~47 行

```rust
pub trait FreelyMutableState: States {
    fn register_state(schedule: &mut Schedule) {
        // 注册 ApplyStateTransition、ExitSchedules、TransitionSchedules、EnterSchedules
        // 四个系统集和对应的系统到 StateTransition schedule
    }
}
```

`FreelyMutableState` 是用户直接操作的状态类型（通过 `NextState<S>` 修改）。`ComputedStates` 和 `SubStates` 不实现此 trait，因为它们的变化由源状态驱动。

### 2.2 核心资源：State / NextState / PreviousState

> 文件：`crates/bevy_state/src/state/resources.rs`，第 52~92 行

```rust
#[derive(Resource, Debug)]
pub struct State<S: States>(pub(crate) S);

impl<S: States> State<S> {
    pub fn new(state: S) -> Self { Self(state) }
    pub fn get(&self) -> &S { &self.0 }
}
```

- **`State<S>`**：当前状态的只读 Resource。通过 `Res<State<MyState>>` 在系统中读取。
- **`NextState<S>`**：下一帧的待切换状态。通过 `ResMut<NextState<MyState>>` 写入，在 `StateTransition` schedule 中被消费。
- **`PreviousState<S>`**：上一次过渡前的状态。只在第一次状态变化后才被插入到 World 中。

> 文件：`crates/bevy_state/src/state/resources.rs`，第 175~191 行

```rust
pub enum NextState<S: FreelyMutableState> {
    #[default]
    Unchanged,
    Pending(S),         // 即使目标与当前相同，也触发过渡
    PendingIfNeq(S),    // 仅在目标与当前不同时触发过渡
}
```

### 2.3 过渡调度标签：OnEnter / OnExit / OnTransition

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 18~39 行

```rust
#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash, Default)]
pub struct OnEnter<S: States>(pub S);

#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash, Default)]
pub struct OnExit<S: States>(pub S);

#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash, Default)]
pub struct OnTransition<S: States> {
    pub exited: S,
    pub entered: S,
}
```

这三个都是**参数化的 ScheduleLabel**。`OnEnter(MainMenu)` 和 `OnEnter(InGame)` 是两个完全不同的 schedule。这意味着 Bevy 会为每个状态的进入/退出创建独立的 schedule，而非在运行时做条件判断。

### 2.4 AppExtStates —— 面向用户的便捷 API

> 文件：`crates/bevy_state/src/app.rs`，第 22~82 行

```rust
pub trait AppExtStates {
    fn init_state<S: FreelyMutableState + FromWorld>(&mut self) -> &mut Self;
    fn insert_state<S: FreelyMutableState>(&mut self, state: S) -> &mut Self;
    fn add_computed_state<S: ComputedStates>(&mut self) -> &mut Self;
    fn add_sub_state<S: SubStates>(&mut self) -> &mut Self;
}
```

- `init_state::<S>()`：使用 `S::default()` 作为初始值，幂等（重复调用无效果）。
- `insert_state(state)`：显式指定初始值，会覆盖已有状态。
- `add_computed_state` / `add_sub_state`：注册派生状态类型。

---

## 三、核心数据结构（第 2 层）

### 3.1 StateTransition schedule 的四阶段结构

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 77~91 行

```rust
#[derive(SystemSet, Clone, Debug, PartialEq, Eq, Hash)]
pub enum StateTransitionSystems {
    DependentTransitions,   // 应用 NextState，计算 ComputedStates/SubStates
    ExitSchedules,          // 按叶子→根顺序运行 OnExit
    TransitionSchedules,    // 运行 OnTransition
    EnterSchedules,         // 按根→叶子顺序运行 OnEnter
}
```

这四个阶段被配置为 **`.chain()`**，确保严格顺序：

```
DependentTransitions → ExitSchedules → TransitionSchedules → EnterSchedules
```

### 3.2 StatesPlugin 对主调度的注入

> 文件：`crates/bevy_state/src/app.rs`，第 311~322 行

```rust
impl Plugin for StatesPlugin {
    fn build(&self, app: &mut App) {
        let mut schedule = app.world_mut().resource_mut::<MainScheduleOrder>();
        schedule.insert_after(PreUpdate, StateTransition);
        schedule.insert_startup_before(PreStartup, StateTransition);
        setup_state_transitions_in_world(app.world_mut());
    }
}
```

`StatesPlugin` 做三件事：
1. 在 `PreUpdate` **之后**插入 `StateTransition`（确保 `First` 和 `PreUpdate` 系统看不到本帧的状态变化）。
2. 在 `PreStartup` **之前**插入 `StateTransition`（确保 `OnEnter(初始状态)` 在 `PreStartup` 之前运行）。
3. 初始化 `StateTransition` schedule 本身（创建四个阶段系统集）。

### 3.3 状态系统的注册模式

每个状态类型 `S` 在被 `init_state` 或 `insert_state` 时，都会调用 `S::register_state(schedule)` 向 `StateTransition` schedule 注册一组系统：

> 文件：`crates/bevy_state/src/state/freely_mutable_state.rs`，第 27~46 行

```rust
fn register_state(schedule: &mut Schedule) {
    schedule.configure_sets((
        ApplyStateTransition::<Self>::default().in_set(StateTransitionSystems::DependentTransitions),
        ExitSchedules::<Self>::default().in_set(StateTransitionSystems::ExitSchedules),
        TransitionSchedules::<Self>::default().in_set(StateTransitionSystems::TransitionSchedules),
        EnterSchedules::<Self>::default().in_set(StateTransitionSystems::EnterSchedules),
    ));

    schedule.add_systems(apply_state_transition::<Self>.in_set(...))
           .add_systems(last_transition::<Self>.pipe(run_exit::<Self>).in_set(...))
           .add_systems(last_transition::<Self>.pipe(run_transition::<Self>).in_set(...))
           .add_systems(last_transition::<Self>.pipe(run_enter::<Self>).in_set(...));
}
```

这里使用了 **`.pipe()`** 组合子：`last_transition` 读取最新的 `StateTransitionEvent`，将其输出通过 `In<T>` 传递给 `run_exit`/`run_transition`/`run_enter`。

---

## 四、关键行为分析（第 3 层）

### 4.1 状态过渡的完整执行链路

假设当前状态是 `A`，某系统在 `Update` 中调用了 `next_state.set(B)`：

**第 1 步：NextState 被写入**
`ResMut<NextState<MyState>>` 被修改，`NextState::Pending(B)` 被设置。

**第 2 步：StateTransition schedule 执行（在 PreUpdate 之后）**

> 文件：`crates/bevy_state/src/state/freely_mutable_state.rs`，第 49~70 行

```rust
fn apply_state_transition<S: FreelyMutableState>(
    event: MessageWriter<StateTransitionEvent<S>>,
    commands: Commands,
    current_state: Option<ResMut<State<S>>>,
    previous_state: Option<ResMut<PreviousState<S>>>,
    next_state: Option<ResMut<NextState<S>>>,
) {
    let Some((next_state, allow_same_state_transitions)) = take_next_state(next_state) else { return; };
    let Some(current_state) = current_state else { return; };
    internal_apply_state_transition(event, commands, Some(current_state), previous_state, Some(next_state), allow_same_state_transitions);
}
```

`take_next_state` 通过 `bypass_change_detection()` 取出 `NextState` 的值并将其重置为 `Unchanged`，同时返回 `allow_same_state_transitions` 标志。

**第 3 步：internal_apply_state_transition 更新资源**

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 138~210 行

```rust
pub(crate) fn internal_apply_state_transition<S: States>(
    mut event: MessageWriter<StateTransitionEvent<S>>,
    mut commands: Commands,
    current_state: Option<ResMut<State<S>>>,
    mut previous_state: Option<ResMut<PreviousState<S>>>,
    new_state: Option<S>,
    allow_same_state_transitions: bool,
) {
    match new_state {
        Some(entered) => {
            match current_state {
                Some(mut state_resource) => {
                    let exited = mem::replace(&mut state_resource.0, entered.clone());
                    event.write(StateTransitionEvent { exited: Some(exited.clone()), entered: Some(entered), allow_same_state_transitions });
                    if let Some(ref mut previous_state) = previous_state {
                        previous_state.0 = exited;
                    } else {
                        commands.insert_resource(PreviousState(exited));
                    }
                }
                None => {
                    commands.insert_resource(State(entered.clone()));
                    event.write(StateTransitionEvent { exited: None, entered: Some(entered), allow_same_state_transitions });
                }
            }
        }
        None => { /* 移除 State<S> 资源，发送 exited 事件 */ }
    }
}
```

关键点：
- 使用 `mem::replace` 原子地替换 `State<S>` 的内部值。
- 写入 `StateTransitionEvent` 消息（可供后续系统读取）。
- 更新或创建 `PreviousState<S>`。

**第 4 步：ExitSchedules 运行 OnExit(A)**
`last_transition` 读取最新消息，确认 exited 状态是 `A`，调用 `world.try_run_schedule(OnExit(A))`。

**第 5 步：TransitionSchedules 运行 OnTransition { exited: A, entered: B }**

**第 6 步：EnterSchedules 运行 OnEnter(B)**

**第 7 步：后续 schedule（Update、PostUpdate 等）在新状态下执行**

### 4.2 同状态过渡（Identity Transition）的处理

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 250~252 行

```rust
if transition.entered == transition.exited && !transition.allow_same_state_transitions {
    return;  // 跳过 OnExit / OnEnter
}
```

- `NextState::Pending(A)` 当当前状态也是 `A` 时：
  - `State<S>` 的值不变（`mem::replace` 后发现相等，直接 clone）。
  - `StateTransitionEvent` 仍然被发送。
  - `OnExit` 和 `OnEnter` **仍然运行**（因为 `allow_same_state_transitions = true`）。
- `NextState::PendingIfNeq(A)` 当当前状态也是 `A` 时：
  - `allow_same_state_transitions = false`。
  - `OnExit` 和 `OnEnter` **被跳过**，但 `OnTransition { A→A }` 仍然运行（如果注册了的话）。

### 4.3 ComputedStates 的派生机制

`ComputedStates` 是一种**只读状态**，其值由 `compute()` 方法从源状态派生：

```rust
pub trait ComputedStates: States {
    type SourceStates: StateSet;
    fn compute(sources: Self::SourceStates) -> Option<Self>;
    const ALLOW_SAME_STATE_TRANSITIONS: bool = false;
}
```

当源状态变化时，`ComputedStates` 的注册系统会自动调用 `compute()`，如果结果变化，则触发自己的 `OnEnter`/`OnExit`。如果 `compute()` 返回 `None`，`ComputedState` 资源会被从 World 中移除。

### 4.4 SubStates 的存在性控制

`SubStates` 类似于 `ComputedStates`，但它是**可手动修改的**：

```rust
pub trait SubStates: States + FreelyMutableState {
    type SourceStates: StateSet;
    fn should_exist(sources: Self::SourceStates) -> Option<Self>;
}
```

- 当源状态不满足条件时，`State<SubState>` 资源被移除，SubState 的 `OnExit` 被触发。
- 当源状态满足条件时，`State<SubState>` 被创建（使用默认值），`OnEnter` 被触发。
- 在存在期间，可以通过 `NextState<SubState>` 自由修改其值。

---

## 五、与上下层的关系

```
用户代码
   │ init_state::<GameState>()
   ▼
AppExtStates::init_state ──→ SubApp::init_resource::<State<S>>()
   │                         SubApp::init_resource::<NextState<S>>()
   │                         S::register_state(StateTransition schedule)
   ▼
StatesPlugin::build ──→ MainScheduleOrder::insert_after(PreUpdate, StateTransition)
                        MainScheduleOrder::insert_startup_before(PreStartup, StateTransition)
   │
   ▼
App::update() ──→ Main::run_main ──→ ... → PreUpdate → StateTransition → Update → ...
                                          │
                                          ▼
                              DependentTransitions (apply_state_transition)
                                          │
                                          ▼
                              ExitSchedules (OnExit)
                                          │
                                          ▼
                              TransitionSchedules (OnTransition)
                                          │
                                          ▼
                              EnterSchedules (OnEnter)
```

---

## 六、设计亮点与潜在陷阱

### 亮点 1：ScheduleLabel 参数化
`OnEnter(S)` 和 `OnExit(S)` 不是两个通用 schedule，而是**每个状态值对应一个独立 schedule**。这让调度器可以在构建阶段就知道哪些系统属于哪个状态过渡，无需运行时条件判断。

### 亮点 2：Message（事件）驱动过渡通知
状态变化通过 `StateTransitionEvent<S>` 消息广播，而非直接回调。这允许任意系统（包括后续帧的系统）读取过渡历史。`last_transition` 函数使用 `MessageReader` 获取最新事件。

### 亮点 3：四阶段严格顺序
`DependentTransitions → Exit → Transition → Enter` 的链式执行确保了：
- 所有状态值在 `Exit` 之前已经更新。
- `OnExit` 可以看到 `PreviousState`（即刚离开的状态）。
- `OnEnter` 可以看到新的 `State` 资源。

### 亮点 4：ComputedStates 与 SubStates 的层级依赖
通过 `DEPENDENCY_DEPTH` 和 `StateSet` 的拓扑排序，Bevy 确保父状态先于子状态/计算状态过渡。测试用例验证了退出顺序是"叶子→根"，进入顺序是"根→叶子"。

### 潜在陷阱 1：StatesPlugin 必须先于 init_state
如果先调用 `app.init_state::<S>()` 再 `app.add_plugins(StatesPlugin)`，`StateTransition` schedule 尚未被添加到 `MainScheduleOrder`，导致 `register_state` 时找不到 schedule 而 panic。

### 潜在陷阱 2：OnExit 中无法读取 NextState
`OnExit` 运行时，`NextState` 已经被 `take_next_state` 重置为 `Unchanged`。如果需要在退出时知道"要进入哪个状态"，应该读取 `StateTransitionEvent` 消息。

### 潜在陷阱 3：SubStates 的 NextState 在不存在时无效
向 `NextState<SubState>` 写入值时，如果源状态不满足 `should_exist` 条件，SubState 的资源根本不存在。`apply_state_transition` 会因为 `current_state` 为 `None` 而跳过（不会 panic，但也不会生效）。

### 潜在陷阱 4：同状态过渡的默认行为差异
`next_state.set(A)` 和 `next_state.set_if_neq(A)` 在目标等于当前时的行为不同：前者触发 `OnEnter`/`OnExit`，后者跳过。这可能导致看似"无变化"的状态写入产生副作用。

---

## 七、关键源码片段

### 7.1 init_state 的初始化流程

> 文件：`crates/bevy_state/src/app.rs`，第 94~118 行

```rust
fn init_state<S: FreelyMutableState + FromWorld>(&mut self) -> &mut Self {
    if !self.world().contains_resource::<State<S>>() {
        self.init_resource::<State<S>>()
            .init_resource::<NextState<S>>()
            .add_message::<StateTransitionEvent<S>>();
        let schedule = self.get_schedule_mut(StateTransition).expect(
            "The `StateTransition` schedule is missing. Did you forget to add StatesPlugin..."
        );
        S::register_state(schedule);
        let state = self.world().resource::<State<S>>().get().clone();
        self.world_mut().write_message(StateTransitionEvent {
            exited: None,
            entered: Some(state),
            allow_same_state_transitions: true,
        });
        enable_state_scoped_entities::<S>(self);
    }
    self
}
```

### 7.2 状态过渡四阶段的 schedule 配置

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 217~234 行

```rust
pub fn setup_state_transitions_in_world(world: &mut World) {
    let mut schedules = world.get_resource_or_init::<Schedules>();
    if schedules.contains(StateTransition) { return; }
    let mut schedule = Schedule::new(StateTransition);
    schedule.configure_sets((
        StateTransitionSystems::DependentTransitions,
        StateTransitionSystems::ExitSchedules,
        StateTransitionSystems::TransitionSchedules,
        StateTransitionSystems::EnterSchedules,
    ).chain());
    schedules.insert(schedule);
}
```

### 7.3 run_enter 的条件判断

> 文件：`crates/bevy_state/src/state/transitions.rs`，第 243~258 行

```rust
pub(crate) fn run_enter<S: States>(
    transition: In<Option<StateTransitionEvent<S>>>,
    world: &mut World,
) {
    let Some(transition) = transition.0 else { return; };
    if transition.entered == transition.exited && !transition.allow_same_state_transitions {
        return;
    }
    let Some(entered) = transition.entered else { return; };
    let _ = world.try_run_schedule(OnEnter(entered));
}
```

### 7.4 子状态/计算状态的退出→进入顺序验证

> 文件：`crates/bevy_state/src/state/mod.rs`，第 962~976 行（测试断言）

```rust
let transitions = &world.resource::<TransitionTracker>().0;
assert_eq!(transitions.len(), 9);
assert_eq!(transitions[0], "computed exit");
assert_eq!(transitions[1], "sub exit");
assert_eq!(transitions[2], "simple exit");
// Transition order is arbitrary
assert_eq!(transitions[6], "simple enter");
assert_eq!(transitions[7], "sub enter");
assert_eq!(transitions[8], "computed enter");
```

这验证了**退出从叶子到根**（computed → sub → simple），**进入从根到叶子**（simple → sub → computed）。

---

## 八、关联阅读

- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：App 构建与 Plugin 系统|Bevy-bevy_app-源码解析：App 构建与 Plugin 系统]] —— `StatesPlugin` 的注册方式
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_app-源码解析：Startup 与 Update 调度|Bevy-bevy_app-源码解析：Startup 与 Update 调度]] —— `StateTransition` 在 `MainScheduleOrder` 中的位置
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度|Bevy-bevy_ecs-源码解析：Schedule 与 System 并行调度]] —— `.pipe()` 组合子与系统集链式配置
- [[Notes/Bevy/第一阶段-构建与ECS核心/Bevy-bevy_ecs-源码解析：Event 与 Commands 延迟执行|Bevy-bevy_ecs-源码解析：Event 与 Commands 延迟执行]] —— `Message`（事件）机制

---

**索引状态**：所属阶段：第一阶段-构建与ECS核心；对应计划笔记：[[Notes/Bevy/00-Bevy全解析主索引#1.3 App 生命周期（bevy_app）|Bevy-bevy_app-源码解析：State 状态机与状态过渡]]
