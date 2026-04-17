---
title: UE-Engine-源码解析：SoundCue 与音频蓝图
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - soundcue
  - audio-blueprint
aliases:
  - UE-SoundCue-源码解析
---

> [← 返回 [[00-UE全解析主索引|UE全解析主索引]]]

# UE-Engine 源码解析：SoundCue 与音频蓝图

## Why：为什么需要 SoundCue？

在游戏开发中，直接播放原始的 `USoundWave`（音频文件）往往无法满足复杂需求。设计师通常需要：

1. **随机变体**：同一枪击声有 5 种细微不同的录音，每次射击随机播放一种。
2. **层级混合**：引擎声由低频轰鸣 + 高频啸叫两个独立的 SoundWave 按转速比例混合而成。
3. **逻辑控制**：脚步声根据地面材质（草地/水泥/木板）切换不同的 Wave；根据玩家速度切换播放频率。
4. **效果叠加**：在播放前自动应用音量衰减（Attenuation）、混响（Reverb）、延迟（Delay）、循环（Looping）。

如果把这些逻辑硬编码在 C++ 或蓝图中，音频设计师将完全依赖程序员，迭代效率极低。UE 的解决方案是 **SoundCue** —— 一种可视化、节点图驱动的音频资产。音频设计师可以在 Sound Cue Editor 中通过拖拽节点（Wave Player、Random、Mixer、Looping、Attenuation 等）来组合出任意复杂的播放行为，而无需编写代码。

---

## What：SoundCue 是什么？

SoundCue 是 UE 音频系统的**高层编排资产**，源码位于 `Runtime/Engine/Classes/Sound/`。它的核心由三部分组成：

| 组件 | 类 | 职责 |
|---|---|---|
| **音频资产基类** | `USoundBase` | 所有可播放音频资产的基类（SoundCue、SoundWave、DialogueWave 均继承于此）。定义了音量、音高、衰减、Submix 路由、并发控制等通用属性。 |
| **SoundCue 资产** | `USoundCue` | 继承 `USoundBase`，包含一个根节点 `FirstNode`，以及整棵节点树。是设计师在编辑器中直接编辑的资产。 |
| **SoundNode 节点** | `USoundNode` 及其子类 | 构成 SoundCue 的行为图。每个节点实现特定的音频逻辑（播放 Wave、随机选择、混合、循环、分支、延迟等）。 |

### 核心类一览

| 类 | 类型 | 职责 |
|---|---|---|
| `USoundBase` | `UObject` | 可播放音频资产的抽象基类。定义 `Parse()`、`IsPlayable()`、`GetMaxDistance()` 等核心接口。 |
| `USoundCue` | `USoundBase` | 节点图资产。持有 `FirstNode` 和编辑器图 `SoundCueGraph`（Editor Only）。实现 `Parse()` 以递归遍历节点树生成 `FWaveInstance`。 |
| `USoundNode` | `UObject` | 节点基类。定义 `ParseNodes()` 递归接口、编辑器接口（`GetMaxChildNodes`、`CreateStartingConnectors` 等）。 |
| `USoundNodeWavePlayer` | `USoundNode` | 叶节点，引用一个 `USoundWave`。是唯一能实际产生 `FWaveInstance` 的节点。 |
| `USoundNodeMixer` | `USoundNode` | 将多个子节点的输出按输入音量混合。 |
| `USoundNodeRandom` | `USoundNode` | 根据权重随机选择一个子节点播放。支持 "不放回" 随机（Without Replacement）。 |
| `USoundNodeLooping` | `USoundNode` | 逻辑循环节点。循环次数达到后重置子节点的初始化状态（让 Random 节点可以重新选择）。 |
| `USoundNodeAttenuation` | `USoundNode` | 在节点树中应用距离衰减设置。 |
| `FSoundCueParameterTransmitter` | 非 UObject | 负责将 Game Thread 的参数更新路由到 SoundCue 解析出的各个子 WaveInstance。 |

---

## How：三层剥离法分析

### 第 1 层：接口层（What / 模块边界）

#### 关键头文件梳理

| 头文件路径 | 核心内容 |
|---|---|
| `Classes/Sound/SoundCue.h` | `USoundCue` 声明，包含 `FirstNode`、`VolumeMultiplier`、`Parse()`、`CacheAggregateValues()` 等。 |
| `Classes/Sound/SoundBase.h` | `USoundBase` 声明，定义播放、衰减、Submix、并发、虚拟化等通用音频属性。 |
| `Classes/Sound/SoundNode.h` | `USoundNode` 基类声明，定义 `ParseNodes()`、`GetMaxChildNodes()`、`ChildNodes` 数组、编辑器接口。 |
| `Classes/Sound/SoundNodeWavePlayer.h` | 叶节点，持有 `TSoftObjectPtr<USoundWave>` 和 `TObjectPtr<USoundWave>`。 |
| `Classes/Sound/SoundNodeMixer.h` | 混合节点，持有 `TArray<float> InputVolume`。 |
| `Classes/Sound/SoundNodeRandom.h` | 随机节点，持有 `TArray<float> Weights` 和运行时状态 `HasBeenUsed`。 |
| `Classes/Sound/SoundNodeLooping.h` | 循环节点，支持有限次循环和无限循环。 |

#### USoundBase：所有可播放音频的统一接口

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundBase.h`，第 107~418 行

```cpp
UCLASS(config=Engine, hidecategories=Object, abstract, editinlinenew, BlueprintType)
class USoundBase : public UObject, public IAudioPropertiesSheetAssetUserInterface, public IInterface_AssetUserData
{
    // ...
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Sound)
    TObjectPtr<USoundClass> SoundClassObject;
    
    UPROPERTY(EditAnywhere, Category = "Effects|Submix")
    TObjectPtr<USoundSubmixBase> SoundSubmixObject;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Submix")
    TArray<FSoundSubmixSendInfo> SoundSubmixSends;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Concurrency")
    TSet<TObjectPtr<USoundConcurrency>> ConcurrencySet;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management")
    EVirtualizationMode VirtualizationMode;
    
    virtual void Parse(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                       FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                       TArray<FWaveInstance*>& WaveInstances);
    // ...
};
```

`USoundBase` 是所有音频资产的统一抽象。无论是直接播放 `USoundWave` 还是通过 `USoundCue` 编排，最终都会调用 `Parse()` 方法生成一组 `FWaveInstance`。`USoundBase` 还统一处理了：
- **SoundClass 路由**：决定默认 Submix、音量分类。
- **并发控制（Concurrency）**：限制同一声音的并发实例数。
- **虚拟化（Virtualization）**：当音源因并发限制被停止时，是静默继续（PlayWhenSilent）、重启（Restart）还是定位恢复（SeekRestart）。

#### USoundCue：节点图资产

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundCue.h`，第 89~348 行

```cpp
UCLASS(hidecategories=object, BlueprintType, meta=(LoadBehavior = "LazyOnDemand"))
class USoundCue : public USoundBase
{
    GENERATED_UCLASS_BODY()
    
    UPROPERTY(BlueprintReadOnly, Category = Sound)
    TObjectPtr<USoundNode> FirstNode;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Sound)
    float VolumeMultiplier;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Sound)
    float PitchMultiplier;
    
#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<TObjectPtr<USoundNode>> AllNodes;
    
    UPROPERTY()
    TObjectPtr<UEdGraph> SoundCueGraph;
#endif
    
    virtual void Parse(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                       FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                       TArray<FWaveInstance*>& WaveInstances) override;
    
    void CacheAggregateValues();
    void PrimeSoundCue();
};
```

`USoundCue` 的核心极其简洁：只有一个 `FirstNode`。所有播放行为都通过递归 `FirstNode->ParseNodes(...)` 来展开。编辑器下的 `AllNodes` 和 `SoundCueGraph` 用于支持可视化编辑。

#### USoundNode：节点基类与编辑器契约

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundNode.h`，第 56~247 行

```cpp
UCLASS(abstract, hidecategories=Object, editinlinenew)
class USoundNode : public UObject
{
    GENERATED_UCLASS_BODY()
    
    static const int32 MAX_ALLOWED_CHILD_NODES = 32;
    
    UPROPERTY(BlueprintReadOnly, Category = "SoundNode")
    TArray<TObjectPtr<class USoundNode>> ChildNodes;
    
    FRandomStream RandomStream;
    
    virtual void ParseNodes(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                            FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                            TArray<FWaveInstance*>& WaveInstances);
    
    virtual int32 GetMaxChildNodes() const { return 1; }
    virtual int32 GetMinChildNodes() const { return 0; }
    
    static UPTRINT GetNodeWaveInstanceHash(const UPTRINT ParentWaveInstanceHash,
                                           const USoundNode* ChildNode, const uint32 ChildIndex);
    
    // Editor-only interface...
};
```

`USoundNode` 的设计模式是**访问者模式（Visitor-like）**的变体：每个节点在 `ParseNodes` 时接收 `FSoundParseParameters`，可以修改参数（如音量、音高、循环标志）后递归调用子节点，也可以直接生成 `FWaveInstance`（只有 `USoundNodeWavePlayer` 这么做）。

---

### 第 2 层：数据层（How / 结构与状态）

#### SoundNode 的运行时状态存储：Payload 系统

SoundCue 的节点图是**静态资产**（编辑时创建，运行时只读），但许多节点需要**每个播放实例的独立状态**（如 Random 节点本次随机选了哪个子节点、Looping 节点当前循环到第几次）。UE 的解决方案是将这些状态存储在 `FActiveSound::SoundNodeData` 中，通过 `NodeWaveInstanceHash` 索引。

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundNode.h`，第 34~54 行

```cpp
#define RETRIEVE_SOUNDNODE_PAYLOAD(Size)
    uint8* Payload = NULL;
    uint32* RequiresInitialization = NULL;
    {
        uint32* TempOffset = ActiveSound.SoundNodeOffsetMap.Find(NodeWaveInstanceHash);
        uint32 Offset;
        if (!TempOffset)
        {
            Offset = ActiveSound.SoundNodeData.AddZeroed(Size + sizeof(uint32));
            ActiveSound.SoundNodeOffsetMap.Add(NodeWaveInstanceHash, Offset);
            RequiresInitialization = (uint32*)&ActiveSound.SoundNodeData[Offset];
            *RequiresInitialization = 1;
            Offset += sizeof(uint32);
        }
        else
        {
            RequiresInitialization = (uint32*)&ActiveSound.SoundNodeData[*TempOffset];
            Offset = *TempOffset + sizeof(uint32);
        }
        Payload = &ActiveSound.SoundNodeData[Offset];
    }
```

**关键设计解读**：
- `FActiveSound` 为每个正在播放的 SoundCue 实例维护一块连续的 `SoundNodeData` 字节数组。
- `SoundNodeOffsetMap`（`TMap<UPTRINT, uint32>`）将 `NodeWaveInstanceHash` 映射到数据偏移。
- 每个节点的 Payload 前面有一个 `uint32` 的 `RequiresInitialization` 标志。当节点第一次被某个 `FActiveSound` 访问时，标志为 1，节点可以在这里初始化自己的状态（如 Random 节点选择索引）。
- 这种模式避免了在 UObject（`USoundNode`）上存储运行时状态，从而支持同一 SoundCue 被成千上万个 `FActiveSound` 并发播放。

#### FSoundParseParameters：递归过程中的参数传递

```cpp
struct FSoundParseParameters
{
    float Volume;
    float Pitch;
    bool bLooping;
    bool bEnableRetrigger;
    // ... 还有更多字段（如 SubmixSends、Attenuation 覆盖等）
};
```

`ParseNodes` 的参数传递方式是**值传递 + 局部修改**：父节点可以基于 `ParseParams` 创建 `UpdatedParams`，修改音量/音高/循环标志后传给子节点。这保证了不同分支的参数互不影响。

#### USoundNodeRandom 的状态布局

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundNodeRandom.h`，第 19~121 行

```cpp
UCLASS(meta=(DisplayName="Random"))
class USoundNodeRandom : public USoundNode
{
    UPROPERTY(EditAnywhere, editfixedsize, Category=Random)
    TArray<float> Weights;
    
    UPROPERTY(transient)
    TArray<bool> HasBeenUsed;      // 运行时状态：哪些分支已被播放过
    
    UPROPERTY(transient)
    int32 NumRandomUsed;           // 已播放的分支计数
    
    UPROPERTY(EditAnywhere, Category=Random)
    int32 PreselectAtLevelLoad;    // 加载时预随机选择的分支数（内存优化）
    
    UPROPERTY(EditAnywhere, Category=Random)
    uint8 bRandomizeWithoutReplacement : 1; // 是否"不放回"随机
};
```

注意：`Weights` 是资产属性（序列化），而 `HasBeenUsed` 和 `NumRandomUsed` 标记为 `transient`，它们只在编辑器下作为资产对象的调试状态存在。**真正的运行时状态**存储在 `FActiveSound::SoundNodeData` 中（通过 `RETRIEVE_SOUNDNODE_PAYLOAD(sizeof(int32))` 存储本次选中的 `NodeIndex`）。

#### FSoundCueParameterTransmitter：参数路由

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundCue.h`，第 350~379 行

```cpp
class FSoundCueParameterTransmitter : public Audio::FParameterTransmitterBase
{
public:
    virtual bool SetParameters(TArray<FAudioParameter>&& InParameters) override;
    void CreateChildTransmitter(UPTRINT InWaveInstanceHash, USoundBase& InSound,
                                Audio::FParameterTransmitterInitParams InTransmitterInitParams);
    void RemoveChildTransmitter(UPTRINT InWaveInstanceHash);
    TArray<FAudioParameter> ReleaseAccumulatedParameterUpdates();
    
    TMap<UPTRINT, TSharedPtr<Audio::IParameterTransmitter>> Transmitters;
};
```

当通过蓝图调用 `SetSoundParameter` 或 `SetFloatParameter` 时，参数变更会先到达 `FSoundCueParameterTransmitter`。由于 SoundCue 解析出的最终播放单元可能是多个 `USoundWave`（通过多个 `USoundNodeWavePlayer`），Transmitter 需要将参数分发给每个子 Wave 的 Transmitter。`Transmitters` 以 `WaveInstanceHash` 为键维护子发射器映射。

---

### 第 3 层：逻辑层（How / 行为与调用链）

#### 播放触发链路：从 UAudioComponent 到 Parse

当蓝图调用 `UAudioComponent::PlaySound(USoundCue*)` 时：

1. **Game Thread**：`UAudioComponent` 请求 `FAudioDevice` 播放 `USoundBase`。
2. **Audio Thread**：`FAudioDevice` 创建 `FActiveSound`，并调用 `USoundCue::Parse(AudioDevice, FirstNodeHash, ActiveSound, ParseParams, WaveInstances)`。
3. **递归解析**：`USoundCue::Parse` 调用 `FirstNode->ParseNodes(...)`，节点树递归展开，最终只有 `USoundNodeWavePlayer` 会生成 `FWaveInstance` 并加入 `WaveInstances` 数组。
4. **实例化音源**：`FAudioDevice` 将 `FWaveInstance` 提交给 `FMixerSourceManager`，分配 `FMixerSource` / `FMixerSourceVoice`，进入 AudioMixer 的实时渲染管线。

#### USoundCue::Parse 源码

> 文件：`Engine/Source/Runtime/Engine/Private/SoundCue.cpp`，第 668~733 行

```cpp
void USoundCue::Parse(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                      FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                      TArray<FWaveInstance*>& WaveInstances)
{
    if (FirstNode)
    {
        FirstNode->ParseNodes(AudioDevice, (UPTRINT)FirstNode, ActiveSound, ParseParams, WaveInstances);
    }
    
    if (FSoundCueParameterTransmitter* Transmitter = static_cast<FSoundCueParameterTransmitter*>(ActiveSound.GetTransmitter()))
    {
        TArray<FAudioParameter> ParameterUpdates = Transmitter->ReleaseAccumulatedParameterUpdates();
        if (ParameterUpdates.IsEmpty()) return;
        if (WaveInstances.IsEmpty()) return;
        
        // 将累积的参数更新分发给所有子 WaveInstance
        for (const FWaveInstance* Instance : WaveInstances)
        {
            if (TSharedPtr<Audio::IParameterTransmitter>* ChildTransmitterPtr = Transmitter->Transmitters.Find(Instance->WaveInstanceHash))
            {
                if (ChildTransmitterPtr->IsValid())
                {
                    // ... UpdateChildParameters
                }
            }
        }
    }
}
```

可以看到，`USoundCue::Parse` 的逻辑非常清晰：先递归解析节点树生成 WaveInstances，再将 Game Thread 累积的参数更新路由给这些 WaveInstances。

#### USoundNodeWavePlayer::ParseNodes：唯一产生声音的叶节点

> 文件：`Engine/Source/Runtime/Engine/Private/SoundNodeWavePlayer.cpp`，第 151~223 行

```cpp
void USoundNodeWavePlayer::ParseNodes(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                                      FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                                      TArray<FWaveInstance*>& WaveInstances)
{
    if (bAsyncLoading)
    {
        ActiveSound.bFinished = false; // 异步加载中，暂不结束
        return;
    }
    
    if (SoundWave)
    {
        RETRIEVE_SOUNDNODE_PAYLOAD(sizeof(int32));
        DECLARE_SOUNDNODE_ELEMENT(int32, bPlayFailed);
        
        if (*RequiresInitialization)
        {
            bPlayFailed = 0;
            
            // 创建子参数发射器，用于将 SoundCue 参数传递给 SoundWave
            if (FSoundCueParameterTransmitter* SoundCueTransmitter = ...)
            {
                Audio::FParameterTransmitterInitParams Params;
                Params.InstanceID = Audio::GetTransmitterID(...);
                Params.SampleRate = AudioDevice->GetSampleRate();
                Params.AudioDeviceID = AudioDevice->DeviceID;
                SoundCueTransmitter->CreateChildTransmitter(NodeWaveInstanceHash, *SoundWave, MoveTemp(Params));
            }
            *RequiresInitialization = 0;
        }
        
        // 临时关闭 SoundWave 自身的循环标志（循环由 SoundCue 节点图控制）
        const bool bWaveIsLooping = SoundWave->bLooping;
        SoundWave->bLooping = false;
        
        if (bLooping || (SoundWave->bProcedural && !SoundWave->IsOneShot()))
        {
            FSoundParseParameters UpdatedParams = ParseParams;
            UpdatedParams.bLooping = true;
            SoundWave->Parse(AudioDevice, NodeWaveInstanceHash, ActiveSound, UpdatedParams, WaveInstances);
        }
        else if (ParseParams.bEnableRetrigger)
        {
            SoundWave->Parse(AudioDevice, NodeWaveInstanceHash, ActiveSound, ParseParams, WaveInstances);
        }
        else
        {
            if (!ActiveSound.bHasVirtualized && bPlayFailed == 0)
            {
                const int32 InitWaveInstancesNum = WaveInstances.Num();
                SoundWave->Parse(AudioDevice, NodeWaveInstanceHash, ActiveSound, ParseParams, WaveInstances);
                
                const bool bFailed = (WaveInstances.Num() == InitWaveInstancesNum);
                if (bFailed)
                {
                    bPlayFailed = 1; // 避免重复解析已失败的播放
                }
            }
        }
        
        SoundWave->bLooping = bWaveIsLooping;
    }
}
```

**关键行为解读**：
- `USoundNodeWavePlayer` 通过 `RETRIEVE_SOUNDNODE_PAYLOAD` 存储 `bPlayFailed` 标志，防止因并发限制或虚拟化导致的一帧内反复解析失败。
- 它临时禁用 `SoundWave->bLooping`，因为 SoundCue 的循环逻辑由 `USoundNodeLooping` 控制，而非 Wave 本身。如果 Wave Player 节点勾选了 `bLooping`，则重新设置 `UpdatedParams.bLooping = true`。
- 参数发射器在这里创建，将 SoundCue 的参数系统桥接到 SoundWave 的参数系统。

#### USoundNodeMixer::ParseNodes：多路混合

> 文件：`Engine/Source/Runtime/Engine/Private/SoundNodeMixer.cpp`，第 17~29 行

```cpp
void USoundNodeMixer::ParseNodes(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                                 FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                                 TArray<FWaveInstance*>& WaveInstances)
{
    FSoundParseParameters UpdatedParams = ParseParams;
    for (int32 ChildNodeIndex = 0; ChildNodeIndex < ChildNodes.Num(); ++ChildNodeIndex)
    {
        if (ChildNodes[ChildNodeIndex])
        {
            UpdatedParams.Volume = ParseParams.Volume * InputVolume[ChildNodeIndex];
            ChildNodes[ChildNodeIndex]->ParseNodes(
                AudioDevice,
                GetNodeWaveInstanceHash(NodeWaveInstanceHash, ChildNodes[ChildNodeIndex], ChildNodeIndex),
                ActiveSound, UpdatedParams, WaveInstances);
        }
    }
}
```

Mixer 节点将每个子分支的音量按 `InputVolume` 缩放后递归解析。注意 `GetNodeWaveInstanceHash` 的使用：它通过组合父 Hash、子节点指针和子索引，生成唯一的实例标识符，确保每个分支在 `FActiveSound::SoundNodeData` 中有独立的 Payload 存储。

#### USoundNodeRandom::ParseNodes：随机选择

> 文件：`Engine/Source/Runtime/Engine/Private/SoundNodeRandom.cpp`，第 201~250 行

```cpp
void USoundNodeRandom::ParseNodes(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash,
                                  FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams,
                                  TArray<FWaveInstance*>& WaveInstances)
{
    RETRIEVE_SOUNDNODE_PAYLOAD(sizeof(int32));
    DECLARE_SOUNDNODE_ELEMENT(int32, NodeIndex);
    
    if (*RequiresInitialization)
    {
        NodeIndex = ChooseNodeIndex(ActiveSound); // 根据 Weights 随机选择
        *RequiresInitialization = 0;
    }
    
    // "Without Replacement" 逻辑：若所有分支都已播放过，重置状态
    if (bRandomizeWithoutReplacement && (NumRandomUsed >= HasBeenUsed.Num()))
    {
        for (int32 i = 0; i < HasBeenUsed.Num(); ++i)
        {
            HasBeenUsed[i] = false;
        }
        if (HasBeenUsed.IsValidIndex(NodeIndex))
        {
            HasBeenUsed[NodeIndex] = true; // 刚播放过的不重复
        }
        NumRandomUsed = 1;
    }
    
    if (NodeIndex < ChildNodes.Num() && ChildNodes[NodeIndex])
    {
        ChildNodes[NodeIndex]->ParseNodes(
            AudioDevice,
            GetNodeWaveInstanceHash(NodeWaveInstanceHash, ChildNodes[NodeIndex], NodeIndex),
            ActiveSound, ParseParams, WaveInstances);
    }
}
```

Random 节点是理解 `RETRIEVE_SOUNDNODE_PAYLOAD` 设计的最佳示例：
- `NodeIndex` 是本次播放实例选中的分支索引，存储在 `FActiveSound` 的私有数据中。
- `bRandomizeWithoutReplacement` 提供类似洗牌随机的保证：在 N 次播放内不会重复同一分支（N = 子节点数）。
- `HasBeenUsed` / `NumRandomUsed` 是 `USoundNodeRandom` 对象上的 transient 字段，用于编辑器预览和在 `ChooseNodeIndex` 中计算全局随机状态；但每个 `FActiveSound` 实例的选择结果还是通过 Payload 存储。

#### USoundNodeLooping：逻辑循环与状态重置

> 文件：`Engine/Source/Runtime/Engine/Classes/Sound/SoundNodeLooping.h`，第 23~47 行

```cpp
UCLASS(meta=(DisplayName="Looping"))
class USoundNodeLooping : public USoundNode
{
    UPROPERTY(EditAnywhere, Category = Looping, meta = (ClampMin = 1))
    int32 LoopCount;
    
    UPROPERTY(EditAnywhere, Category = Looping)
    uint32 bLoopIndefinitely : 1;
    
    virtual bool NotifyWaveInstanceFinished(struct FWaveInstance* WaveInstance) override;
    virtual void ParseNodes(...) override;
    virtual int32 GetNumSounds(...) const;
    
private:
    void ResetChildren(const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, int32& CurrentLoopCount);
};
```

Looping 节点的核心机制：
1. 在 `ParseNodes` 中，它先调用子节点解析，然后如果 `bLoopIndefinitely` 为 true，通过设置 `ActiveSound.bFinished = false` 让 `FActiveSound` 保持存活。
2. 当子 WaveInstance 完成播放时，`NotifyWaveInstanceFinished` 被调用。如果还有剩余循环次数，调用 `ResetChildren` 重置子节点的 `RequiresInitialization` 标志（这样 `USoundNodeRandom` 等状态ful 节点可以重新选择），然后重新触发解析。

这解释了为什么文档注释说："Looping 节点适用于逻辑/程序性循环（如引入延迟），但不适合无缝循环 —— 无缝循环应使用 Wave Player 的 Looping 标志。"

---

## 上下层模块关系

### 下层依赖

| 模块 | 关系 |
|---|---|
| **AudioMixer / AudioMixerCore** | SoundCue 本身不直接依赖 AudioMixer，但通过 `FAudioDevice::Parse` 调用链，最终生成的 `FWaveInstance` 会进入 `FMixerSourceManager`。SoundCue 是 AudioMixer 的"上游编排器"。 |
| **AudioExtensions** | `USoundBase::CreateParameterTransmitter` 返回的 `TSharedPtr<Audio::IParameterTransmitter>` 属于 AudioExtensions 接口；`USoundNodeAttenuation` 引用的衰减设置也与 AudioExtensions 中的空间化插件数据交互。 |

### 上层消费

| 系统 | 关系 |
|---|---|
| **Gameplay / Blueprint** | 设计师直接在蓝图中调用 `PlaySoundAtLocation(USoundCue*)` 或设置 `UAudioComponent::SetSound(USoundCue*)`。SoundCue 是音频内容消费的最常见资产类型。 |
| **Sound Cue Editor** | `ISoundCueAudioEditor` 接口由 `AudioEditor` 模块实现，负责将 `USoundCue` 的节点树可视化为节点图编辑器（类似材质编辑器）。 |
| **Cook / 打包** | `USoundNodeRandom` 的 `PreselectAtLevelLoad` 和平台特定的 `au.MaxRandomBranches` CVar 在 Cook 阶段会裁剪未选中的分支，减少内存占用。 |

---

## 设计亮点与可迁移经验

### 1. 静态资产图 + 运行时 Payload 分离
SoundCue 的节点树是静态的 UObject 图（编辑时构建），但每个播放实例的状态存储在 `FActiveSound::SoundNodeData` 中。这种分离使得：
- 同一 SoundCue 可以被无限并发播放，每个实例状态完全独立。
- 资产本身无需线程同步即可被多线程读取（因为运行时状态不在资产上）。

### 2. Hash 驱动的实例级状态索引
`GetNodeWaveInstanceHash(ParentHash, ChildNode, ChildIndex)` 用简单的位组合生成唯一键，避免了为每个节点实例分配独立对象。结合 `TMap<UPTRINT, uint32>` 的偏移映射，状态访问开销极低。

### 3. 纯递归的解析模型
`ParseNodes` 采用纯递归+参数传递模型，没有显式的执行栈或状态机。这带来了极大的可扩展性：新增一种节点只需继承 `USoundNode` 并重写 `ParseNodes`，无需修改现有代码。

### 4. 参数系统的分层桥接
`FSoundCueParameterTransmitter` 作为"中间人"，将 Game Thread 的参数变更分发给 SoundCue 内部多个子 Wave 的 Transmitter。这种设计允许：
- SoundCue 层统一接收参数（如"Engine RPM"）。
- 不同的 WavePlayer 子节点各自将参数映射到其 SoundWave 的特定参数上。

### 5. 随机节点的内存优化策略
`PreselectAtLevelLoad` 和 `au.MaxRandomBranches` CVar 允许在加载时或 Cook 阶段裁剪 Random 节点的未选中分支。这对移动端等内存受限平台极为重要 —— 一个包含 20 个变体的脚步声 SoundCue 可能只在运行时保留 3~4 个分支的 Wave 引用。

---

## 关联阅读

- [[UE-AudioMixer-源码解析：音频核心与混音]] — 深入理解 SoundCue 生成的 `FWaveInstance` 如何在 AudioMixer 中被调度和渲染。
- [[UE-AudioExtensions-源码解析：音频扩展与空间化]] — 理解 SoundCue 中 Attenuation 节点与空间化/遮挡插件的交互。
- [[UE-Engine-源码解析：Actor 与 Component 模型]] — 理解 `UAudioComponent` 的生命周期及其与 `FActiveSound` 的对应关系。
- [[UE-Engine-源码解析：World 与 Level 架构]] — 理解 `FAudioDevice` 如何在 World 级别管理所有活跃声音。

---

## 索引状态

- **所属阶段**：第三阶段-核心层 → 3.5 音频系统
- **笔记名称**：[[UE-Engine-源码解析：SoundCue 与音频蓝图]]
- **分析完成度**：✅ 完整三层分析（接口层 + 数据层 + 逻辑层）
- **对应 UE 模块**：`Runtime/Engine/Classes/Sound/`
