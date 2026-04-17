---
title: UE-MovieScene-源码解析：Sequencer 与过场动画
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - movie-scene
  - sequencer
  - animation
aliases:
  - MovieScene 模块源码解析
---

> [[00-UE全解析主索引|UE全解析主索引]]

## Why：为什么要学习 MovieScene？

- **问题背景**：现代游戏需要电影级过场动画（Cinematic），这要求引擎具备时间轴驱动的动画系统，能够精确控制对象变换、材质参数、相机切换、音频、事件触发等。
- **不用后果**：没有统一的序列化时间轴系统，过场动画将依赖硬编码或外部视频，无法与 gameplay 动态交互。
- **应用场景**：
  - 游戏开场/结尾动画
  - 剧情演出（Cutscene）
  - 动态相机（Camera Shake/Cut）
  - 与动画蓝图结合的骨骼动画轨道

## What：MovieScene 是什么？

**MovieScene** 是 UE 的**序列资产与运行时评估系统**，核心由两个模块构成：

| 模块 | 职责 |
|------|------|
| `MovieScene` | 基础数据模型、编译系统、ECS 运行时评估核心、遗留模板评估基础设施 |
| `MovieSceneTracks` | 具体轨道类型（Transform、Float、Event、Audio、CameraCut 等）及对应的 ECS System |

UE5 已**全面迁移到 Entity System（ECS）架构**，遗留的 `FMovieSceneEvalTemplate` 评估模型仍保留以兼容旧轨道，但现代轨道均通过 ECS Entity + System 进行评估。

## How：三层源码剖析

---

### 1. 接口层：模块边界与 Public 目录

#### 1.1 模块依赖（Build.cs）

**MovieScene.Build.cs**（`Engine/Source/Runtime/MovieScene/MovieScene.Build.cs`，L5-L28）
```csharp
PublicDependencyModuleNames.AddRange(
    new string[] {
        "Core",
        "CoreUObject",
        "InputCore",
        "Engine",
        "SlateCore",
        "TimeManagement",
        "UniversalObjectLocator"
    }
);
```

**MovieSceneTracks.Build.cs**（`Engine/Source/Runtime/MovieSceneTracks/MovieSceneTracks.Build.cs`，L5-L27）
```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "MovieScene",
    "TimeManagement",
    "AnimationCore",
    "AudioExtensions",
    "Constraints",
    "AudioMixer"
});
PrivateDependencyModuleNames.AddRange(new string[]
{
    "SlateCore",
    "AnimGraphRuntime",
    "PropertyPath"
});
```

> `MovieSceneTracks` 显式依赖 `MovieScene`，并引入 `AnimGraphRuntime`（用于 `UAnimSequencerInstance`）和 `AudioMixer`。

#### 1.2 核心 Track / Section 类型

| 类型 | 文件路径 | 关键职责 |
|------|----------|----------|
| `UMovieScene3DTransformTrack` | `MovieSceneTracks/Public/Tracks/MovieScene3DTransformTrack.h` L56 | 处理 Component Transform 动画，实现 `IMovieSceneBlenderSystemSupport` 以支持混合 |
| `UMovieSceneFloatTrack` | `MovieSceneTracks/Public/Tracks/MovieSceneFloatTrack.h` L14 | 处理 float 属性动画（如材质参数） |
| `UMovieSceneEventTrack` | `MovieSceneTracks/Public/Tracks/MovieSceneEventTrack.h` L31 | 触发离散事件（Blueprint Event），`EFireEventsAtPosition` 控制触发时机 |
| `UMovieSceneAudioTrack` | `MovieSceneTracks/Public/Tracks/MovieSceneAudioTrack.h` L21 | 管理音频 Section（`USoundBase`），支持多行排列 |
| `UMovieSceneCameraCutTrack` | `MovieSceneTracks/Public/Tracks/MovieSceneCameraCutTrack.h` L28 | 相机切换轨道，自动/手动管理 Section 间隙 |

---

### 2. 数据层：序列资产与运行时数据结构

#### 2.1 UMovieScene — 序列资源根容器

**`MovieScene/Public/MovieScene.h`** L355-L362
```cpp
UCLASS(DefaultToInstanced, MinimalAPI)
class UMovieScene
    : public UMovieSceneDecorationContainerObject
{
    GENERATED_UCLASS_BODY()
```

核心数据成员：
- **Spawnables / Possessables**：L390-L499 提供 `AddSpawnable`、`AddPossessable` 等 API，管理对象绑定（运行时生成 vs 场景已有对象）。
- **Tracks**：L648-L705 提供 `AddTrack`、`FindTrack`、`GetTracks`，根级轨道直接属于 `UMovieScene`；对象绑定轨道通过 `FGuid ObjectBindingID` 关联。
- **Bindings**：L773 `TArray<FMovieSceneBinding> ObjectBindings`

#### 2.2 UMovieSceneTrack / UMovieSceneSection — 轨道与片段

**`MovieScene/Public/MovieSceneTrack.h`** L202-L206
```cpp
UCLASS(abstract, DefaultToInstanced, MinimalAPI, BlueprintType)
class UMovieSceneTrack
    : public UMovieSceneDecorationContainerObject
    , public IMovieSceneTrackVirtualAPI
```

关键设计：
- `EvalOptions`（L217-L218）：`bEvalNearestSection`、`bEvaluateInPreroll` / `bEvaluateInPostroll`
- `EvaluationField`（L371-L372）：`FMovieSceneTrackEvaluationField`，描述 Section 何时应被评估
- `SupportedBlendTypes`（L270）：决定轨道是否支持 Layered Blending

**`MovieScene/Public/MovieSceneSection.h`** L240-L244
```cpp
UCLASS(abstract, DefaultToInstanced, MinimalAPI, BlueprintType)
class UMovieSceneSection
    : public UMovieSceneDecorationContainerObject
```

核心属性：
- `SectionRange`（L787-L788）：`FMovieSceneFrameRange`，定义片段时间范围
- `ChannelProxy`（L867）：`TSharedPtr<FMovieSceneChannelProxy>`，聚合所有 Channel 指针
- `Easing`（L783-L784）：`FMovieSceneEasingSettings`，控制自动/手动的 Ease In/Out
- `BlendType`（L860）：`FOptionalMovieSceneBlendType`，片段级混合类型

#### 2.3 FMovieSceneEvaluationTemplate / FMovieSceneEvaluationTrack — 遗留编译模板

**`MovieScene/Public/Evaluation/MovieSceneEvaluationTemplate.h`** L158-L162
```cpp
USTRUCT()
struct FMovieSceneEvaluationTemplate
{
    GENERATED_BODY()
```

- `Tracks`（L276-L277）：`TMap<FMovieSceneTrackIdentifier, FMovieSceneEvaluationTrack>`，编译后的评估轨道映射
- `StaleTracks`（L280）：过渡态轨道，用于运行时安全替换
- `TemplateSerialNumber`（L288-L289）：每次重新编译时递增，确保运行时版本一致性

**`MovieScene/Public/Evaluation/MovieSceneEvaluationTrack.h`** L53-L57
```cpp
USTRUCT()
struct FMovieSceneEvaluationTrack
{
    GENERATED_BODY()
```

- `ChildTemplates`（L411-L412）：`TArray<FMovieSceneEvalTemplatePtr>`，通常每 Section 一个模板
- `EvaluationMethod`（L403-L404）：`EEvaluationMethod::Static`（单点求值）或 `Swept`（区间扫描，用于事件）
- `EvaluationPriority`（L399-L400）：评估优先级，数值越高越先执行
- `TrackTemplate`（L415-L416）：`FMovieSceneTrackImplementationPtr`，轨道级覆盖逻辑

#### 2.4 FMovieSceneChannel — 曲线通道体系

**`MovieScene/Public/Channels/MovieSceneChannel.h`** L110-L116
```cpp
USTRUCT()
struct FMovieSceneChannel
{
    GENERATED_BODY()
    FMovieSceneChannel() {}
    virtual ~FMovieSceneChannel() {}
```

提供通用键操作接口：`GetKeyTime`、`SetKeyTime`、`DuplicateKeys`、`DeleteKeys`、`RemapTimes` 等。

**`MovieScene/Public/Channels/MovieSceneFloatChannel.h`** L120-L123
```cpp
USTRUCT()
struct FMovieSceneFloatChannel : public FMovieSceneChannel
{
    GENERATED_BODY()
    typedef float CurveValueType;
    typedef FMovieSceneFloatValue ChannelValueType;
```

- `Times` / `Values`（L331-L335）：`TArray<FFrameNumber>` + `TArray<FMovieSceneFloatValue>`
- `Evaluate(FFrameTime, float&)`（L187）：基于帧时间的曲线求值
- `PreInfinityExtrap` / `PostInfinityExtrap`（L322-L327）：前后外推模式

**`MovieSceneTracks/Public/Channels/MovieSceneEventChannel.h`** L24-L27
```cpp
USTRUCT()
struct FMovieSceneEventChannel : public FMovieSceneChannel
{
    GENERATED_BODY()
    typedef FMovieSceneEvent CurveValueType;
```

- `KeyTimes` / `KeyValues`（L70-L75）：存储事件触发时间与事件负载

#### 2.5 ECS 架构核心：Runner + Sequence Instance

**`MovieScene/Public/EntitySystem/MovieSceneEntitySystemRunner.h`** L73-L74
```cpp
class FMovieSceneEntitySystemRunner : public TSharedFromThis<FMovieSceneEntitySystemRunner>
```

**`MovieScene/Public/EntitySystem/MovieSceneSequenceInstance.h`** L69
```cpp
struct FSequenceInstance
```

- `FSequenceInstance::Update(const FMovieSceneContext&)`（L115）：单实例每帧更新入口
- `FSequenceInstance::Ledger`（L72）：`FEntityLedger`，追踪本实例实例化的所有 Entity
- `LegacyEvaluator`（L398）：`TUniquePtr<FMovieSceneTrackEvaluator>`，兼容遗留模板评估

---

### 3. 逻辑层：Sequencer 评估一帧的完整流程

#### 3.1 Runner Flush 的流水线阶段

**`MovieScene/Public/EntitySystem/MovieSceneEntitySystemRunner.h`** L29-L43
```cpp
enum class ERunnerFlushState
{
    None                 = 0,
    Start                = 1 << 0,
    ConditionalRecompile = 1 << 1,
    Import               = 1 << 2,
    ReimportAfterCompile = 1 << 3,
    Spawn                = 1 << 4,
    Instantiation        = 1 << 5,
    Evaluation           = 1 << 6,
    Finalization         = 1 << 7,
    EventTriggers        = 1 << 8,
    PostEvaluation       = 1 << 9,
    End                  = 1 << 10,
};
```

**`MovieScene/Private/EntitySystem/MovieSceneEntitySystemRunner.cpp`** L375-L480（`FlushNext` 核心状态机）

完整流水线：

1. **Start**（L391-L395）
   - `StartEvaluation(Linker)`：设置外部评估标志，初始化 EntityManager 的调度线程
2. **ConditionalRecompile**（L398-L402）
   - `GameThread_ConditionalRecompile(Linker)`：检查序列资产是否脏，必要时重新编译评估模板（L650-L713）
3. **Import**（L405-L410）
   - `GameThread_UpdateSequenceInstances(Linker)`：
     - 将上下文拆解为多个 `FDissectedUpdate`（L715-L1000+）
     - 调用 `FSequenceInstance::Update(Context)`，将 Section/Channel 数据转换为 ECS Entity
4. **Spawn**（L419-L425）
   - `GameThread_SpawnPhase(Linker)`：运行 Spawn System，处理 `Spawnables` 的生成/销毁
5. **Instantiation**（L427-L433）
   - `GameThread_InstantiationPhase(Linker)`：将 Entity 与 UObject 实际关联，创建组件实例
   - `GameThread_PostInstantiation`：清理与记账
6. **Evaluation**（L436-L441）
   - `GameThread_EvaluationPhase(Linker)`：主评估阶段，按 System 依赖图并行执行所有 Evaluator System（如 Transform、Float、Audio、CameraCut）
7. **Finalization**（L444-L450）
   - `GameThread_EvaluationFinalizationPhase(Linker)`：收集评估结果，触发外部事件，执行遗留模板
8. **EventTriggers**（L452-L459）
   - `GameThread_EventTriggerPhase(Linker)`：触发绑定的事件（如 EventTrack 的 Blueprint 委托）
9. **PostEvaluation**（L461-L468）
   - `GameThread_PostEvaluationPhase(Linker)`：调用所有 Sequence Instance 的 `PostEvaluation()` 回调
10. **End**（L471-L477）
    - `EndEvaluation(Linker)`：重置标志，广播评估结束

> 预算控制：`Flush(double BudgetMs, ERunnerFlushState TargetState)`（L482-L497）支持按毫秒预算中断，下帧续跑。

#### 3.2 遗留模板评估模型（Legacy Templates）

**`MovieScene/Public/Evaluation/MovieSceneEvalTemplate.h`** L45-L47
```cpp
USTRUCT()
struct FMovieSceneEvalTemplate : public FMovieSceneEvalTemplateBase
```

遗留模板执行三阶段（注释 L40-L43）：
1. **Initialize**（L99）：帧前设置，访问可变状态，准备持久数据
2. **Evaluate**（L115）/ **EvaluateSwept**（L132）：执行计算，将结果写入 `FMovieSceneExecutionTokens`
3. **Execute**：在 Finalization 阶段于 GameThread 执行 Token，实际修改 UObject 状态

**`MovieScene/Public/EntitySystem/MovieSceneSequenceInstance.h`** L142
```cpp
MOVIESCENE_API void RunLegacyTrackTemplates();
```

`FSequenceInstance` 在 Finalization 阶段调用 `RunLegacyTrackTemplates()`，驱动未迁移到 ECS 的轨道。

#### 3.3 与 UAnimSequencerInstance 的交互

**`AnimGraphRuntime/Public/AnimSequencerInstance.h`** L17-L20
```cpp
UCLASS(transient, NotBlueprintable, MinimalAPI)
class UAnimSequencerInstance : public UAnimInstance, public ISequencerAnimationSupport
```

关键接口：
- `UpdateAnimTrack(UAnimSequenceBase*, int32 SequenceId, float Position, float Weight, bool bFireNotifies)`（L25）
- `UpdateAnimTrackWithRootMotion(const FAnimSequencerData&)`（L32）

交互流程：
1. `MovieSceneTracks` 中的 **Skeletal Animation Track** 在 ECS `Evaluation` 阶段通过 `MovieSceneSkeletalAnimationSystem` 收集动画片段数据（时间、权重、镜像、根运动覆盖等）。
2. System 将计算结果写入绑定对象的动画实例。若目标骨架使用 Sequencer 驱动的自定义动画实例，则通过 `ISequencerAnimationSupport` 接口调用 `UAnimSequencerInstance::UpdateAnimTrack`。
3. `UAnimSequencerInstance` 内部维护一个轻量动画节点图，直接输出pose，无需完整 AnimBlueprint 图，从而高效支持 Sequencer 中的骨骼动画混合与根运动提取。

---

## 索引状态

- **所属阶段**：第四阶段-客户端运行时层 / 4.2 动画与视觉系统
- **完成度**：✅
