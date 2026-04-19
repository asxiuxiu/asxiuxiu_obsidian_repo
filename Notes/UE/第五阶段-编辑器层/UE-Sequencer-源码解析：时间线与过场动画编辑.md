---
title: UE-Sequencer-源码解析：时间线与过场动画编辑
date: 2026-04-19
tags: [ue-source, engine-architecture, sequencer]
aliases: [Sequencer源码解析]
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

## Why：为什么要分析 Sequencer？

- **问题背景**：Sequencer 是 UE 中过场动画、镜头编排、Gameplay 动画调度的核心编辑工具，其架构直接影响编辑器性能与扩展性。
- **不用后果**：无法理解 UE 的时间轴编辑、关键帧插值、子序列嵌套等核心机制，也难以基于 Sequencer 开发自定义轨道或编辑器插件。
- **应用场景**：自定义 Track Editor、批量关键帧工具、镜头自动化管线、游戏内过场系统二次开发。

## What：Sequencer 是什么？

Sequencer 是 UE 的**可视化时间线编辑器**，由两个模块协同实现：

| 模块 | 职责 | 关键路径 |
|------|------|----------|
| `SequencerCore` | MVVM 基础框架、ViewModel 树、选择系统、核心接口 | `Engine/Source/Editor/SequencerCore` |
| `Sequencer` | 具体编辑器实现、Track Editor 管理、求值驱动、UI 命令 | `Engine/Source/Editor/Sequencer` |

### 模块依赖（Build.cs）

`Sequencer` 模块的 `PrivateDependencyModuleNames` 包含：`SequencerCore`、`MovieScene`、`MovieSceneTracks`、`MovieSceneTools`、`CurveEditor`、`LevelSequence`、`BlueprintGraph` 等 30+ 个模块（见 `Engine/Source/Editor/Sequencer/Sequencer.Build.cs` 第15~56行）。

`SequencerCore` 的依赖更精简，主要为 `Core`、`CoreUObject`、`Slate`、`SlateCore`、`CurveEditor`、`GraphEditor`、`TimeManagement`（见 `Engine/Source/Editor/SequencerCore/SequencerCore.Build.cs` 第9~28行）。

---

## 第一层：接口层（What）

### 1.1 核心接口类

#### `ISequencer`（`Engine/Source/Editor/Sequencer/Public/ISequencer.h` 第215行）
- **继承**：`IMovieScenePlayer`、`TSharedFromThis<ISequencer>`
- **定位**：Sequencer 编辑器对外的**主控接口**，所有编辑器操作均通过此接口暴露。
- **关键方法**：
  - 时间控制：`GetLocalTime()` / `SetLocalTime()` / `GetGlobalTime()` / `SetGlobalTime()` / `PlayTo()`
  - 序列导航：`GetRootMovieSceneSequence()` / `GetFocusedMovieSceneSequence()` / `FocusSequenceInstance()` / `PopToSequenceInstance()`
  - 绑定管理：`CreateBinding()` / `MakeNewSpawnable()` / `AddActors()` / `ConvertToSpawnable()`
  - 选择系统：`SelectObject()` / `SelectTrack()` / `SelectSection()` / `EmptySelection()`
  - 数据通知：`NotifyMovieSceneDataChanged(EMovieSceneDataChangeType)` / `RefreshTree()`
  - 委托簇：`FOnGlobalTimeChanged` / `FOnPlayEvent` / `FOnMovieSceneDataChanged` / `FOnChannelChanged` 等

#### `ISequencerModule`（`Engine/Source/Editor/Sequencer/Public/ISequencerModule.h` 第221行）
- **定位**：模块级工厂与注册中心。
- **核心功能**：
  - `CreateSequencer(const FSequencerInitParams&)` — 创建编辑器实例
  - `RegisterTrackEditor()` / `UnregisterTrackEditor()` — 注册轨道编辑器
  - `RegisterChannelInterface<ChannelType>()` — 注册通道类型到编辑器接口的映射
  - `RegisterSequenceEditor()` — 为特定序列类型注册专用编辑器

#### `ISequencerCoreModule`（`Engine/Source/Editor/SequencerCore/Public/ISequencerCoreModule.h` 第19行）
- **定位**：MVVM ViewModel 的工厂注册中心。
- **核心方法**：
  - `RegisterModelType(UClass*, TFunction<TSharedPtr<FViewModel>(UObject*)>)` — 将 UObject 类型映射到 ViewModel 工厂
  - `FactoryNewModel(UObject*)` — 根据已注册工厂创建对应 ViewModel

### 1.2 UCLASS/USTRUCT 识别

Sequencer 模块本身**极少直接使用 `UCLASS`**（因为编辑器代码大量是非反射的 Slate/MVVM 代码），但接口层包含以下关键枚举：

```cpp
// Engine/Source/Editor/Sequencer/Public/ISequencer.h 第102~149行
UENUM() enum class EAutoChangeMode : uint8 { AutoKey, AutoTrack, All, None };
UENUM() enum class EAllowEditsMode : uint8 { AllEdits, AllowSequencerEditsOnly, AllowLevelEditsOnly };
UENUM() enum class EKeyGroupMode : uint8 { KeyChanged, KeyGroup, KeyAll };

// 数据变更类型枚举（非 UENUM，但用于委托系统）
enum class EMovieSceneDataChangeType
{
    TrackValueChanged,
    TrackValueChangedRefreshImmediately,
    MovieSceneStructureItemAdded,
    MovieSceneStructureItemRemoved,
    MovieSceneStructureItemsChanged,
    ActiveMovieSceneChanged,
    RefreshAllImmediately,
    Unknown,
    RefreshTree
};
```

### 1.3 核心类清单（Public 头文件）

| 类/接口 | 文件 | 作用 |
|---------|------|------|
| `ISequencer` | `Public/ISequencer.h` | 编辑器主接口 |
| `ISequencerModule` | `Public/ISequencerModule.h` | 模块工厂与注册 |
| `ISequencerTrackEditor` | `Public/ISequencerTrackEditor.h` | 轨道编辑器接口 |
| `ISequencerSection` | `Public/ISequencerSection.h` | 轨道 Section 绘制接口 |
| `ISequencerChannelInterface` | `Public/ISequencerChannelInterface.h` | 通道编辑接口 |
| `ISequencerObjectChangeListener` | `Public/ISequencerObjectChangeListener.h` | 对象变更监听 |
| `ISequencerCoreModule` | `SequencerCore/Public/ISequencerCoreModule.h` | Core 模块接口 |
| `FViewModel` | `SequencerCore/Public/MVVM/ViewModels/ViewModel.h` | MVVM 基类 |
| `FEditorViewModel` | `SequencerCore/Public/MVVM/ViewModels/EditorViewModel.h` | 编辑器根 VM |
| `FSequencerEditorViewModel` | `Sequencer/Public/MVVM/ViewModels/SequencerEditorViewModel.h` | Sequencer 专用 VM |
| `FSequenceModel` | `Sequencer/Public/MVVM/ViewModels/SequenceModel.h` | 序列数据模型 |
| `FTrackModel` | `Sequencer/Public/MVVM/ViewModels/TrackModel.h` | 轨道视图模型 |

---

## 第二层：数据层（How - Structure）

### 2.1 核心 UObject 派生类与数据资产

Sequencer 编辑器的**数据资产**是 `UMovieSceneSequence` 及其派生类（如 `ULevelSequence`），但这些定义在 `MovieScene` / `LevelSequence` 模块中，不在 Sequencer 模块内。

Sequencer 模块内与 UObject 生命周期强相关的对象：

```cpp
// FSequencer 核心数据成员（Engine/Source/Editor/Sequencer/Private/Sequencer.h 第1300~1605行）

/** 用户设置（UObject，由 SequencerSettingsContainer 管理） */
TObjectPtr<USequencerSettings> Settings;

/** 根序列（弱引用，实际生命周期由资产系统管理） */
TWeakObjectPtr<UMovieSceneSequence> RootSequence;

/** 编译数据管理器（UObject，缓存序列编译结果） */
TObjectPtr<UMovieSceneCompiledDataManager> CompiledDataManager;
```

### 2.2 MVVM 树形数据结构（SequencerCore）

SequencerCore 使用**自定义侵入式链表**构建 ViewModel 树，而非 TArray：

```cpp
// Engine/Source/Editor/SequencerCore/Public/MVVM/ViewModels/ViewModel.h 第37~304行
class FViewModel
    : public ICastable
    , public TSharedFromThis<FViewModel>
    , public FDynamicExtensionContainer
{
    // ...
private:
    /** 父节点（弱引用，避免循环引用） */
    TWeakPtr<FViewModel> WeakParent;
    /** 跨层级共享数据（选择状态、事件广播等） */
    TSharedPtr<FSharedViewModelData> SharedData;
    /** 侵入式链表链接 */
    FViewModelListLink Link;
    /** 子节点链表头（支持多链表，如 Outliner + TrackArea） */
    FViewModelListHead* FirstChildListHead;
    // ...
};
```

**设计意图**：
- `WeakParent` 避免父子循环引用；子节点由 `TSharedPtr` 持有，父节点只存弱引用。
- `FirstChildListHead` 支持**一个 ViewModel 同时存在于多个逻辑列表**（如 Outliner 列表和 TrackArea 列表）。
- `SharedData` 在同一棵树的所有节点间共享，用于广播层级变更事件。

### 2.3 TViewModelPtr — 支持动态扩展的智能指针

```cpp
// Engine/Source/Editor/SequencerCore/Public/MVVM/ViewModelPtr.h 第106~336行
template<typename T>
struct TViewModelPtr : TViewModelConversions<T>
{
    TSharedPtr<ViewModelType> SharedModel;
    T* Extension = nullptr;  // 指向具体扩展实例的裸指针
    // 支持隐式转换为任何 Extension 类型
    TImplicitViewModelCast<ViewModelType> ImplicitCast() const;
};
```

`TViewModelPtr` 同时持有 `TSharedPtr<FViewModel>`（生命周期）和 `T* Extension`（接口访问），允许一次 `CastThis<T>()` 后零开销地访问扩展接口。

### 2.4 关键成员变量分布

#### FSequencer（实现类，约 1600 行头文件）

```cpp
// Engine/Source/Editor/Sequencer/Private/Sequencer.h

// --- Track Editor 系统 ---
TArray<TSharedPtr<ISequencerTrackEditor>> TrackEditors;
TMap<FObjectKey, TSharedPtr<ISequencerTrackEditor>> TrackEditorsByType;

// --- 对象绑定系统 ---
TArray<TSharedPtr<ISequencerEditorObjectBinding>> ObjectBindings;
TSharedPtr<ISequencerObjectChangeListener> ObjectChangeListener;

// --- 求值核心 ---
FMovieSceneRootEvaluationTemplateInstance RootTemplateInstance;  // 编译后的求值模板
TSharedPtr<FMovieSceneEntitySystemRunner> Runner;               // UE5 EntitySystem 运行器

// --- 子序列层级栈 ---
TArray<FMovieSceneSequenceID> ActiveTemplateIDs;       // 当前焦点序列层级
TArray<FMovieSceneSequenceID> TemplateIDForwardStack;  // 导航前进栈
TArray<FMovieSceneSequenceID> TemplateIDBackwardStack; // 导航后退栈

// --- 时间变换矩阵 ---
FMovieSceneSequenceTransform GlobalPlaybackWarpTransform;   // 全局时间扭曲
FMovieSceneSequenceTransform RootToUnwarpedLocalTransform;  // 根到本地（未扭曲）
FMovieSceneSequenceTransform RootToWarpedLocalTransform;    // 根到本地（已扭曲）
FMovieSceneSequenceTransform LocalToWarpedLocalTransform;   // 本地到扭曲本地
FMovieSceneTransformBreadcrumbs CurrentTimeBreadcrumbs;     // 时间面包屑（处理循环/跳转）

// --- 播放状态 ---
EMovieScenePlayerStatus::Type PlaybackState;
FMovieScenePlaybackPosition PlayPosition;
float PlaybackSpeed;

// --- MVVM ---
TSharedPtr<UE::Sequencer::FSequencerEditorViewModel> ViewModel;
TSharedRef<FSequencerNodeTree> NodeTree;  // 旧版节点树（逐步被 MVVM 替代）
```

#### FSequenceModel（序列 VM）

```cpp
// Engine/Source/Editor/Sequencer/Public/MVVM/ViewModels/SequenceModel.h 第42~105行
class FSequenceModel : public FViewModel, /* ... */
{
    FViewModelListHead RootOutlinerItems;    // Outliner 根节点列表
    FViewModelListHead DecorationModelList;  // 装饰物节点列表
    TWeakObjectPtr<UMovieSceneSequence> WeakSequence;  // 弱引用序列
    TWeakPtr<FSequencerEditorViewModel> WeakEditor;    // 弱引用编辑器 VM
    FMovieSceneSequenceID SequenceID;
    // 事件处理器（非侵入式）
    MovieScene::TNonIntrusiveEventHandler<MovieScene::ISignedObjectEventHandler> SequenceEventHandler;
    MovieScene::TNonIntrusiveEventHandler<MovieScene::ISequenceDataEventHandler> MovieSceneDataEventHandler;
};
```

### 2.5 内存分配来源

| 对象 | 分配方式 | 备注 |
|------|---------|------|
| `FSequencer` | `MakeShared<FSequencer>()` | Slate 共享指针管理 |
| `FViewModel` 派生类 | `MakeShared<T>()` 或工厂 | 通过 `ISequencerCoreModule::FactoryNewModel` |
| `UMovieSceneSequence` | UObject GC | 标准 UObject 生命周期 |
| `UMovieSceneCompiledDataManager` | `NewObject<>` | 由 FSequencer 强引用（TObjectPtr） |
| `FSequencerNodeTree` | `MakeShared<FSequencerNodeTree>()` | 旧版树结构，逐步废弃 |
| `FMovieSceneRootEvaluationTemplateInstance` | 栈对象（值类型） | 非指针，直接内嵌在 FSequencer 中 |

---

## 第三层：逻辑层（How - Behavior）

### 3.1 核心调用链一：初始化与层级构建（`InitSequencer`）

```cpp
// Engine/Source/Editor/Sequencer/Private/Sequencer.cpp 第333~370行
void FSequencer::InitSequencer(const FSequencerInitParams& InitParams, ...)
{
    // 1. 保存编辑上下文（LevelEditor / Standalone）
    bIsEditingWithinLevelEditor = InitParams.bEditWithinLevelEditor;

    // 2. 初始化 SpawnRegister（管理 Spawnable 对象生命周期）
    SpawnRegister = InitParams.SpawnRegister.IsValid()
        ? InitParams.SpawnRegister
        : MakeShareable(new FNullMovieSceneSpawnRegister);

    // 3. 从 SettingsContainer 加载或创建设置对象
    Settings = USequencerSettingsContainer::GetOrCreate<USequencerSettings>(...);

    // 4. 注册全局委托（Blueprint 编译、对象替换等）
    GEditor->OnBlueprintPreCompile().AddLambda(...);
    FCoreUObjectDelegates::OnObjectsReplaced.AddLambda(...);

    // 5. 初始化 TrackEditors（遍历所有已注册的 FOnCreateTrackEditor 委托）
    for (const FOnCreateTrackEditor& Delegate : TrackEditorDelegates)
    {
        TrackEditors.Add(Delegate.Execute(SharedThis(this)));
    }

    // 6. 初始化 MVVM ViewModel
    ViewModel = MakeShared<FSequencerEditorViewModel>(SharedThis(this), HostCapabilities);
    ViewModel->SetSequence(RootSequence.Get());
    ViewModel->InitializeEditor();

    // 7. 初始化求值模板
    InitRootSequenceInstance();
}
```

**与上下层交互**：
- **上层（模块）**：`ISequencerModule::CreateSequencer` 组装 `FSequencerInitParams` 并调用 `InitSequencer`。
- **下层（MovieScene）**：`InitRootSequenceInstance()` 调用 `RootTemplateInstance.Initialize(...)`，进入 `MovieScene` 模块的求值系统。

### 3.2 核心调用链二：时间轴驱动与求值（`Tick` → `EvaluateInternal`）

```cpp
// Engine/Source/Editor/Sequencer/Private/Sequencer.cpp 第968~1220行
void FSequencer::Tick(float InDeltaTime)
{
    // 1. 执行延迟的层级操作（MVVM 批量变更）
    FViewModelHierarchyOperation Operation(GetViewModel()->GetRootSequenceModel()->GetSharedData());

    // 2. 若数据脏了，重新编译序列
    if (CompiledDataManager->IsDirty(RootSequencePtr))
    {
        CompiledDataManager->Compile(RootSequencePtr);
        bNeedsEvaluate = true;
    }

    // 3. 若需要刷新树，暂停播放并刷新 Outliner
    if (bNeedTreeRefresh || NodeTree->NeedsFilterUpdate())
    {
        RefreshTree();
    }

    // 4. TimeController 驱动（支持外部时钟源，如音频节拍）
    TimeController->Tick(InDeltaTime, PlaybackSpeed * Dilation);

    // 5. 播放状态：从 TimeController 请求新时间
    if (PlaybackState == EMovieScenePlayerStatus::Playing)
    {
        FFrameTime NewGlobalTime = TimeController->RequestCurrentTime(...);
        FFrameTime LocalTime = RootToUnwarpedLocalTransform.TransformTime(NewGlobalTime, ...);
        SetLocalTimeLooped(LocalTime, Breadcrumbs);
    }

    // 6. Tick 所有 TrackEditors
    for (auto& Editor : TrackEditors) Editor->Tick(InDeltaTime);

    // 7. 若需要求值，调用 EvaluateInternal
    if (bNeedsEvaluate && !IsInSilentMode())
    {
        EvaluateInternal(PlayPosition.GetCurrentPositionAsRange());
    }
}
```

```cpp
// Engine/Source/Editor/Sequencer/Private/Sequencer.cpp 第3683~3738行
void FSequencer::EvaluateInternal(FMovieSceneEvaluationRange InRange, bool bHasJumped)
{
    // 1. 更新面包屑（处理循环边界穿越）
    RootToUnwarpedLocalTransform.TransformTime(InRange.GetTime(),
        FTransformTimeParams().HarvestBreadcrumbs(CurrentTimeBreadcrumbs).IgnoreClamps());

    // 2. 应用全局时间扭曲（TimeWarp）
    if (!GlobalPlaybackWarpTransform.IsIdentity())
    {
        InRange = FMovieSceneEvaluationRange(
            GlobalPlaybackWarpTransform.ComputeTraversedHull(InRange.GetRange()),
            InRange.GetFrameRate(), InRange.GetDirection());
    }

    // 3. 更新播放上下文（World / Client）
    UpdateCachedPlaybackContextAndClient();

    // 4. 构建求值上下文
    FMovieSceneContext Context = FMovieSceneContext(InRange, PlaybackState)
        .SetIsSilent(SilentModeCount != 0);
    Context.SetHasJumped(bHasJumped);

    // 5. 【核心】同步阻塞求值 —— 进入 MovieScene 求值系统
    RootTemplateInstance.EvaluateSynchronousBlocking(Context);
    SuppressAutoEvalSignature.Reset();

    // 6. 可选：重跑 ConstructionScript（编辑器预览需要）
    if (Settings->ShouldRerunConstructionScripts()) RerunConstructionScripts();

    // 7. 广播全局时间变更（驱动视口刷新、Slate 重绘等）
    if (!IsInSilentMode()) OnGlobalTimeChangedDelegate.Broadcast();
}
```

**性能关键路径**：
- `CompiledDataManager->Compile()` 只在数据 dirty 时触发，避免每帧重复编译。
- `EvaluateInternal` 最终调用 `FMovieSceneRootEvaluationTemplateInstance::EvaluateSynchronousBlocking`，这是**同步单线程求值**。在 UE5.2 中，底层已迁移到 Entity Component System（`FMovieSceneEntitySystemRunner`），但编辑器预览仍走同步路径以确保确定性。
- `SilentMode` 机制：在批量操作（如拖曳关键帧）时进入 Silent 模式，暂停视口刷新和事件广播，减少 UI 开销。

### 3.3 核心调用链三：本地时间设置与 Snap（`SetLocalTime`）

```cpp
// Engine/Source/Editor/Sequencer/Private/Sequencer.cpp 第3496~3595行
void FSequencer::SetLocalTime(FFrameTime NewTime, ESnapTimeMode SnapTimeMode, bool bEvaluate)
{
    // 1. AutoScroll：若启用，滚动视图使时间可见
    if (IsAutoScrollEnabled()) ScrollIntoView(NewTime / LocalResolution);

    // 2. Interval Snap：强制对齐到整帧
    if ((SnapTimeMode & STM_Interval) && Settings->GetForceWholeFrames())
    {
        NewTime = FFrameRate::TransformTime(
            FFrameRate::TransformTime(NewTime, LocalResolution, LocalDisplayRate).RoundToFrame(),
            LocalDisplayRate, LocalResolution);
    }

    // 3. Key Snap：吸附到最近的关键帧 / Section / Mark
    if (SnapTimeMode & STM_Keys)
    {
        NewTime = OnGetNearestKey(NewTime, NearestKeyOption);
    }

    // 4. 直接设置本地时间（经过 Snap 后）
    SetLocalTimeDirectly(NewTime, bEvaluate);
}

void FSequencer::SetLocalTimeDirectly(FFrameTime NewTime, bool bEvaluate)
{
    // 将本地时间反变换到全局时间空间
    FMovieSceneInverseSequenceTransform LocalToRootTransform = /* ... */;
    TOptional<FFrameTime> NewGlobalTime = LocalToRootTransform.TryTransformTime(
        NewTime + ScrubLinearOffset, Breadcrumbs,
        EInverseEvaluateFlags::AnyDirection | EInverseEvaluateFlags::Cycle);

    if (NewGlobalTime.IsSet())
    {
        SetGlobalTime(NewGlobalTime.GetValue(), bEvaluate);
    }
}

void FSequencer::SetGlobalTime(FFrameTime NewTime, bool bEvaluate)
{
    // 转换到 PlayPosition 的输入帧率
    NewTime = ConvertFrameTime(NewTime, GetRootTickResolution(), PlayPosition.GetInputRate());

    // FrameLocked 模式下向下取整
    if (PlayPosition.GetEvaluationType() == EMovieSceneEvaluationType::FrameLocked)
        NewTime = NewTime.FloorToFrame();

    // 若时间未变，跳过求值（避免重复触发事件）
    if (PlayPosition.GetCurrentPosition() != NewTime)
    {
        FMovieSceneEvaluationRange EvalRange = PlayPosition.JumpTo(NewTime);
        if (bEvaluate) EvaluateInternal(EvalRange);
    }
}
```

**关键设计**：
- **本地时间 vs 全局时间**：本地时间是当前焦点序列的相对时间；全局时间是根序列的绝对时间。Sequencer 通过 `FMovieSceneSequenceTransform` 矩阵族管理多层级时间映射。
- **BreadCrumb 机制**：`FMovieSceneTransformBreadcrumbs` 记录穿越循环边界、子序列边界的路径，确保时间反变换（Local→Global）在复杂嵌套和 TimeWarp 场景下仍能正确定位。

### 3.4 多线程场景

Sequencer 编辑器模块本身**未使用多线程**处理核心求值逻辑：
- `EvaluateInternal` 调用 `EvaluateSynchronousBlocking` —— 名称即表明同步阻塞。
- 编辑器 Tick 发生在 **Game Thread**（`FTickableEditorObject`）。
- UE5.2 的 `FMovieSceneEntitySystemRunner` 支持后台异步求值，但 Sequencer **编辑器预览**为保证帧间确定性，仍使用同步路径。
- 多线程主要出现在：
  - `UMovieSceneCompiledDataManager::Compile()` 内部可能并行编译子序列。
  - 渲染 Movie（`RenderMovie`）时走独立的渲染管线。

### 3.5 与上下层模块交互点

| 交互方向 | 模块 | 交互方式 | 说明 |
|----------|------|---------|------|
| 上层 | `LevelEditor` | `FLevelEditorSequencerIntegration` | 将 Sequencer 嵌入关卡编辑器 |
| 上层 | `UnrealEd` | `FEditorUndoClient` | 撤销/重做事务监听 |
| 同层 | `CurveEditor` | `FCurveEditor` / 委托 | 曲线编辑器双向同步 |
| 同层 | `SequencerWidgets` | Slate 控件 | 时间轴、Transport 控件 |
| 下层 | `MovieScene` | `FMovieSceneRootEvaluationTemplateInstance` | 编译与求值核心 |
| 下层 | `MovieSceneTracks` | `ISequencerTrackEditor` | 各类轨道编辑器 |
| 下层 | `MovieSceneTools` | 循环引用（Circular） | 工具函数（如 Bake、Export） |
| 下层 | `LevelSequence` | `ULevelSequence` | 默认编辑的序列类型 |

---

## 架构 insights

1. **MVVM 渐进式迁移**：UE5.2 的 Sequencer 正处于旧版 `FSequencerNodeTree` 向新 MVVM（`FViewModel` 树）的过渡期。`FSequencer` 中同时存在 `NodeTree`（旧）和 `ViewModel`（新），但新代码优先使用 MVVM。

2. **侵入式链表优于 TArray**：`FViewModel` 使用 `FViewModelListHead/FViewModelListLink` 侵入式链表管理子节点，原因：
   - 一个节点可同时属于 Outliner 和 TrackArea 两个逻辑列表。
   - 移动节点时无需重新分配数组内存，O(1) 指针调整。
   - 支持 `FViewModelHierarchyOperation` 批量延迟事件广播。

3. **Extension 模式替代多重继承**：`FTrackModel` 通过实现 `IRenameableExtension`、`ILockableExtension`、`IGroupableExtension` 等 10+ 个接口，避免了一个巨型基类。`TViewModelPtr` 的 `ImplicitCast()` 提供了类似 Rust `dyn Trait` 的零开销抽象。

4. **时间系统的矩阵化设计**：Sequencer 不存储单一时间，而是维护 `RootToUnwarpedLocalTransform`、`RootToWarpedLocalTransform`、`LocalToWarpedLocalTransform` 等变换矩阵，配合 `FMovieSceneTransformBreadcrumbs` 处理子序列嵌套、TimeWarp、循环等复杂场景。

---

## 索引状态

- **所属阶段**：第五阶段-编辑器层-5.2 可视化编辑工具
- **状态**：✅ 完成
