---
title: UE-NiagaraEditor-源码解析：Niagara 编辑器
date: 2026-04-19
tags:
  - ue-source
  - engine-architecture
  - niagara
  - vfx
aliases:
  - NiagaraEditor
  - Niagara 编辑器
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-NiagaraEditor-源码解析：Niagara 编辑器

## Why：为什么要分析 Niagara 编辑器？

Niagara 是 UE 的下一代视觉特效（VFX）框架，其编辑器模块 `NiagaraEditor` 是连接**艺术家操作界面**与**底层粒子模拟/渲染管线**的关键桥梁。理解该模块的架构，有助于掌握：

1. **可视化脚本编辑器**如何在 UE 编辑器框架（`UnrealEd`/`Slate`）下实现领域特定的图编辑（Graph Editing）。
2. **编译管线**如何将艺术家堆叠的模块（Module Stack）转化为可在 VectorVM / GPU Compute 上执行的 HLSL/字节码。
3. **ViewModel 分层模式**如何隔离 UObject 数据层与 Slate UI 层，支撑复杂的撤销/重做、实时预览和Sequencer时间线联动。
4. **可迁移经验**：模块化的节点图编译、增量编译缓存（DDC）、参数映射（Parameter Map）等设计，可直接映射到自研引擎的粒子/材质编辑器架构中。

---

## What：模块定位与接口层

### Build.cs 模块边界

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/NiagaraEditor.Build.cs`

`NiagaraEditor` 是位于 `Engine/Plugins/FX/Niagara/` 下的**插件编辑器模块**，其依赖关系清晰地划分了三层边界：

| 依赖方向 | 关键模块 | 作用 |
|---------|---------|------|
| **Public 依赖** | `NiagaraCore`, `NiagaraShader`, `Niagara`, `Engine`, `EditorFramework`, `UnrealEd` | 对外暴露的正式接口，依赖运行时核心与编辑器框架 |
| **Private 依赖** | `Slate`, `SlateCore`, `GraphEditor`, `PropertyEditor`, `Sequencer`, `CurveEditor`, `VectorVM`, `Renderer` | 内部实现所需的 UI 框架、曲线编辑、序列器、渲染器 |
| **DynamicallyLoaded** | `WorkspaceMenuStructure` | 延迟加载的菜单结构扩展 |

关键约束：
- `PublicIncludePathModuleNames` 显式导出了 `Engine` 和 `Niagara`，保证外部模块能正确解析 Niagara 运行时类型。
- 通过 `PrivateDependencyModuleNames` 引入 `ShaderFormatVectorVM`，说明编译管线需要调用 VectorVM 着色器后端。

### 核心类族谱

NiagaraEditor 的公共头文件分布在 `Public/` 和 `Private/` 中（该模块无 `Classes/` 目录，所有 UObject 类直接声明在 `Public/` 或 `Private/` 下）。按职责可划分为五大族谱：

#### 1. 模块入口与管理
- **`FNiagaraEditorModule`**（`Public/NiagaraEditorModule.h`，第 172 行）
  - 继承 `IModuleInterface`、`IHasMenuExtensibility`、`IHasToolBarExtensibility`、`FGCObject`
  - 负责注册类型工具、渲染器工厂、编译代理、WidgetProvider、Sequencer 轨道编辑器等

#### 2. 图编辑与节点（Graph/Node）
- **`UEdGraphSchema_Niagara`**（`Public/EdGraphSchema_Niagara.h`，第 87 行）
  - 继承 `UEdGraphSchema`，定义 Niagara 图的核心 Pin 类型、连线规则、类型转换策略
- **`UNiagaraGraph`**（`Public/NiagaraGraph.h`，第 210 行）
  - 继承 `UEdGraph`，是 Niagara 脚本的核心数据容器，管理参数引用、编译哈希、遍历缓存
- **`UNiagaraNode`**（`Public/NiagaraNode.h`，第 28 行）
  - 继承 `UEdGraphNode`，所有 Niagara 节点的基类，提供 `Compile()`、`BuildParameterMapHistory()` 等虚函数

#### 3. 脚本源与编译桥接
- **`UNiagaraScriptSource`**（`Public/NiagaraScriptSource.h`，第 18 行）
  - 继承 `UNiagaraScriptSourceBase`，持有 `UNiagaraGraph* NodeGraph`，是 UObject → Graph 的桥梁
- **`INiagaraCompiler`** / **`INiagaraHlslTranslator`**（`Public/INiagaraCompiler.h`，第 64/125 行）
  - 抽象编译接口，将图遍历结果翻译为 HLSL，再经 VectorVM/GPU 后端生成可执行数据

#### 4. ViewModel 体系（MVVM 数据层）
- **`FNiagaraSystemViewModel`**（`Public/ViewModels/NiagaraSystemViewModel.h`，第 119 行）
  - 非 UObject，继承 `FGCObject`、`FTickableEditorObject`，管理整个 System 的编辑状态、Sequencer、预览组件
- **`FNiagaraScriptViewModel`**（`Public/ViewModels/NiagaraScriptViewModel.h`，第 27 行）
  - 管理单个 NiagaraScript 的输入/输出参数集合与图视图模型
- **`UNiagaraStackViewModel`** / **`UNiagaraStackRoot`**（`Public/ViewModels/Stack/NiagaraStackViewModel.h` 第 51 行 / `NiagaraStackRoot.h` 第 22 行）
  - UObject 派生的栈视图模型，将 Graph 节点以“模块堆栈”形式呈现给 UI

#### 5. 工具箱（Toolkit）
- **`FNiagaraSystemToolkit`**（`Private/Toolkits/NiagaraSystemToolkit.h`，第 48 行）
  - 继承 `FWorkflowCentricApplication`，是 Niagara System/Emitter 资产编辑器的顶层 Slate 容器

### 反射边界（UCLASS/USTRUCT/UFUNCTION）

| 宏 | 典型使用者 | 说明 |
|----|----------|------|
| `UCLASS()` | `UNiagaraGraph`, `UNiagaraNode`, `UNiagaraScriptSource`, `UNiagaraStackViewModel`, `UNiagaraStackRoot` | 需要 UObject 生命周期、GC、序列化的核心数据类 |
| `USTRUCT()` | `FNiagaraSchemaAction_NewNode`, `FNiagaraGraphParameterReference`, `FNiagaraGraphScriptUsageInfo` | 图操作、参数引用、编译缓存等值类型 |
| `UFUNCTION()` | 多为 `UNiagaraStackEntry` 子类的 Blueprint/Editor 可调用接口 | 栈条目的交互行为暴露 |
| `UPROPERTY()` | `UNiagaraGraph::VariableToScriptVariable`, `UNiagaraScriptSource::NodeGraph` | GC 托管的引用关系 |

> **UHT 提示**：`.generated.h` 由 UHT 生成，分析时以原始 `.h` 为准。例如 `NiagaraGraph.generated.h` 是 `NiagaraGraph.h` 的反射产物。

---

## How - Structure：数据层

### UObject 体系与内存布局

#### 1. UNiagaraGraph：脚本图的数据心脏

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraGraph.h`，第 526~582 行

```cpp
UCLASS(MinimalAPI)
class UNiagaraGraph : public UEdGraph
{
    // ...
private:
    UPROPERTY()
    FGuid ChangeId;  // 图整体变更标识，任何结构修改都会更新

    UPROPERTY()
    FGuid ForceRebuildId;  // 强制重建 DDC 键的内部值

    UPROPERTY()
    TArray<FNiagaraGraphScriptUsageInfo> CachedUsageInfo;  // 按 Usage 缓存的编译哈希与遍历结果

    UPROPERTY()
    TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>> VariableToScriptVariable;  // 变量元数据映射

    UPROPERTY(Transient)
    mutable TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterToReferencesMap;  // 参数引用追踪（运行时重建）

    UPROPERTY(Transient)
    mutable TArray<FNiagaraScriptVariableData> CompilationScriptVariables;  // 编译期脚本变量缓存

    bool bIsForCompilationOnly = false;  // 是否为编译临时副本
};
```

**内存来源分析**：
- `UNiagaraGraph` 本身分配在 **UObject GC Heap**（由 `NewObject<UNiagaraGraph>()` 创建）。
- `TMap`、`TArray` 等容器内联在 UObject 内存布局中，其元素（如 `FNiagaraVariable`）由 UObject 序列化系统管理。
- `Transient` 标记的字段（如 `ParameterToReferencesMap`）不参与磁盘序列化，仅在内存中按需重建。

#### 2. UNiagaraScriptSource：Graph 的 UObject 外壳

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraScriptSource.h`，第 17~26 行

```cpp
UCLASS(MinimalAPI)
class UNiagaraScriptSource final : public UNiagaraScriptSourceBase
{
    UPROPERTY()
    TObjectPtr<class UNiagaraGraph> NodeGraph = nullptr;  // 核心图数据

    bool bIsCompilationCopy = false;  // 是否为编译副本（非 UPROPERTY，不序列化）
    bool bIsReleased = false;
};
```

**Outer/Package 关系**：
- `UNiagaraScriptSource` 的 Outer 是 `UNiagaraScript`（或 `UNiagaraEmitter` 内的 Script）。
- `UNiagaraScript` 的 Outer 链：`UNiagaraScript` → `UNiagaraEmitter` / `UNiagaraSystem` → `UPackage`。
- 编译时通过 `CreateCompilationCopy()` 生成**临时副本**，其 Outer 为 `FNiagaraEditorModule::TempPackage`（一个 transient package），避免污染原始资产。

#### 3. FNiagaraSystemViewModel：非 UObject 的 GC 托管 ViewModel

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraSystemViewModel.h`，第 119~127 行

```cpp
class FNiagaraSystemViewModel 
    : public TSharedFromThis<FNiagaraSystemViewModel>
    , public FGCObject          // 手动添加 UObject 引用到 GC 根集
    , public FEditorUndoClient  // 撤销/重做监听
    , public FTickableEditorObject  // 编辑器 Tick
    , public TNiagaraViewModelManager<UNiagaraSystem, FNiagaraSystemViewModel>
    , public INiagaraParameterDefinitionsSubscriberViewModel
    , public UE::MovieScene::ISignedObjectEventHandler
```

**关键成员与引用类型**：

```cpp
// 第 659~666 行
UNiagaraSystem* System;  // 裸指针，被 FGCObject::AddReferencedObjects 保护
TObjectPtr<UNiagaraComponent> PreviewComponent;  // UPROPERTY 风格强引用
FNiagaraSystemInstance* SystemInstance;  // 底层模拟实例（非 UObject，运行时原生指针）
```

**内存来源**：
- `FNiagaraSystemViewModel` 分配在 **FMalloc**（普通 C++ `new`）。
- 其内部 UObject 成员（`PreviewComponent`、`NiagaraSequence` 等）分配在 **UObject GC Heap**。
- `SystemInstance` 指向 `Niagara` 运行时模块分配的底层模拟状态（通常在高频缓存友好的自定义分配器中）。

#### 4. UNiagaraStackEntry：栈条目的树形结构

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackEntry.h`，第 52~752 行

```cpp
UCLASS(MinimalAPI)
class UNiagaraStackEntry : public UObject
{
    // ...
private:
    TWeakPtr<FNiagaraSystemViewModel> SystemViewModel;   // 弱引用，避免循环引用
    TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel; // 弱引用

    UPROPERTY()
    TObjectPtr<UNiagaraStackEditorData> StackEditorData; // 持久化编辑器状态（展开/折叠等）

    UPROPERTY()
    TArray<TObjectPtr<UNiagaraStackEntry>> Children;     // 树形子条目（GC 强引用）

    mutable TArray<UNiagaraStackEntry*> FilteredChildren; // 过滤后的子条目缓存（非 UPROPERTY，手动维护）
};
```

**设计要点**：
- `Children` 使用 `TObjectPtr<UNiagaraStackEntry>`，由 UObject GC 自动管理生命周期。
- `SystemViewModel`/`EmitterViewModel` 使用 `TWeakPtr`，因为 ViewModel 是 C++ 共享指针对象，避免 UObject 与 SharedPtr 之间形成循环引用。
- `StackEditorData` 是 `UPROPERTY` 强引用，确保用户折叠/展开状态随资产持久化。

### 核心对象生命周期

| 阶段 | 关键函数 | 说明 |
|------|---------|------|
| **创建** | `FNiagaraSystemToolkit::InitializeWithSystem()` → `FNiagaraSystemViewModel::Initialize()` | Toolkit 打开资产时创建 ViewModel |
| **初始化** | `FNiagaraSystemViewModel::SetupPreviewComponentAndInstance()` | 创建预览组件和 SystemInstance |
| **Tick** | `FNiagaraSystemViewModel::Tick(float)` | 每帧检查自动编译、序列器同步、Stack Tick |
| **编译** | `CompileSystem(bool)` → `FNiagaraSystemCompilationTask` | 发起异步编译任务 |
| **销毁** | `FNiagaraSystemViewModel::~()` / `Cleanup()` | 移除委托、释放预览组件、清理 ViewModel |
| **GC** | `FGCObject::AddReferencedObjects` | ViewModel 手动报告 UObject 引用，防止预览组件被 GC 回收 |

---

## How - Behavior：逻辑层

### 调用链 1：System 编译管线（Graph → HLSL → VM/GPU）

这是 Niagara 编辑器最核心的行为：将艺术家编辑的模块堆栈转化为运行时可执行代码。

#### Step 1：触发编译

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/ViewModels/NiagaraSystemViewModel.cpp`

```cpp
void FNiagaraSystemViewModel::CompileSystem(bool bForce)
{
    // Emitter 合并模式下禁止自动编译
    if (EditMode == ENiagaraSystemViewModelEditMode::EmitterDuringMerge && bForce == false)
    {
        return;
    }
    SystemScriptViewModel->CompileSystem(bForce);  // 委托给 SystemScriptViewModel
    bCompilePendingCompletion = true;
    InvalidateCachedCompileStatus();

    // 请求 Stack 验证更新
    if (SystemStackViewModel)
    {
        SystemStackViewModel->RequestValidationUpdate();
        // ... 对每个 Emitter 的 Stack 也请求验证
    }
}
```

#### Step 2：System 级异步编译任务

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraCompilationTasks.h`，第 12~295 行

```cpp
struct FNiagaraSystemCompilationTask : public TSharedFromThis<FNiagaraSystemCompilationTask, ESPMode::ThreadSafe>
{
    // 编译任务状态机
    enum class EState : uint8
    {
        Invalid = 0,
        WaitingForProcessing,
        ResultsProcessed,
        Completed,
        Aborted,
    };
    std::atomic<EState> CompilationState = EState::Invalid;
    // ...
};
```

编译任务内部结构：
- **`FSystemInfo`** / **`FEmitterInfo`**：消化后的 System/Emitter 元数据，包含静态变量、模拟阶段（SimStages）、常量解析器。
- **`FCompileGroupInfo`**：按 Emitter 分组，管理编译副本（`FNiagaraCompilationCopyData`）。
- **`FCompileTaskInfo`**：单个 Script 的编译任务，包含：
  - `Translate()`：图 → HLSL（调用 `FNiagaraHlslTranslator`）
  - `IssueCompileVm()`：HLSL → VectorVM 字节码（调用 `FHlslNiagaraCompiler`）
  - `IssueCompileGpu()`：HLSL → 计算着色器（调用 `FNiagaraShaderMapCompiler`）

#### Step 3：图遍历与 HLSL 生成

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraHlslTranslator.h`，第 72~87 行

```cpp
enum class ENiagaraCodeChunkMode : uint8
{
    GlobalConstant,
    SystemConstant,
    OwnerConstant,
    EmitterConstant,
    Uniform,
    Source,
    Body,
    SpawnBody,
    UpdateBody,
    InitializerBody,
    SimulationStageBody,
    // ...
};
```

`FNiagaraHlslTranslator`（实现 `INiagaraHlslTranslator`）的职责：
1. 从 `UNiagaraGraph` 的 OutputNode（如 `SystemSpawnScript`、`ParticleUpdateScript`）开始反向遍历。
2. 对每个 `UNiagaraNode` 调用 `Compile(FTranslator*, TArray<int32>& Outputs)`，生成 HLSL 代码块（Code Chunk）。
3. 根据 `ENiagaraCodeChunkMode` 将代码块分类到不同执行阶段（Spawn/Update/Simulation Stage）。
4. 输出完整的 HLSL 字符串，供后续 VectorVM 或 GPU 后端编译。

#### Step 4：编译结果回写

编译完成后，`FNiagaraSystemCompilationTask::Poll()` 检查结果，并将 `FNiagaraVMExecutableData` 回写到 `UNiagaraScript` 的 `VMExecutableData` 中。`FNiagaraSystemViewModel::Tick()` 每帧检测 `bCompilePendingCompletion`，完成后广播 `OnSystemCompiled` 委托，触发 Stack 验证和视口刷新。

**多线程要点**：
- **Game Thread**：`FNiagaraSystemViewModel::Tick()` 发起编译请求、检测完成状态。
- **Task Graph / Worker Threads**：`FNiagaraSystemCompilationTask` 使用 `UE::Tasks::FTaskEvent` 调度图翻译（Translate）和编译（CompileVM/CompileGPU）。
- **DDC 异步 I/O**：`FDispatchAndProcessDataCacheGetRequests` / `FDispatchDataCachePutRequests` 在后台线程读写 Derived Data Cache，避免阻塞 UI。
- **Render Thread**：GPU 编译（`FNiagaraShaderMapCompiler`）通过 `FShaderCompilingManager` 将着色器编译任务分发到 `ShaderCompileWorker` 子进程，结果通过 Render Thread 的 ShaderMap 回调应用。

### 调用链 2：Emitter 预览与实时 Simulation

#### 预览组件生命周期

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraSystemViewModel.h`，第 662~665 行

```cpp
TObjectPtr<UNiagaraComponent> PreviewComponent;  // 视口中的预览组件
FNiagaraSystemInstance* SystemInstance;          // 底层模拟实例
```

**初始化链路**：
1. `FNiagaraSystemViewModel::Initialize()` → `SetupPreviewComponentAndInstance()`
2. 创建 `UNiagaraComponent` 并附加到预览世界的 `AActor` 上。
3. `UNiagaraComponent` 内部创建 `FNiagaraSystemInstance`，开始粒子模拟。
4. `SystemInstanceInitialized()` / `SystemInstanceReset()` 委托监听实例状态变化，同步到 UI。

#### Tick 驱动的实时预览

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/ViewModels/NiagaraSystemViewModel.cpp`

```cpp
void FNiagaraSystemViewModel::Tick(float DeltaTime)
{
    // 1. 检测编译是否完成
    if (bCompilePendingCompletion && GetSystem().HasOutstandingCompilationRequests() == false)
    {
        bCompilePendingCompletion = false;
        OnSystemCompiled().Broadcast();
    }

    // 2. 自动编译触发
    bool bAutoCompileThisFrame = GetDefault<UNiagaraEditorSettings>()->GetAutoCompile() 
        && bCanAutoCompile 
        && GetLatestCompileStatus() == ENiagaraScriptCompileStatus::NCS_Dirty;
    if ((bForceAutoCompileOnce || bAutoCompileThisFrame) && !WaitingOnCompilation())
    {
        CompileSystem(false);
    }

    // 3. 处理待处理的 Reset 请求
    if (bResetRequestPending)
    {
        ResetSystem(ETimeResetMode::AllowResetTime, EMultiResetMode::AllowResetAllInstances, EReinitMode::ReinitializeSystem);
    }

    // 4. Tick 所有 StackViewModel（递归刷新条目）
    if (SystemStackViewModel != nullptr)
    {
        SystemStackViewModel->Tick();
    }
    for (auto& EmitterHandleVM : EmitterHandleViewModels)
    {
        EmitterHandleVM->GetEmitterStackViewModel()->Tick();
    }
    // ...
}
```

**性能关键路径**：
- `TickCompileStatus()` 使用**时间片（Time Slicing）**策略：每帧只检查少量脚本的编译状态，避免大量 Emitter 时的一次性遍历开销。
- `ResetSystem()` 区分 **Reset**（保持当前状态，不拉取变更）和 **Reinitialize**（重新初始化，拉取最新编译结果），在快速迭代时减少不必要的模拟重启。

### 调用链 3：Stack 刷新与模块依赖解析

Niagara 编辑器的 Stack UI 将图节点抽象为“模块堆栈”（Module Stack），每个模块对应一个 `UNiagaraNodeFunctionCall`。Stack 的刷新机制是理解编辑器性能的关键。

#### Stack 树形结构刷新

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/ViewModels/Stack/NiagaraStackRoot.cpp`

```cpp
void UNiagaraStackRoot::RefreshChildrenInternal(
    const TArray<UNiagaraStackEntry*>& CurrentChildren, 
    TArray<UNiagaraStackEntry*>& NewChildren, 
    TArray<FStackIssue>& NewIssues)
{
    if (bIncludeSystemInformation)
    {
        RefreshSystemChildren(CurrentChildren, NewChildren);  // System Spawn / Update
    }
    if (bIncludeEmitterInformation && GetEmitterViewModel().IsValid())
    {
        // 根据 Emitter 模式选择不同的子条目生成策略
        bool bStateless = EmitterHandleViewModel->GetEmitterHandle()->GetEmitterMode() == ENiagaraEmitterMode::Stateless;
        if (bStateless)
        {
            RefreshEmitterStatelessChildren(CurrentChildren, NewChildren, EmitterHandleViewModel);
        }
        else
        {
            bool bSummaryView = GetEmitterViewModel()->GetEditorData().ShouldShowSummaryView();
            bSummaryView ? RefreshEmitterSummaryChildren(CurrentChildren, NewChildren)
                         : RefreshEmitterFullChildren(CurrentChildren, NewChildren);
        }
    }
}
```

**刷新策略**：
- `GetOrCreateXxxGroup()` 采用**增量复用**策略：对比 `CurrentChildren` 与目标状态，复用已有条目，仅创建/删除差异条目，减少 UObject 分配和 Slate 重建。
- `UNiagaraStackViewModel::Tick()` 驱动搜索（`SearchTick()`）、验证（`UpdateStackWithValidationResults()`）和结构刷新（`RequestRefreshDeferred()`）。

#### 模块依赖数据构建

> 文件：`Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraSystemViewModel.h`，第 110~116 行

```cpp
struct FNiagaraStackModuleData
{
    TWeakObjectPtr<UNiagaraNodeFunctionCall> WeakModuleNode;  // 弱引用模块节点
    ENiagaraScriptUsage Usage;
    FGuid UsageId;
    int32 Index;
    FGuid EmitterHandleId;
};
```

`FNiagaraSystemViewModel::BuildAndCacheStackModuleData()` 在编译前构建每个 Emitter 的模块依赖图，用于：
1. **循环依赖检测**：检查模块输入输出是否存在闭环。
2. **增量编译判断**：通过比较 `FNiagaraStackModuleData` 的哈希决定哪些 Script 需要重新编译。
3. **参数传递分析**：确定 Rapid Iteration Parameters（快速迭代参数）的有效范围。

---

## 上下层模块交互

```
┌─────────────────────────────────────────────────────────────┐
│                    Slate UI 层                               │
│  SNiagaraSystemViewport / SNiagaraSystemEditorWidget         │
│         ↕                                                    │
│  FNiagaraSystemToolkit (FWorkflowCentricApplication)         │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│              ViewModel 层 (NiagaraEditor)                    │
│  FNiagaraSystemViewModel / FNiagaraScriptViewModel           │
│  UNiagaraStackViewModel / UNiagaraStackEntry (UObject)       │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│              图数据层 (NiagaraEditor)                        │
│  UNiagaraGraph ← UNiagaraScriptSource ← UNiagaraScript       │
│  UNiagaraNode / UNiagaraNodeFunctionCall / UNiagaraNodeOutput│
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│              编译与运行时桥接                                │
│  FNiagaraHlslTranslator → INiagaraCompiler → FHlslNiagaraCompiler│
│  FNiagaraSystemCompilationTask → FShaderCompilingManager     │
│  DerivedDataCache (DDC) 读写                               │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│              Niagara 运行时核心 (Niagara/NiagaraCore)        │
│  UNiagaraSystem / UNiagaraEmitter / UNiagaraScript           │
│  FNiagaraSystemInstance / FNiagaraEmitterInstance            │
│  VectorVM / GPU Compute Dispatch                             │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│              渲染层 (Renderer / NiagaraShader)               │
│  FNiagaraRenderer / FNiagaraMeshRenderer / FNiagaraSpriteRenderer│
│  FNiagaraShader / ShaderMap / RHI CommandList                │
└─────────────────────────────────────────────────────────────┘
```

### 关键交互点

| 交互方向 | 机制 | 说明 |
|---------|------|------|
| **NiagaraEditor → NiagaraCore** | `UNiagaraScriptSource` 继承 `UNiagaraScriptSourceBase` | 编辑器图数据通过 SourceBase 接口注入运行时 Script |
| **NiagaraEditor → Sequencer** | `ISequencer` / `UMovieSceneNiagaraEmitterTrack` | Sequencer 时间线控制 Emitter 生命周期和参数关键帧 |
| **NiagaraEditor → PropertyEditor** | `FNiagaraStackObjectPropertyCustomization` / `IDetailCustomization` | Stack 中的 UObject 属性通过 Details 面板定制渲染 |
| **NiagaraEditor → DDC** | `FDerivedDataCache` + `UE::Tasks::FTaskEvent` | 编译结果缓存到 DDC，跨会话复用 |
| **NiagaraEditor → ShaderCore** | `FNiagaraShaderMapCompiler` / `FShaderCompilingManager` | GPU Script 的 HLSL 经标准 UE 着色器编译管线生成 ShaderMap |
| **NiagaraEditor → NiagaraShader** | `FNiagaraShaderType` / `FNiagaraShaderMapId` | Niagara 自定义 ShaderType，将粒子属性绑定到着色器参数 |

---

## 设计亮点与可迁移经验

### 1. 图编译的“三层分离”
Niagara 将编译明确分为：
- **Traversal（遍历）**：`UNiagaraGraph::BuildTraversal()` 生成节点执行顺序。
- **Translation（翻译）**：`FNiagaraHlslTranslator` 将节点语义转为 HLSL。
- **Compilation（编译）**：`FHlslNiagaraCompiler` / `FNiagaraShaderMapCompiler` 将 HLSL 转为 VM 字节码或 GPU Shader。

这种分层使得：
- 前端图结构变更只需重新 Traverse + Translate，若 HLSL 未变则无需重新 Compile。
- GPU/VM 后端可独立演进，前端节点逻辑不受影响。

### 2. 增量编译与变更追踪
- `ChangeId` + `CompileHash` + `ReferenceHash` 三级标识：
  - `ChangeId`：图结构任何修改都会变，用于 UI 刷新。
  - `CompileHash`：遍历结果的哈希，用于判断是否需要重新翻译。
  - `ReferenceHash`：静态变量和开关状态的哈希，用于判断下游脚本是否需要联动编译。
- **DDC 缓存键**由 `FNiagaraVMExecutableDataId` 生成，包含所有输入参数的哈希，实现编译结果的精确复用。

### 3. ViewModel 的“弱引用 + FGCObject”模式
- `FNiagaraSystemViewModel` 不是 UObject，但实现了 `FGCObject`，可以安全地持有 `TObjectPtr<UNiagaraComponent>` 等 UObject 引用，防止预览组件在编辑过程中被 GC。
- ViewModel 之间使用 `TWeakPtr` 连接，避免 Slate/UI 层与 UObject 数据层形成循环引用。

### 4. Stack 的“增量刷新 + 延迟搜索”
- `UNiagaraStackEntry::RefreshChildrenInternal()` 采用增量复用策略，每帧仅创建/销毁差异条目。
- `UNiagaraStackViewModel::SearchTick()` 将搜索任务分片到多帧执行，每帧消耗不超过 `MaxSearchTime`，避免大量条目时 UI 卡顿。

### 5. 参数映射（Parameter Map）的历史追踪
- `UNiagaraNode::BuildParameterMapHistory()` 构建参数读写历史，支持：
  - **数据流可视化**：在 Stack 中高亮参数的读取/写入位置。
  - **编译优化**：确定哪些参数可以放入 Rapid Iteration 常量表，避免不必要的 uniform 更新。
  - **验证规则**：检测未初始化读取、类型不匹配等错误。

---

## 关键源码索引

| 文件路径（相对 `D:\workspace\UnrealEngine-release`） | 行号范围 | 内容 |
|-----------------------------------------------------|---------|------|
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/NiagaraEditor.Build.cs` | 1~80 | 模块依赖定义 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraEditorModule.h` | 172~602 | 模块入口类，编译代理，WidgetProvider |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/EdGraphSchema_Niagara.h` | 87~228 | Niagara 图模式定义，Pin 类型与连线规则 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraGraph.h` | 210~582 | 核心图数据，编译哈希缓存，参数引用映射 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraNode.h` | 28~280 | 节点基类，Compile/BuildParameterMapHistory 接口 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraScriptSource.h` | 17~101 | Script 与 Graph 的桥梁，编译副本管理 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/INiagaraCompiler.h` | 29~143 | 编译抽象接口，HLSL 翻译器接口 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraHlslTranslator.h` | 1~100 | HLSL 翻译器，CodeChunkMode 枚举 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraCompilationTasks.h` | 12~295 | 系统级异步编译任务，状态机，DDC 交互 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraSystemViewModel.h` | 119~851 | System 编辑总控，Tick，预览，编译触发 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/Toolkits/NiagaraSystemToolkit.h` | 48~290 | 资产编辑器 Toolkit，工作流模式 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraScriptViewModel.h` | 27~156 | Script 级 ViewModel，参数集合与图视图 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackViewModel.h` | 51~247 | Stack 视图模型，搜索，验证，结构刷新 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackRoot.h` | 22~88 | Stack 根条目，System/Emitter 子条目生成 |
| `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackEntry.h` | 52~752 | Stack 条目基类，树形结构，问题收集 |

---

## 关联阅读

- [[UE-Niagara-源码解析：Niagara 粒子系统核心]] — 运行时 NiagaraCore 与 Niagara 模块的 UObject 体系和模拟管线
- [[UE-Niagara-源码解析：Niagara 渲染与模拟]] — FNiagaraSystemInstance、EmitterInstance、Renderer 的底层实现
- [[UE-UnrealEd-源码解析：编辑器框架总览]] — FWorkflowCentricApplication、FGCObject、FEditorUndoClient 的通用机制
- [[UE-Sequencer-源码解析：时间线与过场动画编辑]] — ISequencer、MovieSceneTrack 的编辑框架
- [[UE-专题：材质与着色器编译链路]] — 对比 MaterialEditor → ShaderCore → ShaderCompileWorker 与 Niagara 编译管线的异同

---

## 索引状态

- **所属阶段**：第五阶段-编辑器层-5.2 可视化编辑工具
- **状态**：✅ 完成
