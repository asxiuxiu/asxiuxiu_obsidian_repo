---
name: unreal-engine-analysis
description: 当用户请求分析 Unreal Engine（UE）源码、或生成 UE 模块源码分析笔记时触发。提供适配 UE 巨型代码库的系统化三层剥离法分析工作流，聚焦 Public/Private 模块边界、UHT 反射体系、UObject 生命周期，产出结构化的 Obsidian 笔记。
---

# Unreal Engine 源码分析

系统化源码分析工作流，用于对 `D:\workspace\UnrealEngine-release` 进行模块级架构解析。

## 触发条件

- 用户说"帮我分析 UE 的 xx 模块"
- 用户提到"Unreal Engine 源码"
- 用户请求"生成 UE 源码分析笔记"

## 核心方法论：UE 适配版三层剥离法

分析必须按以下三层递进。**UE 代码量巨大，严禁未读完 Public 头文件就深入 .cpp 实现。**

### 第 1 层：接口层（What）
**目标**：建立模块地图，理解对外暴露的能力边界。

- 读取模块根目录的 `*.Build.cs`，了解模块名、`PublicDependencyModuleNames`、`PrivateDependencyModuleNames`、`PublicIncludePaths`
- 阅读 `Public/` 和 `Classes/` 下的头文件（这些是模块的正式接口）
- 识别 `UCLASS`/`USTRUCT`/`UENUM`/`UFUNCTION`/`UPROPERTY` 宏，标记反射边界
- 用 `Grep` 查找其他模块 `#include "Module/Public/Header.h"` 的地方，确认使用方式
- 列出核心类和关键 public/virtual 方法

> **UE 特别提示**：`.generated.h` 是 UHT（Unreal Header Tool）生成的，分析时以原始 `.h` 文件为准，`.generated.h` 仅用于理解 UHT 注入的反射代码。

### 第 2 层：数据层（How - Structure）
**目标**：理解 UObject 体系、内存布局、状态流转。

- 找到核心类（重点关注 `UObject` 派生类、命名空间如 `UE::`）
- 分析成员变量：原始类型 vs `UPROPERTY` 标记的引用（`TObjectPtr`、`TWeakObjectPtr`、`TSoftObjectPtr`）
- 追踪 UObject 生命周期：`NewObject` → `Initialize` → `BeginPlay`/`Tick` → `EndPlay` → `MarkPendingKill`/`GarbageCollect`
- 标注内存分配来源：UObject GC Heap、FMalloc、TMemStack、RenderTarget GPU memory、Custom Allocator
- 分析 UObject 的 Outer/Package/World 层级关系

### 第 3 层：逻辑层（How - Behavior）
**目标**：理解关键算法的执行流程和动态行为。

- 选取 2~3 个最核心的成员函数（优先 `virtual` 重写或 `UFUNCTION` 暴露的入口），逐行追踪调用链
- 分析多线程场景：Game Thread、Render Thread、RHI Thread、Async Loading Thread 的数据传递与同步原语（`FRenderCommand`、`ENQUEUE_RENDER_COMMAND`、`AsyncTask`）
- 分析性能关键路径上的优化手段：DOD（如 `FTransform` SoA）、SIMD、缓存友好遍历、命令缓冲
- 标注与上下层模块的交互点（`FCoreDelegates`、`FSlateApplication`、`FWorldDelegates`、渲染命令队列）

## UE 源码导航策略

| 任务类型 | 推荐工具组合 |
|---------|-------------|
| **模块定位** | `Glob`（`Engine/Source/**/ModuleName/*.Build.cs`）+ `Grep` |
| **接口梳理** | `ReadFile`（`Public/*.h`、`Classes/*.h`）+ `Grep`（`#include` 引用点） |
| **反射追踪** | `Grep`（`UCLASS()`、`UFUNCTION()`）+ `ReadFile`（原始 `.h`，跳过 `.generated.h`） |
| **调用链追踪** | `Grep`（函数定义）+ `ReadFile`（`Private/*.cpp`） |
| **跨模块关系** | `Agent(subagent_type="explore")` |
| **大型目录扫描** | `Shell`（`tree` / `dir`） |
| **批量生成笔记** | `WriteFile` / `StrReplaceFile` |

### 关键目录约定

UE 源码固定根目录：`D:\workspace\UnrealEngine-release`

模块典型结构：
```
Engine/Source/<分组>/<模块名>/
├── <模块名>.Build.cs        # 模块定义
├── Public/                   # 对外公共头文件
│   └── *.h
├── Classes/                  # UObject 类声明（旧约定，仍有大量遗留）
│   └── *.h
├── Internal/                 # 模块间内部接口
│   └── *.h
└── Private/                  # 实现文件
    └── *.cpp
```

常用源码根路径：
- `Engine/Source/Runtime/` — 核心运行时（Core、CoreUObject、Engine、RenderCore、RHI 等）
- `Engine/Source/Editor/` — 编辑器模块
- `Engine/Source/Programs/` — UBT、UHT、UnrealPak 等工具
- `Engine/Source/Developer/` — 开发工具与中间件

### Explore Agent 触发条件

满足以下任一条件时启动 `subagent_type="explore"`：
1. 目标模块源码文件超过 30 个，或依赖关系复杂（UE 常见）
2. 需要追踪一个接口在 3 个以上不同模块中的实现/重写（如 `FRHICommandList`）
3. 对某个 UE 子系统完全没有概念，需要快速建立认知
4. 怀疑存在某种 UE 特定设计模式（如 `TSharedPtr` / `TWeakPtr`、命令队列、代理对象），需要跨文件确认

**Prompt 模板**：
```
请对 D:\workspace\UnrealEngine-release\Engine\Source\<Runtime|Editor>\<模块名> 进行 read-only 探索。
重点关注：
1. Public/ 和 Classes/ 下有哪些核心头文件？
2. 核心类/结构体的命名和职责（尤其 UObject 派生类）
3. 依赖哪些下层模块？被哪些上层模块依赖？（参考 .Build.cs）
4. 是否存在 UHT 代码生成、宏驱动（如 GENERATED_BODY）或大量委托（Delegate）机制？
返回简洁概览：目录结构、关键文件名、核心类列表。
```

### Coder Agent 触发条件

当涉及以下任务时启动 `subagent_type="coder"`：
- 将大量零散发现整理为一篇连贯的 UE 源码分析笔记
- 绘制 Mermaid 类图/流程图/时序图
- 编写辅助分析脚本（如批量查找 UCLASS 定义）

## 笔记产出规范

> **注意**：产出源码分析笔记时，必须结合使用 `/obsidian-markdown` skill，确保正确使用 Obsidian 的 wikilink、embed、callout、frontmatter 等语法规范。

### 目录结构与阶段归类

**所有 UE 源码分析笔记默认输出到 `Notes/UE/` 目录下，并按分析阶段建立子文件夹归类。**

索引文件放在根目录：
- `Notes/UE/00-UE全解析主索引.md`

各阶段子目录命名如下（与 `00-UE全解析主索引.md` 中的阶段划分保持一致）：

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

若用户明确要求将 UE 笔记与其他笔记统一管理，可协商后调整路径。

### 文件名格式

```
UE-<模块>-源码解析：<主题>.md
```
例：`UE-CoreUObject-源码解析：UObject 生命周期.md`、`UE-Engine-源码解析：World 与 Level 架构.md`

专题笔记：
```
UE-专题：<主题>.md
```
例：`UE-专题：反射与代码生成.md`

### 完整输出路径示例

- `Notes/UE/第一阶段-构建系统/UE-构建系统-源码解析：UBT 构建体系总览.md`
- `Notes/UE/第三阶段-核心层/UE-CoreUObject-源码解析：UObject 生命周期.md`
- `Notes/UE/第八阶段-跨领域专题/UE-专题：渲染一帧的生命周期.md`

### 笔记模板

完整模板参见 [references/note-template.md](references/note-template.md)。生成笔记时必须包含：
- frontmatter（title, date, tags, aliases）
- 返回索引的导航链接（如 `[[00-UE全解析主索引|UE全解析主索引]]`）
- 模块定位、接口梳理、数据结构、行为分析、上下层关系、设计亮点、关键源码片段、关联阅读

### 代码引用规范

- 必须标明**文件路径**（相对于 `D:\workspace\UnrealEngine-release`）和**行号范围**
- 只引用最关键的部分，避免大段复制无关代码
- 对引用代码添加中文注释说明
- 遇到 UHT 生成的代码时，备注 `"由 UHT 生成"`

示例格式：
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

## 索引对齐规范

每次分析前、分析中、分析后必须参考索引文件：
- **`Notes/UE/00-UE全解析主索引.md`** — UE 端的本轮分析坐标与进度

### 分析前
1. **必须读取** `Notes/UE/00-UE全解析主索引.md`，确认目标 UE 模块是否已规划、所属阶段和分析重点。
2. 若 UE 索引中尚无该条目，先向用户确认插入位置（阶段/子分类），再更新索引后进入分析。

### 分析中
- 若用户指定了具体 UE 模块（如 `RenderCore`），先建立该模块的独立理解，再分析其与上下层模块的关系。
- 跨模块关系章节优先链接到 `Notes/UE/` 下已产出的其他 UE 分析笔记（使用 wikilink）。

### 分析后
- 在笔记末尾添加"索引状态"标注：所属 UE 阶段、对应的 UE 笔记名称、本轮分析完成度。
- 若用户允许，更新 `Notes/UE/00-UE全解析主索引.md` 中的对应状态（⬜ → 🔄 → ✅）。

## 链接维护规范

产出或更新笔记后，必须执行以下链接维护动作：

1. **笔记 → 索引**：在笔记顶部添加返回 `[[00-UE全解析主索引|UE全解析主索引]]` 的导航链接。
2. **跨笔记链接**：在"关联阅读"章节中，使用 wikilink 链接到已产出的其他 UE 源码分析笔记。
3. **标签一致性**：tags 中统一包含 `ue-source`、`engine-architecture`。

## 迭代式深化策略

UE 代码量巨大，每次分析分三轮进行，每轮结束后应告知用户本轮产出，等待确认后再进入下一轮。

### 第一轮：骨架扫描（What / 接口层）
**目标**：建立 UE 模块地图。

- 读取 `Notes/UE/00-UE全解析主索引.md`，确认 UE 端目标模块的坐标
- 定位 UE 模块路径，读取 `.Build.cs`
- 阅读 `Public/` 和 `Classes/` 下所有头文件
- Grep 查找 `UCLASS`/`USTRUCT`/`UFUNCTION` 定义，列出核心类
- 输出一篇快速概览笔记到对应阶段的 `Notes/UE/` 子目录下

### 第二轮：血肉填充（How / 数据层 + 逻辑层）
**目标**：深入 UObject/核心类的数据结构、生命周期和关键算法。

- 选取 2~3 个核心类，读取 `.h` + `.cpp`（优先 `Private/`）
- 追踪 UObject 或关键结构体的内存布局、GC 引用链、Outer 层级
- 追踪核心成员函数的调用链（如 `UWorld::Tick`、`AActor::Tick`），绘制 Mermaid 流程图
- 补充多线程同步、Render Thread 交互、性能优化分析

### 第三轮：关联辐射 + 知识沉淀（Context）
**目标**：将 UE 模块与引擎其他部分关联，形成可带走的设计知识。

- Grep 查找 UE 上层调用者（如 Editor、Gameplay 代码如何使用该模块）
- 分析数据流入和流出的完整路径
- 补充"设计亮点与可迁移经验"
- 更新索引中的对应状态和链接（与用户确认后执行）

## 用户任务格式与 AI 承诺

### 理想任务格式

鼓励用户每次请求包含：
- 目标 UE 模块（如 `Engine/Source/Runtime/Engine`）
- 分析轮次（第一轮/第二轮/第三轮）
- 特别关注点（如 UObject GC、渲染线程分派、Build.cs 模块边界）
- 已知信息
- 是否允许更新索引状态

### AI 响应承诺

1. **产出物明确**：要么是一篇写入 `Notes/UE/` 对应阶段子目录的笔记，要么是一份结构化发现摘要
2. **不确定性标注**：基于推测的结论明确标注"推测"或"待确认"
3. **引用可追溯**：所有源码结论都有文件路径（相对于 `D:\workspace\UnrealEngine-release`）和行号支撑
4. **不发散**：严格限定在用户指定的 UE 模块范围内
