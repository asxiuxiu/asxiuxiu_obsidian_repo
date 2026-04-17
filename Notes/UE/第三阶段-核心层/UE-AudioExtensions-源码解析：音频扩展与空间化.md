---
title: UE-AudioExtensions-源码解析：音频扩展与空间化
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - audio-extensions
  - soundfield
aliases:
  - UE-AudioExtensions-源码解析
---

> [← 返回 [[00-UE全解析主索引|UE全解析主索引]]]

# UE-AudioExtensions 源码解析：音频扩展与空间化

## Why：为什么需要 AudioExtensions？

在基础混音器（AudioMixer）之上，现代游戏音频还需要大量**可插拔的扩展能力**：

1. **空间音频**：HRTF（头相关传输函数）、Ambisonics、对象音频（Object-Based Audio）—— 这些算法高度专业化，不同项目需求差异极大，不适合硬编码在 AudioMixer 内部。
2. **外部输出**：音频不仅要输出到默认声卡，还可能需要输出到触觉反馈设备、网络流媒体、VR 头显专用音频设备、多声道环绕声解码器等。
3. **调制系统**：音量、音高、滤波器截止频率需要被 LFO、Envelope、Game Parameter（如玩家生命值）动态调制，这要求一套统一的参数路由框架。
4. **参数驱动**：随着 MetaSounds 等新系统的引入，音频参数（`FAudioParameter`）需要跨模块、跨线程安全传递。

UE 的解决方案是 **AudioExtensions** 模块 —— 一个纯接口与数据定义层，将上述能力抽象为可插拔的插件系统。它不实现任何具体算法，但定义了 AudioMixer 与外部插件之间的全部契约。

---

## What：AudioExtensions 是什么？

AudioExtensions 是 UE 音频系统的**扩展插件契约层**，位于 `Runtime/AudioExtensions/`。它向下被 `AudioMixer` 依赖，向上被具体插件（如 SoundFieldRendering、第三方 HRTF 插件）实现。

### 模块依赖

> 文件：`Engine/Source/Runtime/AudioExtensions/AudioExtensions.Build.cs`

```csharp
public class AudioExtensions : ModuleRules
{
    PublicDependencyModuleNames.AddRange(
        new string[] { "Core", "CoreUObject", "SignalProcessing", "AudioMixerCore" }
    );
    PublicIncludePathModuleNames.AddRange(
        new string[] { "AudioMixer" }
    );
}
```

**关键观察**：AudioExtensions 只 Public 依赖 `Core`、`CoreUObject`、`SignalProcessing`、`AudioMixerCore`。它通过 `PublicIncludePathModuleNames` 暴露 `AudioMixer` 的头文件路径，这意味着 AudioMixer 的类（如 `FMixerSubmix`、`FMixerSourceManager`）可以作为接口参数类型出现在 AudioExtensions 的公共 API 中，但 AudioExtensions **并不在链接层直接依赖 AudioMixer**。这种设计避免了 AudioMixer 与 AudioExtensions 之间的循环依赖。

### 核心子系统分类

| 子系统 | 核心头文件 | 用途 |
|---|---|---|
| **音频插件接口** | `IAudioExtensionPlugin.h` | 定义空间化（Spatialization）、遮挡（Occlusion）、混响（Reverb）、数据源覆盖（SourceDataOverride）、调制（Modulation）五类插件的 Factory + Instance 接口。 |
| **Soundfield 格式** | `ISoundfieldFormat.h` | 定义 Ambisonics 等声场格式的编码器（Encoder）、解码器（Decoder）、转码器（Transcoder）、混音器（Mixer）接口。 |
| **Soundfield Endpoint** | `ISoundfieldEndpoint.h` | 定义接收声场编码数据的外部输出端点（如专用空间音频渲染器）。 |
| **普通 Endpoint** | `IAudioEndpoint.h` | 定义接收标准交错 PCM 浮点数据的外部输出端点（如触觉设备、网络音频）。 |
| **音频调制** | `IAudioModulation.h` | 定义调制参数（`FModulationParameter`）、调制器句柄（`FModulatorHandle`）和调制管理器（`IAudioModulationManager`）。 |
| **音频参数** | `AudioParameter.h` | 定义 `FAudioParameter` 结构体，支持 Float/Bool/Int/String/Object 及其数组，以及 Trigger 类型。 |
| **参数发射器** | `IAudioParameterTransmitter.h` | 定义 `IParameterTransmitter`，用于将参数变更从 Game Thread 路由到音频实例。 |

---

## How：三层剥离法分析

### 第 1 层：接口层（What / 模块边界）

#### IAudioExtensionPlugin：五大音频插件类型

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/IAudioExtensionPlugin.h`，第 39~728 行

AudioExtensions 将可扩展音频能力归纳为 **5 种插件类型**：

```cpp
enum class EAudioPlugin : uint8
{
    SPATIALIZATION = 0,   // HRTF / Object-Based 空间化
    REVERB = 1,           // 源级混响（Per-Source Reverb）
    OCCLUSION = 2,        // 遮挡/衍射处理
    MODULATION = 3,       // LFO / Envelope / Game Parameter 调制
    SOURCEDATAOVERRIDE = 4 // 覆盖音源位置/传播路径（如墙角绕射模拟）
};
```

每种插件都遵循统一的 **Factory + Instance** 双层接口模式：

| 角色 | 接口名 | 职责 |
|---|---|---|
| **Factory** | `IAudioSpatializationFactory` / `IAudioReverbFactory` / ... | 继承 `IAudioPluginFactory` + `IModularFeature`，通过 UE 的模块化特性系统注册。负责创建插件实例、声明平台支持、提供自定义设置 UClass。 |
| **Instance** | `IAudioSpatialization` / `IAudioReverb` / ... | 每 `FAudioDevice` 一个实例（或每 Source 一个效果）。负责实际的音频处理。 |

**以空间化插件为例**：

```cpp
class IAudioSpatializationFactory : public IAudioPluginFactory, public IModularFeature
{
public:
    static FName GetModularFeatureName() { return FName(TEXT("AudioSpatializationPlugin")); }
    virtual int32 GetMaxSupportedChannels() { return 1; }
    virtual TAudioSpatializationPtr CreateNewSpatializationPlugin(FAudioDevice* OwningDevice) = 0;
    virtual UClass* GetCustomSpatializationSettingsClass() const { return nullptr; }
};

class IAudioSpatialization
{
public:
    virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) {}
    virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, USpatializationPluginSourceSettingsBase* InSettings) {}
    virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) {}
    virtual void OnAllSourcesProcessed() {}
    virtual void OnReleaseSource(const uint32 SourceId) {}
};
```

> 注意：所有实例接口都有默认空实现，这意味着插件可以选择性重写自己关心的方法，降低了接入门槛。

#### Soundfield 系统：编码-解码-转码-混合

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/ISoundfieldFormat.h`，第 52~454 行

Soundfield 是 AudioExtensions 中最复杂的接口族。它允许音频在**声场域**（而非传统的扬声器域）中进行处理、混合和传输。核心接口包括：

| 接口 | 职责 |
|---|---|
| `ISoundfieldAudioPacket` | 一帧声场数据的 opaque 容器。对 Ambisonics 而言可能是交错浮点缓冲；对专有格式可能是比特流。 |
| `ISoundfieldEncodingSettingsProxy` | 编码设置的渲染线程安全代理（对应编辑器侧的 `USoundfieldEncodingSettingsBase` UObject）。 |
| `ISoundfieldEncoderStream` | 将交错 PCM 编码为声场包。 |
| `ISoundfieldDecoderStream` | 将声场包解码为交错 PCM。 |
| `ISoundfieldTranscodeStream` | 在不同声场格式之间转换（如 Ambisonics 1st order → 3rd order，或 Ambisonics →  proprietary format）。 |
| `ISoundfieldMixerStream` | 将多个声场包叠加为一个。 |
| `ISoundfieldFactory` | 工厂接口，注册为 `IModularFeature`，创建上述所有流实例。 |
| `ISoundfieldEffectInstance` | 在声场域上运行的效果器（如 Ambisonics 旋转器）。 |

Soundfield 的关键设计是 **"按需实例化流"**：`FMixerSubmix` 在 `SetupSoundfieldStreams` 时，根据当前 Submix 的父子关系和连接到的格式，动态创建 `Encoder` / `Decoder` / `Transcoder` / `Mixer`。这使得同一个 `USoundSubmix` 可以在运行时切换为不同的空间音频格式。

#### Endpoint 系统：音频的"多设备输出"

AudioExtensions 定义了两类 Endpoint：

**1. 普通 Endpoint（`IAudioEndpoint`）**

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/IAudioEndpoint.h`，第 80~193 行

接收标准交错 PCM 浮点缓冲。典型用途：触觉反馈、网络音频、额外硬件输出。

```cpp
class IAudioEndpoint
{
public:
    virtual ~IAudioEndpoint() {};
    virtual float GetSampleRate() const;           // 子类必须覆盖
    virtual int32 GetNumChannels() const;          // 子类必须覆盖
    virtual bool OnAudioCallback(const TArrayView<const float>& InAudio, const int32& NumChannels, const IAudioEndpointSettingsProxy* InSettings) { return false; }
protected:
    Audio::FPatchInput PatchNewInput(float ExpectedDurationPerRender, float& OutSampleRate, int32& OutNumChannels);
    int32 PopAudio(float* OutAudio, int32 NumSamples);
};
```

`IAudioEndpoint` 内部使用 `Audio::FPatchMixer` 来混合所有输入（通过 `PatchNewInput` 创建的 `FPatchInput`），并在回调线程中 `PopAudio` 获取混合后的音频。

**2. Soundfield Endpoint（`ISoundfieldEndpoint`）**

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/ISoundfieldEndpoint.h`，第 63~158 行

接收 `ISoundfieldAudioPacket` 而非 PCM。典型用途：专用 Ambisonics 解码器、Dolby Atmos 渲染器、VR 音频中间件。

```cpp
class ISoundfieldEndpoint
{
public:
    ISoundfieldEndpoint(int32 NumRenderCallbacksToBuffer);
    bool PushAudio(TUniquePtr<ISoundfieldAudioPacket>&& InPacket);
    virtual void OnAudioCallback(TUniquePtr<ISoundfieldAudioPacket>&& InPacket, const ISoundfieldEndpointSettingsProxy* InSettings) {}
protected:
    TUniquePtr<ISoundfieldAudioPacket> PopAudio();
};
```

`ISoundfieldEndpoint` 内部维护一个 `TArray<TUniquePtr<ISoundfieldAudioPacket>>` 作为环形缓冲，通过 `FThreadSafeCounter` 实现无锁读写计数器。

---

### 第 2 层：数据层（How / 结构与状态）

#### 音频参数系统：FAudioParameter

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/AudioParameter.h`，第 91~344 行

`FAudioParameter` 是 UE 音频系统中跨模块、跨线程传递参数的通用数据包：

```cpp
USTRUCT(BlueprintType)
struct FAudioParameter
{
    GENERATED_USTRUCT_BODY()
    FName ParamName;
    float FloatParam = 0.f;
    bool BoolParam = false;
    int32 IntParam = 0;
    TObjectPtr<UObject> ObjectParam = nullptr;
    FString StringParam;
    TArray<float> ArrayFloatParam;
    // ... BooleanArray, IntegerArray, ObjectArray, StringArray
    EAudioParameterType ParamType = EAudioParameterType::None;
    TArray<TSharedPtr<Audio::IProxyData>> ObjectProxies;
};
```

**内存安全设计**：
- 当参数包含 `UObject`（如 `USoundWave`）时，直接传递裸指针在 Audio Render Thread 上是不安全的。
- `FAudioParameter` 通过 `ObjectProxies`（`TSharedPtr<Audio::IProxyData>`）在跨线程前将 UObject 转换为线程安全的代理数据。这是 MetaSounds 和 SoundCue 参数系统能够安全共存的基础。

#### 调制系统：FModulatorHandle 与 IAudioModulationManager

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/IAudioModulation.h`，第 128~216 行

```cpp
struct FModulatorHandle
{
    FModulationParameter Parameter;
    FModulatorHandleId HandleId = INDEX_NONE;
    FModulatorTypeId ModulatorTypeId = INDEX_NONE;
    FModulatorId ModulatorId = INDEX_NONE;
    TWeakPtr<IAudioModulationManager> Modulation;
};

class IAudioModulationManager : public TSharedFromThis<IAudioModulationManager>
{
public:
    virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) = 0;
    virtual void ProcessModulators(const double InElapsed) = 0;
    virtual void UpdateModulator(const USoundModulatorBase& InModulator) = 0;
protected:
    virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const = 0;
    virtual void UnregisterModulator(const Audio::FModulatorHandle& InHandle) = 0;
};
```

**状态流转**：
1. **Game Thread**：`USoundModulatorBase` UObject 被编辑/修改。
2. **音频线程**：通过 `UpdateModulator` 将 UObject 状态同步到 `IAudioModulationManager`。
3. **音频渲染线程**：每帧 `ProcessModulators` 更新所有调制器的当前值（如 LFO 的正弦波值、Envelope 的包络值）。
4. **音源处理**：`FMixerSourceManager` 在 `ComputeSourceBuffer` 前后，通过 `FModulatorHandle::GetValue()` 查询当前调制值，应用到音量、音高、滤波器频率。

`FModulatorHandle` 是跨线程引用的关键：它在 Game Thread 创建，持有弱引用到 `IAudioModulationManager`，并在渲染线程安全查询值。如果调制管理器已销毁，句柄自动失效。

#### SoundfieldEncodingKey：避免重复编码的缓存键

> 文件：`Engine/Source/Runtime/AudioExtensions/Public/ISoundfieldFormat.h`，第 427~454 行

```cpp
struct FSoundfieldEncodingKey
{
    FName SoundfieldFormat;
    int32 EncodingSettingsID;
    
    inline bool operator==(const FSoundfieldEncodingKey& Other) const
    {
        return (SoundfieldFormat == Other.SoundfieldFormat) && (EncodingSettingsID == Other.EncodingSettingsID);
    }
};
```

在 `FMixerSourceManager` 中，如果一个 Source 同时发送到多个相同格式和相同设置的 Soundfield Submix，UE 会通过 `FSoundfieldEncodingKey` 去重，确保只编码一次，显著减少 CPU 开销。

---

### 第 3 层：逻辑层（How / 行为与调用链）

#### 插件生命周期：从模块加载到音频处理

以 **Spatialization Plugin** 为例：

1. **模块初始化**：第三方插件模块在 `StartupModule()` 中构造自己的 `IAudioSpatializationFactory` 子类，并调用 `IModularFeatures::Get().RegisterModularFeature(IAudioSpatializationFactory::GetModularFeatureName(), this);`

2. **设备初始化**：`FMixerDevice::InitializeHardware()` 中，通过 `AudioPluginUtilities::GetDesiredSpatializationPluginName()` 查找匹配的 Factory，然后调用 `CreateNewSpatializationPlugin(this)` 创建实例。

3. **音源初始化**：当 `UAudioComponent` 播放一个使用了空间化设置（`USpatializationPluginSourceSettingsBase`）的声音时，`FMixerSourceManager::InitSource` 调用 `IAudioSpatialization::OnInitSource(SourceId, AudioComponentUserId, Settings)`。

4. **一帧处理**：在 `FMixerSourceManager::ComputeNextBlockOfSamples()` 中：
   - 对每个活跃音源调用 `IAudioSpatialization::ProcessAudio(InputData, OutputData)` 进行空间化。
   - 所有音源处理完毕后，调用 `IAudioSpatialization::OnAllSourcesProcessed()`（某些插件需要批量后处理，如基于场景的全局 HRTF 优化）。

5. **释放**：音源停止时，`FMixerSourceManager::ReleaseSource` 调用 `OnReleaseSource(SourceId)`。

#### Soundfield Submix 的渲染链路

在 [[UE-AudioMixer-源码解析：音频核心与混音]] 中，我们追踪了 `FMixerSubmix::ProcessAudio`。当 Submix 是 Soundfield Submix 时，流程变为：

> 文件：`Engine/Source/Runtime/AudioMixer/Private/AudioMixerSubmix.cpp`，第 1290~1326 行

```cpp
void FMixerSubmix::ProcessAudio(FAlignedFloatBuffer& OutAudioBuffer)
{
    if (IsSoundfieldSubmix())
    {
        FScopeLock ScopeLock(&SoundfieldStreams.StreamsLock);
        
        // 1. 初始化或清空 MixedDownAudio 包
        if (!SoundfieldStreams.MixedDownAudio.IsValid())
        {
            SoundfieldStreams.MixedDownAudio = SoundfieldStreams.Factory->CreateEmptyPacket();
        }
        else
        {
            SoundfieldStreams.MixedDownAudio->Reset();
        }
        
        // 2. 在声场域内递归处理子 Submix 和 Source
        ProcessAudio(*SoundfieldStreams.MixedDownAudio);
        
        // 3. 使用 ParentDecoder 将声场包解码为 PCM 并混合到父 Submix
        if (SoundfieldStreams.ParentDecoder.IsValid())
        {
            FSoundfieldDecoderInputData DecoderInput = {
                *SoundfieldStreams.MixedDownAudio,
                SoundfieldStreams.CachedPositionalData,
                MixerDevice->GetNumOutputFrames(),
                MixerDevice->GetSampleRate()
            };
            FSoundfieldDecoderOutputData DecoderOutput = { OutAudioBuffer };
            SoundfieldStreams.ParentDecoder->DecodeAndMixIn(DecoderInput, DecoderOutput);
        }
        return;
    }
    // ... 普通 PCM Submix 处理
}
```

**这里的 `ProcessAudio(ISoundfieldAudioPacket&)` 重载做了什么？**

> 文件：`Engine/Source/Runtime/AudioMixer/Private/AudioMixerSubmix.cpp`（对应 Soundfield 重载）

1. **混合 Child Submixes**：对每个子 Submix，如果子 Submix 是普通 PCM Submix，通过 `ChildSubmixEntry.Value.Encoder->EncodeAndMixIn(...)` 将其 PCM 输出编码为声场包后累加；如果子 Submix 本身也是 Soundfield Submix，通过 `Transcoder->TranscodeAndMixIn(...)` 转码后累加。

2. **混合 Source Voices**：遍历 `MixerSourceVoices`，对每个 Source 调用 `FMixerSourceManager::GetEncodedOutput(SourceId, EncodingKey)` 获取该 Source 已编码好的声场包，然后通过 `SoundfieldStreams.Mixer->MixTogether(...)` 累加到 `MixedDownAudio`。

3. **应用 Soundfield 效果器**：遍历 `SoundfieldStreams.EffectProcessors`，调用 `Processor->ProcessAudio(InOutPacket, EncodingSettings, ProcessorSettings)`。

4. **输出到 Endpoint**：如果配置了 Soundfield Endpoint，将 `MixedDownAudio` 推送到 `ISoundfieldEndpoint`。

#### Endpoint Submix 的音频分路

`FMixerDevice::OnProcessAudioStream` 在 Master Submix 处理完成后，还会处理两类 Endpoint Submixes：

```cpp
// DefaultEndpointSubmixes：未指定外部 Endpoint 的 Submix，最终混合到主输出
for (const FMixerSubmixPtr& Submix : DefaultEndpointSubmixes)
{
    Submix->ProcessAudio(Output);
}

// ExternalEndpointSubmixes：发送到外部硬件/网络/中间件
for (FMixerSubmixPtr& Submix : ExternalEndpointSubmixes)
{
    Submix->ProcessAudioAndSendToEndpoint();
}
```

对于普通 `IAudioEndpoint`，`FMixerSubmix` 会在 `ProcessAudioAndSendToEndpoint()` 中：
1. 调用 `ProcessAudio(EndpointBuffer)` 生成 PCM。
2. 如果 Endpoint 采样率/通道数与设备不匹配，通过 `Audio::FResampler` 重采样、通过 `DownmixBuffer` 进行通道映射。
3. 通过 `EndpointData.Input.PushAudio(...)` 将音频推送到 Endpoint 的 `FPatchMixer`。
4. 如果 Endpoint 需要回调（`EndpointRequiresCallback()`），由 `IAudioEndpoint` 的内部线程定期 `PopAudio` 并调用 `OnAudioCallback`。

---

## 上下层模块关系

### 下层依赖

| 模块 | 关系 |
|---|---|
| **AudioMixerCore** | 提供 `IAudioMixer` 回调接口签名、`FOutputBuffer`、通道枚举、`Audio::FAlignedFloatBuffer` 等底层类型。AudioExtensions 的效果器接口参数中大量使用了这些类型。 |
| **SignalProcessing** | 提供 DSP 数学原语。Soundfield 的 Encoder/Decoder 实现通常依赖 SignalProcessing 的 FFT、矩阵运算等工具。 |

### 上层消费与横向关联

| 模块 | 关系 |
|---|---|
| **AudioMixer** | AudioMixer 是 AudioExtensions 接口的主要消费者。`FMixerSourceManager` 调用 `IAudioSpatialization::ProcessAudio`；`FMixerSubmix` 调用 `ISoundfieldEncoderStream::EncodeAndMixIn`；`FMixerDevice` 创建并持有所有音频插件实例。 |
| **SoundFieldRendering** | UE 内置的 Ambisonics 实现模块，实现了 `ISoundfieldFactory` 和相关的 Encoder/Decoder/Transcoder/Mixer。 |
| **NonRealtimeAudioRenderer** | 非实时音频渲染器（如离线混音、音频导出），依赖 AudioExtensions 的 Endpoint 接口将渲染结果输出到文件。 |
| **Engine** | `FAudioDevice` 在初始化时通过 `AudioPluginUtilities` 发现注册的音频插件 Factory；`USoundSubmix` 的资产属性中引用了 `USoundfieldEncodingSettingsBase` 等 UObject。 |

---

## 设计亮点与可迁移经验

### 1. 纯接口层 + 模块化特性注册
AudioExtensions 没有任何具体算法实现，它完全是一个**契约层**。所有扩展点都通过 `IModularFeature` 注册，这意味着第三方插件无需修改引擎源码，只需编写自己的模块并实现对应接口即可扩展音频能力。这种架构对大型商业引擎至关重要。

### 2. 线程安全的代理模式（Proxy Pattern）
无论是 `ISoundfieldEncodingSettingsProxy`、`IAudioEndpointSettingsProxy` 还是 `FAudioParameter::ObjectProxies`，AudioExtensions 都遵循同一个模式：
- **Game Thread**：维护可编辑的 UObject（`USoundfieldEncodingSettingsBase`、`UAudioEndpointSettingsBase`、`USoundWave`）。
- **跨线程前**：将 UObject 的数据提取到轻量级、无 GC 的 Proxy 对象中。
- **Audio Render Thread**：只访问 Proxy，绝不直接触碰 UObject。

这是 UE 音频系统能够在实时音频回调中安全使用 UObject 资产数据的核心设计。

### 3. 声场域与 PCM 域的分离
Soundfield 接口族将"声场编码音频"视为与 PCM 平行的一等公民。Submix 图可以在声场域内完成混合和效果处理，只在最终需要输出到扬声器时才解码。这带来了极大的灵活性：
- 同一声场 Submix 可以输出到不同布局的扬声器系统（立体声、5.1、7.1、HRTF）。
- 声场效果器（如 Ambisonics 旋转）无需关心具体扬声器布局。

### 4. 调制系统的解耦
`IAudioModulationManager` 是一个独立的插件类型，但它不直接修改音频缓冲区。相反，它只负责计算**标量调制值**（0~1 范围的浮点数），由 `FMixerSourceManager` 在合适时机查询并应用到具体参数上。这种"拉取式"（Pull）设计避免了调制插件与混音器之间的紧耦合。

---

## 关联阅读

- [[UE-AudioMixer-源码解析：音频核心与混音]] — 深入理解 AudioExtensions 接口的主要消费者，以及 Submix、SourceManager 的渲染流程。
- [[UE-Engine-源码解析：Actor 与 Component 模型]] — 理解 `UAudioComponent` 如何触发音频播放，以及插件设置 UObject 如何绑定到组件。
- [[UE-Engine-源码解析：World 与 Level 架构]] — 理解 `FAudioDevice` 的生命周期与世界关联。
- [[UE-Core-源码解析：委托与事件系统]] — AudioExtensions 中的 `IAudioPluginListener` 使用了类似的回调订阅模式。

---

## 索引状态

- **所属阶段**：第三阶段-核心层 → 3.5 音频系统
- **笔记名称**：[[UE-AudioExtensions-源码解析：音频扩展与空间化]]
- **分析完成度**：✅ 完整三层分析（接口层 + 数据层 + 逻辑层）
- **对应 UE 模块**：`Runtime/AudioExtensions/`
