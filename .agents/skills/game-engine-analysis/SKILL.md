---
name: game-engine-analysis
description: 当用户请求分析 chaos 引擎、Unreal Engine（UE）、Bevy 引擎、Piccolo 引擎或 proven_ground 游戏项目的源码、进行源码级架构解析、或生成引擎模块分析笔记时触发。提供系统化的三层剥离法（接口层→数据层→逻辑层）分析工作流，支持多引擎适配，指导 AI 对大型代码库进行高效、可持续的源码级解析，并产出结构化的分析笔记。
---

# 游戏引擎源码分析

系统化源码分析工作流，用于对 chaos 引擎、Unreal Engine、Bevy 引擎及 wolfgang/proven_ground 游戏项目的大型代码库进行模块级架构解析。

## 支持的引擎

| 引擎 | 语言 | 对象模型 | 构建系统 | 典型用途 |
|------|------|---------|---------|---------|
| **chaos** | C++ | OOP + 自定义 ECS | CMake | 中型商业引擎 |
| **Unreal Engine** | C++ | UObject 继承树 | UBT (.Build.cs) | 巨型工业级引擎 |
| **Bevy** | Rust | 原生 ECS (Entity+Component+System) | Cargo | 现代开源 ECS 引擎 |
| **Piccolo** | C++ | OOP + 组件变体 | CMake + 自定义预编译 | 小型教学引擎 (GAMES104) |

## 触发条件

- 用户请求分析 chaos / UE / Unreal Engine / Bevy / Piccolo 的源码
- 用户提到"三层剥离法"或"源码级架构解析"
- 用户请求"生成游戏引擎源码分析笔记"且明确提到上述引擎之一
- 用户要求"阅读/解析某个开源项目的代码"且该项目为上述引擎之一

## 环境配置

本 skill 通过项目根目录下的 **`.engine-source-config`** 文件动态读取各引擎源码路径。

### 配置文件说明

- **文件位置**：`<vault-root>/.engine-source-config`
- **已加入 `.gitignore`**，不会被提交到 git，方便在不同平台配置不同路径
- **格式**：简单的 `KEY=value`，每行一个配置项，以 `#` 开头的行为注释

### 配置示例

```
# 引擎源码根目录配置
CHAOS_SOURCE_ROOT=/path/to/chaos
UE_SOURCE_ROOT=/Users/asmallgod/Documents
BEVY_SOURCE_ROOT=/path/to/bevy
```

### AI 读取规范

每次分析源码时，**必须首先读取** `<vault-root>/.engine-source-config` 文件，提取对应引擎的 `*_SOURCE_ROOT` 值作为源码根目录。若文件不存在或该变量未设置，**必须提示用户配置**（可引用上述示例），禁止直接回退到任何默认路径。

> 下文中所有涉及源码根目录的地方，均用 `<CHAOS_SOURCE_ROOT>`、`<UE_SOURCE_ROOT>`、`<BEVY_SOURCE_ROOT>` 占位符表示实际读取到的路径。

## 核心方法论：三层剥离法

分析必须按以下三层递进，**严禁未读完公共接口就深入实现细节**。

### 第 1 层：接口层（What）
**目标**：建立模块地图，理解对外暴露的能力边界。

- 读取模块构建定义（`CMakeLists.txt` / `.Build.cs` / `Cargo.toml`），了解目标名、依赖项、源文件列表
- 阅读对外暴露的公共接口（`include/` / `Public/` / `Classes/` / `src/lib.rs` / `src/*.rs`）
- 识别反射/元数据机制（UHT 宏 / Rust derive 宏 / 手动注册）
- 用 `Grep` 查找其他模块引用该模块接口的地方，确认使用方式
- 列出核心类和关键 public 方法

### 第 2 层：数据层（How - Structure）
**目标**：理解核心数据结构、内存布局、状态流转。

- 找到核心类/结构体/组件（关注命名空间、crate、module）
- 分析成员变量之间的关系（ownership、引用、池化索引、Archetype 存储）
- 追踪关键数据的生命周期：创建 → 使用 → 销毁
- 标注内存分配来源（栈、堆、mempool、GPU memory、ECS World、Arena）
- 对于 ECS 引擎（Bevy），重点分析 Component 存储模型（Sparse Set / Archetype / Table）

### 第 3 层：逻辑层（How - Behavior）
**目标**：理解关键算法的执行流程和动态行为。

- 选取 2~3 个最核心的成员函数/System，逐行追踪调用链
- 分析多线程场景下的同步策略
  - chaos：线程池、任务图、锁
  - UE：Game Thread、Render Thread、RHI Thread、Async Loading Thread
  - Bevy：System 并行调度、Read/Write 依赖图、task pool
- 分析性能关键路径上的优化手段
- 标注与上下层模块的交互点（回调、事件、命令缓冲、ECS Event/Command）

## 引擎适配层

三层剥离法是通用方法论，以下按引擎补充特定约定。

### chaos 适配

| 维度 | 约定 |
|------|------|
| 模块定义 | `CMakeLists.txt`（目标名、依赖项、源文件列表） |
| 公共接口 | `include/*.hpp` 或模块根目录头文件 |
| 实现文件 | `.hpp` + `.cpp` |
| 反射/元数据 | 手动注册，无代码生成 |
| 对象模型 | OOP + 自定义 ECS |

### UE 适配

| 维度 | 约定 |
|------|------|
| 模块定义 | `*.Build.cs`（`PublicDependencyModuleNames`、`PrivateDependencyModuleNames`） |
| 公共接口 | `Public/*.h`、`Classes/*.h` |
| 实现文件 | `Private/*.cpp` |
| 反射/元数据 | UHT（Unreal Header Tool）生成 `.generated.h`，以原始 `.h` 为准 |
| 对象模型 | UObject 继承树，关注 `UCLASS`/`USTRUCT`/`UFUNCTION`/`UPROPERTY` |

> **UE 特别提示**：`.generated.h` 是 UHT 生成的，分析时以原始 `.h` 文件为准，`.generated.h` 仅用于理解 UHT 注入的反射代码。

**UE 关键目录结构**：
```
Engine/Source/<分组>/<模块名>/
├── <模块名>.Build.cs        # 模块定义
├── Public/                   # 对外公共头文件
├── Classes/                  # UObject 类声明（旧约定，仍有大量遗留）
├── Internal/                 # 模块间内部接口
└── Private/                  # 实现文件
```

常用源码根路径：
- `Engine/Source/Runtime/` — 核心运行时（Core、CoreUObject、Engine、RenderCore、RHI 等）
- `Engine/Source/Editor/` — 编辑器模块
- `Engine/Source/Programs/` — UBT、UHT、UnrealPak 等工具
- `Engine/Source/Developer/` — 开发工具与中间件

### Bevy 适配

| 维度 | 约定 |
|------|------|
| 模块定义 | `Cargo.toml`（workspace member、dependencies、features） |
| 公共接口 | `crates/<crate-name>/src/lib.rs`、`src/*.rs` 中 `pub` 导出的 API |
| 实现文件 | `src/*.rs`（Rust 无头文件/实现文件分离） |
| 反射/元数据 | Rust derive 宏（`#[derive(Component)]`、`#[derive(Resource)]`） |
| 对象模型 | 原生 ECS：Entity（ID）+ Component（数据）+ System（逻辑）+ Resource（全局状态） |

**Bevy 关键目录结构**：
```
bevy/
├── crates/                   # workspace 成员 crate
│   ├── bevy_ecs/             # ECS 核心（Entity、Component、System、Schedule、World）
│   ├── bevy_app/             # App 生命周期、Plugin 系统
│   ├── bevy_asset/           # 资源加载与管理
│   ├── bevy_render/          # 渲染管线
│   └── ...
├── Cargo.toml                # workspace 定义
└── examples/                 # 官方示例
```

**Bevy 特别提示**：
- Bevy 是 ECS 原生引擎，分析时**不要以 OOP 思维寻找"类"**，而是关注：
  - `Component` 是什么数据？存储在哪个 `Storage` 中？
  - `System` 如何访问 `Query<&ComponentA, &mut ComponentB>`？
  - `Schedule` 如何基于 `SystemParam` 的读写依赖构建并行图？
  - `World` 是唯一的全局状态容器，`Commands` 是延迟执行的变更队列
- Bevy 大量使用 Rust trait 和泛型，分析时要关注 `trait System`、`trait Component`、`trait Resource` 的约束条件

### Piccolo 适配

| 维度 | 约定 |
|------|------|
| 模块定义 | `CMakeLists.txt`（编译目标、源文件列表、依赖项） |
| 公共接口 | `engine/source/<模块>/*.h` |
| 实现文件 | `engine/source/<模块>/*.cpp` |
| 反射/元数据 | 自定义 `meta_parser` 预编译生成 |
| 对象模型 | OOP + 组件变体 |

**Piccolo 核心信息**：

| 项目 | 值 |
|------|-----|
| **源码路径** | `D:/workspace/Piccolo`（固定路径，无需 `.engine-source-config`） |
| **渲染 API** | Vulkan |
| **编辑器 UI** | ImGui |
| **物理引擎** | JoltPhysics |
| **脚本系统** | Lua (sol2 绑定) |

**Piccolo 分析路径**：
```
构建系统(1) → 编辑器入口(2) → 运行时核心(3) → 游戏框架(4) → 渲染管线(5) → 物理/输入/资源(6/7)
```

> Piccolo 虽小但五脏俱全，分析遵循"由外向内、先广后深"原则，禁止在未理解上层启动链路的情况下直接深入某个 Pass 或 Component 的实现细节。

## 工具使用策略

| 任务类型 | 推荐工具组合 |
|---------|-------------|
| **模块定位** | `Glob` + `Grep` |
| **接口梳理** | `ReadFile`（公共 API 文件）+ `Grep`（引用点） |
| **调用链追踪** | `Grep`（函数定义）+ `ReadFile`（函数体） |
| **跨模块关系** | `Agent(subagent_type="explore")` |
| **大型目录扫描** | `Shell` (`tree` / `dir`) |
| **批量生成笔记** | `WriteFile` / `StrReplaceFile` |

### Explore Agent 触发条件

满足以下任一条件时启动 `subagent_type="explore"`：
1. 目标模块源码文件超过 20 个（chaos/Bevy）或 30 个（UE），调用关系复杂
2. 需要追踪一个接口在 3 个以上不同模块中的实现/重写
3. 对某个子系统完全没有概念，需要快速建立认知
4. 怀疑存在某种设计模式，需要跨文件确认

**Prompt 模板（通用）**：
```
请对 <引擎源码路径>/<模块路径> 进行 read-only 探索。
重点关注：
1. 公共接口头文件/导出模块有哪些？
2. 核心类/结构体/组件的命名和职责
3. 依赖哪些下层模块？被哪些上层模块依赖？
4. 是否存在代码生成、配置文件或宏驱动的机制？
返回简洁概览：目录结构、关键文件名、核心类列表、设计模式推测。
```

### Coder Agent 触发条件

当涉及以下任务时启动 `subagent_type="coder"`：
- 将大量零散发现整理为一篇连贯的笔记
- 绘制 Mermaid 类图/流程图/时序图
- 编写辅助分析脚本

## 笔记产出规范

> **注意**：产出源码分析笔记时，必须结合使用 `/obsidian-markdown` skill，确保语法规范。叙述质量需符合 `note-refine` skill 的费曼原则（零假定、Why 先行、问题链驱动等）。本 skill 聚焦源码级分析（不脱敏），不重复定义叙事规范。

### 引擎对应的输出目录与文件名

| 引擎 | 输出目录 | 文件名格式 | 索引文件 |
|------|---------|-----------|---------|
| **chaos** | `Game/` | `<模块>-源码解析：<主题>.md` | `Game/00-引擎与游戏全解析主索引.md` |
| **UE** | `Notes/UE/` | `UE-<模块>-源码解析：<主题>.md` | `Notes/UE/00-UE全解析主索引.md` |
| **Bevy** | `Notes/Bevy/` | `Bevy-<模块>-源码解析：<主题>.md` | `Notes/Bevy/00-Bevy全解析主索引.md` |
| **Piccolo** | `Notes/Piccolo/` | `<模块>-源码解析：<主题>.md` | `Notes/Piccolo/索引.md` |

专题笔记文件名格式：
- chaos：`专题：<主题>.md`
- UE：`UE-专题：<主题>.md`
- Bevy：`Bevy-专题：<主题>.md`
- Piccolo：`专题：<主题>.md`

> **索引对齐要求**：若索引中已存在该模块的 wikilink 名称，产出的笔记标题、文件名、aliases 必须与索引表格中的计划笔记保持一致，确保双向链接可自动建立。

### UE 阶段子目录

| 阶段 | 子目录名 |
|------|---------|
| 第一阶段 | `Notes/UE/第一阶段-构建系统/` |
| 第二阶段 | `Notes/UE/第二阶段-基础层/` |
| 第三阶段 | `Notes/UE/第三阶段-核心层/` |
| 第四阶段 | `Notes/UE/第四阶段-客户端运行时层/` |
| 第五阶段 | `Notes/UE/第五阶段-编辑器层/` |
| 第六阶段 | `Notes/UE/第六阶段-网络与服务器后端/` |
| 第七阶段 | `Notes/UE/第七阶段-工具链与插件/` |
| 第八阶段 | `Notes/UE/第八阶段-跨领域专题/` |

### Bevy 阶段子目录（建议）

| 阶段 | 子目录名 |
|------|---------|
| 第一阶段 | `Notes/Bevy/第一阶段-构建与ECS核心/` |
| 第二阶段 | `Notes/Bevy/第二阶段-App生命周期与插件/` |
| 第三阶段 | `Notes/Bevy/第三阶段-渲染管线/` |
| 第四阶段 | `Notes/Bevy/第四阶段-资源与资产管理/` |
| 第五阶段 | `Notes/Bevy/第五阶段-输入与窗口/` |
| 第六阶段 | `Notes/Bevy/第六阶段-跨领域专题/` |

> 若 `Notes/Bevy/00-Bevy全解析主索引.md` 中已定义阶段划分，以索引为准。

### Piccolo 阶段子目录（建议）

| 阶段 | 子目录名 |
|------|---------|
| 第一阶段 | `Notes/Piccolo/第一阶段-构建系统/` |
| 第二阶段 | `Notes/Piccolo/第二阶段-编辑器与框架/` |
| 第三阶段 | `Notes/Piccolo/第三阶段-运行时核心/` |
| 第四阶段 | `Notes/Piccolo/第四阶段-渲染管线/` |
| 第五阶段 | `Notes/Piccolo/第五阶段-物理与输入/` |
| 第六阶段 | `Notes/Piccolo/第六阶段-资源与脚本/` |
| 第七阶段 | `Notes/Piccolo/第七阶段-跨领域专题/` |

> 若 `Notes/Piccolo/索引.md` 中已定义阶段划分，以索引为准。

### 笔记模板

完整模板参见 [references/note-template.md](references/note-template.md)。生成笔记时必须包含：
- frontmatter（title, date, tags, aliases）
- 返回索引的导航链接
- 模块定位、接口梳理、数据结构、行为分析、上下层关系、设计亮点、关键源码片段、关联阅读

**Piccolo 特有要求**：每篇 Piccolo 分析笔记必须包含 **"设计亮点与可迁移原理"** 章节：
- 从 Piccolo 的实现中提取 1~2 个可以迁移到自研引擎的通用设计模式
- 这个设计在小型引擎里为什么够用？如果要扩展成大型引擎，需要做什么改造？
- 对照 `Notes/SelfGameEngine/` 中的已有设计，指出可借鉴或需修正的地方
- 如果发现 Piccolo 的某个设计恰好解决了 `SelfGameEngine` 中悬而未决的问题，这是最高优先级的产出

### 代码引用规范

- 必须标明**文件路径**（相对于对应引擎的 `*_SOURCE_ROOT`）和**行号范围**
- 只引用最关键的部分，避免大段复制无关代码
- 对引用代码添加中文注释说明
- 遇到代码生成时，备注生成来源（如 `"由 UHT 生成"`、`"由 Rust derive 宏展开"`）

示例格式（chaos）：
```markdown
> 文件：`chaos/base/String.hpp`，第 45~62 行

```cpp
class String {
public:
    // 默认使用全局内存池分配器
    String(Allocator* alloc = GetDefaultAllocator());
```
```

示例格式（UE）：
```markdown
> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h`，第 120~145 行

```cpp
UCLASS(abstract, config=Engine, BlueprintType)
class COREUOBJECT_API UObject : public UObjectBaseUtility
{
    GENERATED_BODY()
public:
    // UObject 的外层容器，决定生命周期和序列化范围
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Object")
    UObject* Outer;
```
```

示例格式（Bevy）：
```markdown
> 文件：`crates/bevy_ecs/src/system/system.rs`，第 45~62 行

```rust
// System trait：所有可运行的逻辑都必须实现此 trait
pub trait System: Send + Sync + 'static {
    fn run(&mut self, world: &mut World);
}
```
```

## 索引对齐规范

每次分析前、分析中、分析后必须参考对应引擎的索引文件：
- **chaos**：`Game/00-引擎与游戏全解析主索引.md`
- **UE**：`Notes/UE/00-UE全解析主索引.md`
- **Bevy**：`Notes/Bevy/00-Bevy全解析主索引.md`（若不存在，向用户确认后创建）

### 分析前
1. **必须读取**对应引擎的全解析主索引，确认目标模块所属阶段和分析重点。
2. 查找索引中是否已有该模块的"计划笔记"和"分析重点"。
3. 若存在，本轮分析的 scope 必须覆盖索引表格中标注的"分析重点"。
4. 若不存在，向用户确认是否要在索引中新增条目，并提议插入位置（阶段/子分类）。

### 分析中
- 笔记内容结构应直接回应索引中的"分析重点"。
- 跨模块关系章节优先链接到索引中同阶段或相邻阶段的已有笔记。
- 若分析 Bevy，注意 Bevy 的 ECS 架构与其他引擎差异较大，索引对齐时重点标注"chaos/UE 中的 X 对应 Bevy 中的 Y"。

### 分析后
- 在笔记末尾添加"索引状态"标注：所属阶段、与索引中计划笔记的对应关系。
- 如用户未明确禁止，主动提议更新对应引擎的全解析主索引中对应条目的状态（⬜ → 🔄 → ✅）。

## 链接维护规范

产出或更新笔记后，必须执行以下链接维护动作：

1. **笔记 → 索引**：在笔记顶部添加返回对应索引的导航链接。
2. **索引 → 笔记**：检查对应引擎的全解析主索引，确认该笔记的 wikilink 已存在且能正确链接。若用户允许，更新该条目的状态图标（⬜ → ✅ 或 🔄）。
3. **跨笔记链接**：在"关联阅读"章节中，使用 wikilink 链接到索引中同阶段或相邻阶段的已有笔记。
4. **跨引擎链接**（可选但推荐）：若同一主题在多个引擎中都有分析，在"关联阅读"中标注其他引擎的对应笔记，如：
   - "UE 对应笔记：[[UE-xxx-源码解析：xxx]]"
   - "Bevy 对应笔记：[[Bevy-xxx-源码解析：xxx]]"

## 迭代式深化策略

每次分析分三轮进行，每轮结束后应告知用户本轮产出，等待确认后再进入下一轮。**三轮分析必须在对应引擎的全解析主索引框架下进行**。

### 第一轮：骨架扫描（What / 接口层）
**目标**：建立模块地图，确认它在全解析索引中的坐标。

- 读取对应引擎的全解析主索引，确认目标模块所属阶段和分析重点
- 读取模块构建定义（`CMakeLists.txt` / `.Build.cs` / `Cargo.toml`）
- 阅读所有公共接口文件
- Grep 查找命名空间、核心类、下游引用点
- 输出一篇快速概览笔记，覆盖索引中该模块的"分析重点"条目（优先接口层面）

### 第二轮：血肉填充（How / 数据层 + 逻辑层）
**目标**：深入核心类与算法，填充索引要求的分析重点。

- 根据索引"分析重点"选取 2~3 个核心类/System，读取实现文件
- 追踪关键数据结构的生命周期和内存布局（第 2 层）
- 追踪核心成员函数的调用链，绘制 Mermaid 流程图/时序图（第 3 层）
- 补充多线程同步、性能优化、上下层交互点的分析

### 第三轮：关联辐射（Context / 跨模块关系）
**目标**：将模块放回引擎/游戏的完整链路中。

- Grep 查找上层调用者和同阶段相关模块的引用
- 分析数据流入和流出的完整路径
- 补充"与上下层的关系"和"关联阅读"
- 若索引中存在同主题的"专题"，说明该模块在专题链路中的位置
- **跨引擎对照**（可选）：若同一主题在多个引擎中已有分析，补充简要的跨引擎架构差异说明
- 更新对应引擎的全解析主索引中的对应状态（与用户确认后执行）

## 用户任务格式与 AI 承诺

### 理想任务格式

鼓励用户每次请求包含：
- 目标引擎（chaos / UE / Bevy）
- 目标模块（如 `core/ECS`、`Engine/Source/Runtime/Engine`、`bevy_ecs`）
- 目标阶段（参考对应引擎的全解析主索引）
- 分析轮次（第一轮/第二轮/第三轮）
- 特别关注点（可覆盖索引中的"分析重点"）
- 已知信息
- 是否允许更新索引状态

### AI 响应承诺

1. **产出物明确**：要么是一篇写入文件的笔记，要么是一份结构化发现摘要
2. **不确定性标注**：基于推测的结论明确标注"推测"或"待确认"
3. **引用可追溯**：所有源码结论都有文件路径（相对于对应 `*_SOURCE_ROOT`）和行号支撑
4. **不发散**：严格限定在用户指定的引擎和模块范围内
5. **引擎适配**：自动识别目标引擎的构建系统、目录约定和对象模型，不将一种引擎的假设套用到另一种引擎上
