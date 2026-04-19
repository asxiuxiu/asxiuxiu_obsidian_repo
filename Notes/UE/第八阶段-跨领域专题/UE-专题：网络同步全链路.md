---
title: UE-专题：网络同步全链路
date: 2026-04-18
tags:
  - ue-source
  - engine-architecture
  - networking
  - replication
  - cross-cutting
aliases:
  - UE 网络同步全链路
  - UE Networking Pipeline
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-专题：网络同步全链路

## Why：为什么要梳理 UE 的网络同步全链路？

网络同步是多人游戏的根问题。单机引擎只需关心一帧内的 Tick 顺序，而网络引擎必须在**带宽、延迟、丢包**三重约束下，让多个远端终端对世界状态达成可接受的共识。UE 的网络同步功能横跨 **Sockets**（OS 层 Socket 抽象）、**PacketHandlers**（包处理链）、**Net/Core**（底层序列化与网络基础设施）、**Engine/Net**（Replication 核心）、**GameFramework**（Gameplay 层网络规则）、**NetworkReplayStreaming**（回放系统）六个层级。理解它们如何串联，才能回答：

- 一个 `ServerMove` RPC 从客户端按键到服务器响应，经过了哪些模块？
- NetDriver 如何在 Game Thread 上同时处理收包、发包、复制、RPC？
- 网络回放（Replay）如何录制和重放整个网络流量？
- 为什么某些属性不同步？PushModel 和 NetConditionGroup 如何工作？

---

## What：UE 网络同步的六层架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  第 6 层：回放与录像（NetworkReplayStreaming）                               │
│  ├─ INetworkReplayStreamer          → 录制/回放抽象接口                        │
│  ├─ FHttpNetworkReplayStreamer      → HTTP 远程回放                           │
│  ├─ FLocalFileNetworkReplayStreamer → 本地文件回放                            │
│  └─ UDemoNetDriver                  → 回放专用 NetDriver                       │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 5 层：Gameplay 网络规则（Engine / GameFramework）                         │
│  ├─ AGameModeBase                   → 连接校验、PlayerController 分配          │
│  ├─ APlayerController               → 客户端视角、Camera、RPC 中转             │
│  ├─ AGameStateBase                  → 全局游戏状态同步                         │
│  ├─ UCharacterMovementComponent     → 客户端预测 + 服务器权威 + 回滚修正        │
│  └─ UGameplayAbility / UGameplayTask → GAS 预测键（PredictionKey）            │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 4 层：Replication 核心（Engine / Net）                                    │
│  ├─ UNetDriver                      → 网络总控、连接管理、复制调度             │
│  ├─ UReplicationDriver              → 复制策略抽象（Legacy / Iris）            │
│  ├─ UNetConnection                  → 单条连接、可靠缓冲区、带宽限制           │
│  ├─ UActorChannel                   → Actor 专属通道、属性同步、RPC            │
│  ├─ FObjectReplicator               → 单对象复制执行器（Diff + 序列化）        │
│  ├─ FRepLayout / FRepState          → 属性复制布局与每连接状态                │
│  └─ UPackageMapClient               → NetGUID 分配与对象引用映射               │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 3 层：底层网络基础设施（Net/Core）                                        │
│  ├─ FNetBitWriter / FNetBitReader   → 位级序列化                             │
│  ├─ PushModel（MarkPropertyDirty）  → 显式脏标记，跳过逐属性比对              │
│  ├─ FNetToken / FNetTokenStore      → 轻量级对象引用令牌（20bit Index）        │
│  ├─ FNetConditionGroupManager       → 条件组管理（COND_NetGroup）             │
│  ├─ FNetHandle                      → 复制对象唯一标识（Id + Epoch）           │
│  ├─ FastArraySerializer             → 大数组增量同步                           │
│  └─ FDDoSDetection                  → DDoS 检测与限流                         │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 2 层：包处理链（PacketHandlers）                                          │
│  ├─ PacketHandler                   → 处理链管理器                            │
│  ├─ HandlerComponent[]              → 入站/出站流水线组件                      │
│  ├─ FEncryptionComponent            → 加密/解密                                │
│  ├─ FReliabilityHandlerComponent    → 可靠性（ACK + 超时重传，UE5.3 已废弃）   │
│  └─ 自定义 Handler（压缩、校验）      → 项目可扩展                             │
├─────────────────────────────────────────────────────────────────────────────┤
│  第 1 层：Socket 子系统（Sockets）                                            │
│  ├─ ISocketSubsystem                → 平台 Socket 抽象工厂                     │
│  ├─ FSocket                         → 跨平台 Socket（SendTo / RecvFrom）       │
│  ├─ FInternetAddr                   → 网络地址抽象                            │
│  └─ 平台实现（Windows/BSD/Unix/iOS） → 系统调用封装                            │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 第 1 层：接口层（What）—— 各层接口边界

### 1.1 Sockets 层：ISocketSubsystem 与 FSocket

> 文件：`Engine/Source/Runtime/Sockets/Public/SocketSubsystem.h`，第 80~150 行

```cpp
class ISocketSubsystem
{
public:
    virtual FSocket* CreateSocket(const FName& SocketType, ...);
    virtual void DestroySocket(FSocket* Socket);
    virtual FResolveInfo* GetHostByName(const TCHAR* HostName);
    virtual TSharedPtr<FInternetAddr> CreateInternetAddr(uint32 Address=0, uint32 Port=0);
};
```

`ISocketSubsystem` 是平台 Socket 的抽象工厂。Windows 实现用 `Winsock2`，BSD/Unix 实现用 Berkeley Socket。`UNetDriver` 在初始化时通过 `ISocketSubsystem::Get()` 获取默认子系统，创建 `FSocket`（通常为 UDP）。

> 文件：`Engine/Source/Runtime/Sockets/Public/Sockets.h`，第 120~200 行

```cpp
class FSocket
{
public:
    virtual bool Bind(const FInternetAddr& Addr);
    virtual bool SendTo(const uint8* Data, int32 Count, int32& BytesSent, const FInternetAddr& Destination);
    virtual bool RecvFrom(uint8* Data, int32 BufferSize, int32& BytesRead, FInternetAddr& Source);
    virtual bool SetNonBlocking(bool bIsNonBlocking = true);
};
```

### 1.2 PacketHandlers 层：处理链管理器

> 文件：`Engine/Source/Runtime/PacketHandler/Public/PacketHandler.h`，第 100~180 行

```cpp
class PacketHandler
{
public:
    TArray<TSharedPtr<HandlerComponent>> HandlerComponents;
    
    void Incoming(FBitReader& Packet, FIncomingPacketRef PacketRef);
    void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits);
    void IncomingConnectionless(const TSharedPtr<FInternetAddr>& Address, FBitReader& Packet);
    void OutgoingConnectionless(const TSharedPtr<FInternetAddr>& Address, FBitWriter& Packet);
};
```

所有收发包先经过 `PacketHandler` 链。`UNetDriver` 持有 `PacketHandler` 实例，在 `TickDispatch`（收包）和 `TickFlush`（发包）时调用。DDoS 检测（来自 Net/Core）在 `Incoming` 前执行。

### 1.3 Net/Core 层：位级序列化与网络基础设施

> 文件：`Engine/Source/Runtime/Net/Core/Public/Net/Core/PushModel/PushModel.h`，第 1~80 行

```cpp
namespace UEPushModelPrivate
{
    void MarkPropertyDirty(UObject* Object, int32 RepIndex, int32 RepOffset);
}
```

PushModel 允许开发者在修改属性值时显式标记脏状态，引擎不再每帧逐属性比对，而是直接发送被标记的属性。

> 文件：`Engine/Source/Runtime/Net/Core/Public/Net/Core/NetToken/NetToken.h`，第 40~100 行

```cpp
namespace UE::Net
{
    struct FNetToken
    {
        uint32 Index : 20;      // 对象索引
        uint32 TypeId : 3;      // 类型标识
        uint32 bIsAuthority : 1; // 权威标记
    };
}
```

`FNetToken` 是 UE5 引入的轻量级对象引用，替代传统的 `NetGUID` 在部分场景下的使用，更紧凑、更高效。

### 1.4 Engine/Net 层：Replication 核心类

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/NetDriver.h`，第 31~75 行

```cpp
UCLASS(abstract, transient)
class ENGINE_API UNetDriver : public UObject
{
    GENERATED_BODY()
public:
    TArray<TObjectPtr<UNetConnection>> ClientConnections;
    virtual int32 ServerReplicateActors(float DeltaSeconds);
    virtual void TickDispatch(float DeltaTime);
    virtual void TickFlush(float DeltaTime);
};
```

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/ReplicationDriver.h`，第 46~111 行

```cpp
UCLASS(abstract, transient, config=Engine)
class UReplicationDriver : public UObject
{
    virtual int32 ServerReplicateActors(float DeltaSeconds) PURE_VIRTUAL(...);
    virtual void AddNetworkActor(AActor* Actor) PURE_VIRTUAL(...);
};
```

`UReplicationDriver` 是复制策略的抽象插槽。UE5 的 **Iris** 复制系统通过派生自 `UReplicationDriver` 逐步替代 Legacy 流程。

> 文件：`Engine/Source/Runtime/Engine/Public/Net/DataReplication.h`，第 55~170 行

```cpp
class FObjectReplicator
{
    FRepLayout* RepLayout;
    FSendingRepState* SendingRepState;
    FReceivingRepState* ReceivingRepState;
public:
    bool ReplicateProperties(FOutBunch& Bunch, FReplicationFlags RepFlags);
    bool ReceivedBunch(FNetBitReader& Bunch, const FReplicationFlags& RepFlags, ...);
};
```

### 1.5 GameFramework 层：Gameplay 网络规则

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`，第 400~500 行

```cpp
UFUNCTION(Reliable, Server, WithValidation)
void ServerUpdateCamera(FVector_NetQuantize CamLoc, int32 CamPitchAndYaw);

UFUNCTION(Reliable, Client)
void ClientUpdateLevelStreamingStatus(FName PackageName, bool bNewShouldBeLoaded, ...);
```

`APlayerController` 是客户端与服务器之间的主要 RPC 通道，承担视角同步、关卡流送、状态切换等职责。

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h`，第 800~950 行

```cpp
class FSavedMove_Character : public FSavedMove
{
    float Timestamp;
    FVector_NetQuantize Acceleration;
    FVector_NetQuantize Location;
    FRotator ControlRotation;
    TEnumAsByte<EMovementMode::Type> MovementMode;
    bool bPressedJump;
};
```

### 1.6 NetworkReplayStreaming 层：回放接口

> 文件：`Engine/Source/Runtime/NetworkReplayStreaming/Public/NetworkReplayStreaming.h`，第 100~200 行

```cpp
class INetworkReplayStreamer
{
public:
    virtual void StartStreaming(const FStartStreamingParameters& Params, FStartStreamingResult& OutResult) = 0;
    virtual void StopStreaming() = 0;
    virtual FArchive* GetStreamingArchive() = 0;
    virtual void FlushCheckpoint(const FFlushCheckpointParameters& Params) = 0;
    virtual void GotoTimeInMS(const uint32 TimeInMS, FGotoTimeInMSResult& Result) = 0;
};
```

`UDemoNetDriver` 在回放/录制时替换标准 `UNetDriver`，通过 `INetworkReplayStreamer` 读写网络流。

---

## 第 2 层：数据层（How - Structure）—— 核心数据结构

### 2.1 Packet 与 Bunch：网络数据的原子单元

> 文件：`Engine/Source/Runtime/Engine/Public/Net/DataBunch.h`，第 20~121 行

```cpp
class FOutBunch : public FNetBitWriter
{
    FOutBunch *Next;
    UChannel *Channel;
    int32 ChIndex, ChSequence, PacketId;
    uint8 bOpen:1, bClose:1, bReliable:1, bPartial:1;
    TArray<FNetworkGUID> ExportNetGUIDs;
    TArray<UE::Net::FNetToken> NetTokensPendingExport;
};
```

- **Packet**：UDP/TCP 一次发送的物理数据单元。
- **Bunch**：逻辑数据单元，隶属于某个 Channel。一个 Packet 可包含多个 Bunch，一个大的 Bunch 也可拆成多个 Partial Bunch 跨 Packet 发送。

### 2.2 FRepLayout / FRepState：属性复制的元数据与状态

`FRepLayout` 基于 `UClass`/`UStruct` 的反射元数据，在运行时构建出"扁平化"的复制命令列表（`FRepLayoutCmd`）。

| 内存视图 | 说明 |
|---------|------|
| `FRepObjectDataBuffer` | 对象在 UObject 内存中的实际地址 |
| `FRepShadowDataBuffer` | 按 `FRepLayoutCmd` 紧凑排列的"上次已发送状态"副本，用于 Diff |

> 文件：`Engine/Source/Runtime/Engine/Private/RepLayout.cpp`，第 5142~5160 行（DiffProperties）

### 2.3 FNetGUIDCache：对象到 GUID 的全局映射

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/PackageMapClient.h`，第 119~141 行

```cpp
class FNetGuidCacheObject
{
    TWeakObjectPtr<UObject> Object;
    FNetworkGUID OuterGUID;
    FName PathName;
    uint32 NetworkChecksum;
    uint8 bNoLoad:1, bIgnoreWhenMissing:1, bIsPending:1;
};
```

Server 首次向 Client 发送对象引用时写出 `NetGUID + PathName`；Client 异步加载后，后续只写 `NetGUID`。

### 2.4 UNetConnection 的可靠性缓冲区

每个 `UNetConnection` 维护：
- `OutAckSeq`：最后确认的可靠序列号。
- `OutReliable[ChIndex]`：每个 Channel 的可靠发送序列号。
- `InReliable[ChIndex]`：每个 Channel 的可靠接收序列号。
- 可靠 Bunch 在收到 ACK 前保留在重传缓冲区，超时时重发。

### 2.5 NetworkReplayStreaming 的数据流

| 组件 | 数据结构 | 说明 |
|------|---------|------|
| `FNetworkReplayVersion` | `AppString, NetworkVersion, Changelist` | 回放版本兼容性校验 |
| `FReplayEventList` | `TArray<FReplayEventListItem>` | 可 JSON 序列化的事件列表 |
| `FInMemoryNetworkReplayStreamer` | `TArray<uint8> StreamData` | 内存回放缓冲区 |
| `FHttpNetworkReplayStreamer` | `FArchive* HttpStream` | HTTP 流式上传/下载 |

---

## 第 3 层：逻辑层（How - Behavior）—— 关键调用链

### 调用链 1：收包全链路（Socket → PacketHandler → NetConnection → Channel）

```
OS 内核：RecvFrom UDP Packet
  └─> Socket Thread（部分平台）
        └─> TQueue / FCriticalSection 将原始 Packet 交给 Game Thread

Game Thread：UNetDriver::TickDispatch(float DeltaTime)
  └─> 遍历所有 ClientConnections + ServerConnection
        └─> UNetConnection::ReceivedRawPacket(Data, Count)
              └─> PacketHandler::Incoming(PacketReader, PacketRef)
                    ├─> FDDoSDetection 阈值检测（来自 Net/Core）
                    ├─> HandlerComponent[0]::Incoming()  // 如解密
                    ├─> HandlerComponent[1]::Incoming()  // 如解压
                    └─> ...
              └─> UNetConnection::ReceivedPacket(Traits)
                    ├─> 解析 Packet Header（PacketId、Bunch 数量）
                    ├─> 处理 ACK：释放已确认的可靠 Bunch
                    ├─> 遍历 Packet 中的所有 Bunch
                    │     └─> UChannel::ReceivedRawBunch(Bunch)
                    │           ├─> UControlChannel::ProcessBunch  → 处理连接控制消息（NMT_Hello / NMT_Login）
                    │           └─> UActorChannel::ProcessBunch   → Actor 属性同步 / RPC
                    │                 ├─> FObjectReplicator::ReceivedBunch()
                    │                 │     ├─> 反序列化属性 → 写入 UObject 内存
                    │                 │     └─> 处理未解析 GUID → 加入 QueuedBunches
                    │                 └─> FObjectReplicator::ReceivedRPC()
                    │                       └─> UFunction::Invoke() → 执行本地函数
                    └─> 更新连接状态、带宽统计
```

> 文件：`Engine/Source/Runtime/Engine/Private/NetDriver.cpp`，第 2500~2700 行（TickDispatch）
> 文件：`Engine/Source/Runtime/Engine/Private/NetConnection.cpp`，第 1500~1800 行（ReceivedPacket）

### 调用链 2：发包全链路（Replication → Bunch → Packet → Socket）

```
UWorld::Tick() → UNetDriver::TickFlush(DeltaSeconds)
  └─> UNetDriver::ServerReplicateActors(DeltaSeconds)
        ├─> ServerReplicateActors_PrepConnections()
        ├─> ServerReplicateActors_BuildConsiderList()
        │     └─> 遍历所有 NetDormant 非休眠 Actor
        │           └─> AActor::IsNetRelevantFor(Connection)  → 相关性裁剪
        └─> 对每个 Connection：ServerReplicateActors_ForConnection(Connection, ConsiderList)
              ├─> 计算 Priority、Relevancy
              ├─> 打开/获取 UActorChannel
              └─> UActorChannel::ReplicateActor()
                    ├─> FOutBunch Bunch(this, 0)
                    ├─> if (RepFlags.bNetInitial) PackageMap->SerializeNewActor(Bunch, this, Actor)
                    ├─> ActorReplicator->ReplicateProperties(Bunch, RepFlags)
                    │     ├─> DiffProperties(ObjectBuffer vs ShadowBuffer)
                    │     ├─> PushModel：直接读取脏标记，跳过 Diff
                    │     └─> RepLayout->SendProperties_r(Writer, ...)
                    │           └─> 递归遍历属性树 → 位级写入 FNetBitWriter
                    ├─> DoSubObjectReplication(Bunch, RepFlags)  // 组件/子对象
                    └─> SendBunch(&Bunch, 1)
                          └─> UChannel::SendBunch()
                                └─> 合并到 Packet → PacketHandler::Outgoing()
                                      └─> HandlerComponent[]::Outgoing()  // 如加密、压缩
                                      └─> FSocket::SendTo() → OS 内核
```

> 文件：`Engine/Source/Runtime/Engine/Private/NetDriver.cpp`，第 6085~6150 行（ServerReplicateActors）
> 文件：`Engine/Source/Runtime/Engine/Private/DataChannel.cpp`，第 2500~2700 行（ReplicateActor）
> 文件：`Engine/Source/Runtime/Engine/Private/RepLayout.cpp`，第 2767~2830 行（SendProperties_r）

### 调用链 3：RPC 全链路（Client → Server → Client）

以 **Server RPC**（Client → Server）为例：

```
Client Game Thread：AActor::CallRemoteFunction(UFunction* Function, void* Parameters)
  └─> 遍历 ActiveNetDrivers
        └─> UNetDriver::ProcessRemoteFunction(Actor, Function, Parameters, ...)
              └─> 找到目标 Connection
                    └─> ProcessRemoteFunctionForChannelPrivate()
                          └─> UActorChannel::SendServerExecuteRPC()
                                ├─> 序列化 RPC 参数到 FNetBitWriter
                                ├─> 写入 Function NetGUID / Name
                                └─> SendBunch() → Packet → Socket::SendTo()

Server OS 内核：RecvFrom
  └─> UNetDriver::TickDispatch → PacketHandler::Incoming → UNetConnection::ReceivedPacket
        └─> UActorChannel::ProcessBunch → FObjectReplicator::ReceivedRPC()
              ├─> 反序列化参数
              ├─> 找到 UFunction → UFunction::Invoke()
              └─> 执行 Server RPC Implementation

Server 响应（如需要）：Client RPC
  └─> 走相同的 Replication 路径返回
```

> 文件：`Engine/Source/Runtime/Engine/Private/Actor.cpp`，第 4000~4100 行（CallRemoteFunction）
> 文件：`Engine/Source/Runtime/Engine/Private/DataReplication.cpp`，第 2500~2700 行（ReceivedRPC）

### 调用链 4：NetworkPrediction 客户端预测 + 服务器回滚

```
Client Tick：
  UCharacterMovementComponent::TickComponent()
    └─> 生成 FSavedMove_Character（输入快照）
    └─> FSavedMove_Character::PrepMove(Character)  // 本地预测执行
    └─> SavedMoves.Add(NewMove)
    └─> CallServerMovePacked(MoveDataContainer)    // RPC 发送

Server 接收：
  UCharacterMovementComponent::ServerMovePacked_Implementation(MoveDataContainer)
    └─> 解压移动数据
    └─> 按 Timestamp 执行移动
    └─> 对比结果位置
          ├─> 一致 → ClientAckGoodMove() → Client 移除已确认移动
          └─> 不一致 → 构建 FClientAdjustment
                └─> ClientAdjustPosition RPC → Client

Client 修正：
  UCharacterMovementComponent::ClientAdjustPosition_Implementation(...)
    └─> bUpdatePosition = true
    └─> ClientUpdatePositionAfterServerUpdate()
          ├─> 回滚到服务器给定状态
          ├─> 重放 SavedMoves 中该 Timestamp 之后的所有移动
          └─> SmoothCorrection()  // 视觉插值平滑
```

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h`，第 1200~1400 行（FSavedMove_Character）
> 文件：`Engine/Source/Runtime/Engine/Private/CharacterMovementComponent.cpp`，第 5500~5800 行（ServerMovePacked / ClientAdjustPosition）

### 调用链 5：网络回放录制与回放

```
录制模式（UDemoNetDriver 替换 UNetDriver）：
  UDemoNetDriver::TickFlush()
    └─> 正常执行 ServerReplicateActors（构建 ConsiderList、ReplicateActor）
    └─> 不是发送到 ClientConnections，而是写入 INetworkReplayStreamer::GetStreamingArchive()
          └─> FLocalFileNetworkReplayStreamer：写入本地 .replay 文件
          └─> FHttpNetworkReplayStreamer：HTTP 流式上传到回放服务器
    └─> 定期 FlushCheckpoint() → 保存当前世界状态的完整快照

回放模式：
  UDemoNetDriver::TickDispatch()
    └─> 从 INetworkReplayStreamer::GetStreamingArchive() 读取 Packet
    └─> 模拟正常收包流程：PacketHandler::Incoming → UNetConnection::ReceivedPacket
    └─> 客户端像正常游戏一样处理属性同步和 RPC
    
  跳转时间点：
    └─> GotoTimeInMS(TimeInMS)
          ├─> 找到最近的 Checkpoint
          ├─> 加载 Checkpoint 世界快照
          └─> 从 Checkpoint 时间点开始重放后续网络流
```

> 文件：`Engine/Source/Runtime/Engine/Private/DemoNetDriver.cpp`，第 1000~1500 行（TickFlush / TickDispatch）
> 文件：`Engine/Source/Runtime/NetworkReplayStreaming/Private/LocalFileNetworkReplayStreaming.cpp`，第 200~400 行（GetStreamingArchive / FlushCheckpoint）

---

## 多线程与同步

| 线程/上下文 | 网络阶段 | 同步方式 |
|-------------|---------|----------|
| **Game Thread** | TickDispatch、ServerReplicateActors、FlushNet、RPC | — |
| **Socket Thread**（部分平台） | RecvFrom / SendTo | TQueue / FCriticalSection 将原始 Packet 交给 Game Thread |
| **Async Loading Thread** | Client 收到 NetGUID 后异步加载资源 | `QueuedBunches` 机制：资源未加载完成前，Bunch 被暂存 |
| **Render Thread** | 网络同步与渲染无关，但 NetworkPrediction 的结果影响渲染 | Game Thread 状态驱动 |

**关键设计**：
- UE 的网络系统**主要运行在 Game Thread** 上。Socket 层收包后通过队列交给 Game Thread 处理，避免多线程解析网络协议的复杂性。
- `Async Loading Thread` 异步加载 NetGUID 引用的资源时，相关的 Bunch 被放入 `UActorChannel::QueuedBunches`，加载完成后 `ProcessQueuedBunchesInternal` 一次性处理。
- `FDDoSDetection`（Net/Core）在 `PacketHandler::Incoming` 前执行，监控包频率，超限时限流或断开连接。

---

## 上下层关系

### 上层调用者

| 上层 | 使用方式 |
|------|---------|
| `GameplayAbilities` | GAS 的 PredictionKey 机制建立在 NetDriver RPC 之上 |
| `OnlineSubsystem` | 在 NetDriver 之上提供会话、匹配、NAT 穿透 |
| `Engine Gameplay` | `APlayerController`、`ACharacter` 大量依赖网络同步 |
| `Replay System` | `UDemoNetDriver` 替换标准 NetDriver，录制/回放网络流 |

### 下层依赖

| 下层 | 作用 |
|------|------|
| `OS Socket API` | `recvfrom` / `sendto` / `WSASendTo` 等系统调用 |
| `D3D12/Vulkan/Metal` | 与网络无关，但 NetworkPrediction 的结果最终驱动渲染 |

---

## 设计亮点与可迁移经验

1. **驱动-连接-通道三级架构**
   `UNetDriver` 负责连接管理和复制调度，`UNetConnection` 负责可靠性和带宽控制，`UActorChannel` 负责单个 Actor 的语义。这种层级分离让网络层易于扩展和替换。

2. **NetGUID 解耦对象引用与内存地址**
   首次发送写 `GUID + PathName`，后续只写 `GUID`。这种"握手后短引用"模式显著降低带宽，且避免了直接序列化指针的版本耦合。

3. **ShadowBuffer + Diff 是通用属性同步方案**
   为每个网络对象维护一份 `LastSentState` 快照，逐属性 Diff 只发送变更。自研引擎可直接借鉴此模式。

4. **PushModel 与轮询 Diff 的性能权衡**
   轮询 Diff 适合小规模对象和快速原型；PushModel 适合已知修改点的项目代码；FastArraySerializer 适合大集合的增量同步。三者可以共存，按场景选择。

5. **NetworkPrediction 的标准模型**
   客户端预测 + 服务器权威 + 回滚修正（Prediction/Replication/Correction）是现代竞技游戏移动同步的工业标准。核心要素：输入快照（SavedMove）、时间戳对齐、服务器修正帧、客户端回滚重放、平滑插值。

6. **Replay 系统的录制/回放解耦**
   通过 `INetworkReplayStreamer` 抽象和 `UDemoNetDriver` 替换标准 NetDriver，UE 实现了"同一套 Replication 代码，既可在线同步，也可离线回放"。Checkpoint + 增量流的组合让大时长回放也能快速跳转。

7. **ReplicationDriver 提供策略插槽**
   把"哪些 Actor 复制给谁"这一策略从"如何序列化"中抽离出来。大型项目可接入 Replication Graph 做分帧、分区域、分优先级的批量复制。

---

## 关键源码片段

### UNetDriver::ServerReplicateActors

> 文件：`Engine/Source/Runtime/Engine/Private/NetDriver.cpp`，第 6085~6150 行

```cpp
int32 UNetDriver::ServerReplicateActors(float DeltaSeconds)
{
    ServerReplicateActors_PrepConnections();
    ServerReplicateActors_BuildConsiderList();
    for (UNetConnection* Connection : ClientConnections)
    {
        ServerReplicateActors_ForConnection(Connection, ConsiderList);
    }
    for (UNetConnection* Connection : ClientConnections)
    {
        Connection->FlushNet();
    }
}
```

### UActorChannel::ReplicateActor

> 文件：`Engine/Source/Runtime/Engine/Private/DataChannel.cpp`，第 2500~2700 行

```cpp
int32 UActorChannel::ReplicateActor()
{
    FOutBunch Bunch(this, 0);
    if (RepFlags.bNetInitial)
    {
        PackageMap->SerializeNewActor(Bunch, this, Actor);
    }
    ActorReplicator->ReplicateProperties(Bunch, RepFlags);
    DoSubObjectReplication(Bunch, RepFlags);
    SendBunch(&Bunch, 1);
    return Bunch.GetNumBits();
}
```

### PushModel 脏标记

> 文件：`Engine/Source/Runtime/Net/Core/Public/Net/Core/PushModel/PushModel.h`，第 60~80 行

```cpp
void SetMyReplicatedBool(const bool bNewValue)
{
    bMyReplicatedBool = bNewValue;
    MARK_PROPERTY_DIRTY_FROM_NAME(AMyAwesomeActor, bMyReplicatedBool, this);
}
```

---

## 关联阅读

- [[UE-Net-源码解析：网络同步与 Replication]] — NetDriver/Connection/Channel 的详细实现
- [[UE-Engine-源码解析：网络同步与预测]] — NetworkPrediction、SavedMove、客户端预测回滚
- [[UE-Engine-源码解析：GameFramework 与规则体系]] — PreLogin、PlayerController 生成逻辑
- [[UE-Engine-源码解析：Actor 与 Component 模型]] — ActorChannel 中 Actor/Component 的层级
- [[UE-Engine-源码解析：World 与 Level 架构]] — UWorld::Listen、UPendingNetGame 初始化
- [[UE-CoreUObject-源码解析：反射系统与 UHT]] — FRepLayout 依赖的 UProperty / UClass 元数据
- [[UE-Serialization-源码解析：Archive 序列化体系]] — FNetBitWriter / FNetBitReader 与 FArchive
- [[UE-Sockets-源码解析：Socket 子系统]] — NetDriver 的底层 Socket 支撑
- [[UE-专题：UObject 生命周期全链路]] — 网络同步中 Actor 的 Spawn/Destroy 生命周期
- [[UE-专题：内存管理全链路]] — 网络层内存分配（Bunch、Packet、RepState）

---

## 索引状态

- **所属 UE 阶段**：第八阶段 — 跨领域专题深度解析
- **对应 UE 笔记**：UE-专题：网络同步全链路
- **本轮分析完成度**：✅ 已完成三层分析（接口层 + 数据层 + 逻辑层 + 多线程交互 + 关联辐射）
- **更新日期**：2026-04-18
