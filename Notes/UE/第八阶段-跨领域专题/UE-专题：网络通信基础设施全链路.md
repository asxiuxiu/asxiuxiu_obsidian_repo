---
title: UE-专题：网络通信基础设施全链路
date: 2026-04-18
tags:
  - ue-source
  - engine-architecture
  - network
  - sockets
aliases:
  - UE 网络通信基础设施全链路
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引\|UE全解析主索引]])

# UE-专题：网络通信基础设施全链路

## Why：为什么要理解网络通信基础设施全链路？

- **网络是游戏引擎的"第五大系统"**。渲染、物理、动画、音频解决的是"本地世界如何呈现"，而网络解决的是"多个终端如何共享同一个世界"。不理解从 Socket 到 OnlineSubsystem 的完整链路，就无法定位联机卡顿、连接失败、后端对接等问题的根因。
- **UE 的网络栈是分层解耦的典范**。从 BSD Socket 的跨平台抽象，到 PacketHandler 的管道化处理，再到 NetDriver 的连接管理，最后到 OnlineSubsystem 的平台服务聚合，每一层都有清晰的接口边界。这种分层设计是自研网络中间件的重要参照。
- **WebSockets 与 HTTP 是新时代游戏的必需品**。实时推送、聊天系统、Web 后端对接、云服务通信，都离不开 TCP/HTTP 之上更高层的协议栈。

---

## What：网络通信基础设施是什么？

UE 的网络通信基础设施是一个**自下而上的分层协议栈**，涵盖以下五个核心模块：

| 层级 | 模块 | 核心职责 |
|------|------|---------|
| L1 传输层 | `Sockets` | 跨平台 BSD Socket 抽象，端到端的字节流传输 |
| L2 处理层 | `PacketHandlers` + `NetCore` | 数据包管道处理（加密、压缩、可靠性）、连接句柄、PushModel |
| L3 连接层 | `Engine/Net` | `UNetDriver` / `UNetConnection` / `UChannel` 管理，Replication 的载体 |
| L4 服务层 | `OnlineSubsystem` | 平台无关的在线服务聚合：Session、Identity、Voice、Friends、Leaderboards |
| L5 应用层 | `WebSockets` + `HTTP` | WebSocket 长连接、HTTP REST 通信、后端对接 |

> 文件：`Engine/Source/Runtime/Sockets/Sockets.Build.cs`
> 文件：`Engine/Source/Runtime/PacketHandlers/PacketHandler/PacketHandler.Build.cs`
> 文件：`Engine/Plugins/Online/OnlineSubsystem/OnlineSubsystem.Build.cs`

---

## 接口梳理（第 1 层）

### Sockets：跨平台传输抽象

`Sockets` 模块是 UE 网络栈的根基，通过**抽象基类 + 平台子类化**屏蔽操作系统差异。

> 文件：`Engine/Source/Runtime/Sockets/Public/Sockets.h`，第 62~79 行

```cpp
class FSocket : public TSharedFromThis<FSocket, ESPMode::ThreadSafe>
{
protected:
    const ESocketType SocketType;
    FString SocketDescription;
    FName SocketProtocol;
public:
    virtual bool Bind(const FInternetAddr& Addr) = 0;
    virtual bool Connect(const FInternetAddr& Addr) = 0;
    virtual bool Listen(int32 MaxBacklog) = 0;
    virtual bool Send(const uint8* Data, int32 Count, int32& BytesSent) = 0;
    virtual bool Recv(uint8* Data, int32 BufferSize, int32& BytesRead) = 0;
    virtual bool SetNonBlocking(bool bIsNonBlocking = true) = 0;
};
```

`ISocketSubsystem` 是全局工厂入口：

> 文件：`Engine/Source/Runtime/Sockets/Public/SocketSubsystem.h`，第 91~101 行

```cpp
class ISocketSubsystem
{
public:
    static SOCKETS_API ISocketSubsystem* Get(const FName& SubsystemName=NAME_None);
    virtual FSocket* CreateSocket(const FName& SocketType, const FString& SocketDescription, const FName& ProtocolName) = 0;
    virtual void DestroySocket(FSocket* Socket) = 0;
    virtual TSharedRef<FInternetAddr> CreateInternetAddr() = 0;
};
```

> 更详细的 Sockets 分析见：[[UE-Sockets-源码解析：Socket 子系统]]

### PacketHandlers：数据包管道处理器

`PacketHandler` 实现了**责任链模式**的数据包处理管道。每个连接拥有一条独立的 Handler 链，支持入站/出站双向处理。

> 文件：`Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h`，第 80~116 行

```cpp
namespace UE::Handler
{
    enum class State : uint8 { Uninitialized, InitializingComponents, Initialized };
    enum class Mode : uint8 { Client, Server };
    namespace Component
    {
        enum class State : uint8 { UnInitialized, InitializedOnLocal, InitializeOnRemote, Initialized };
    }
}
```

核心类：

| 类 | 职责 |
|---|---|
| `PacketHandler` | 管道管理器，维护 `HandlerComponent` 数组，调度 `Incoming`/`Outgoing` |
| `HandlerComponent` | 处理组件基类，子类实现加密、压缩、可靠性等逻辑 |
| `FEncryptionComponent` | 加密组件抽象接口 |
| `ReliabilityHandlerComponent` | 可靠性保障：ACK、重传、包序管理 |

> 文件：`Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h`，第 122~150 行

```cpp
struct ProcessedPacket
{
    uint8* Data;
    int32 CountBits;
    bool bError;
};

class PacketHandler
{
public:
    ProcessedPacket Incoming(uint8* Data, int32 CountBits);
    ProcessedPacket Outgoing(uint8* Data, int32 CountBits, FOutPacketTraits& Traits);
    void AddHandler(TSharedPtr<HandlerComponent> NewHandler);
};
```

`PacketHandler` 在连接握手阶段会缓存所有出站包，直到所有 `HandlerComponent` 完成双向初始化（`Initialized` 状态），然后一次性释放缓冲区。

### NetCore：网络核心原语

`NetCore` 提供 NetDriver 以下的通用网络基础设施，不依赖 Engine 模块。

> 文件：`Engine/Source/Runtime/Net/Core/Public/Net/Core/Connection/ConnectionHandle.h`

```cpp
struct FConnectionHandle
{
    uint32 ParentConnectionId;
    uint32 ConnectionId;
    // 唯一标识父子连接，用于分帧复制和 NetAnalytics
};
```

> 文件：`Engine/Source/Runtime/Net/Core/Public/Net/Core/Connection/NetResult.h`

```cpp
struct FNetResult
{
    // 网络操作结果链，支持错误码枚举反射和结果链拼接
    void AddChainResult(const FNetResult& Other);
};
```

### OnlineSubsystem：平台服务聚合层

`OnlineSubsystem` 位于 `Engine/Plugins/Online/`，是 UE 对接 Steam、Xbox Live、PSN、EOS 等平台的统一抽象。

> 文件：`Engine/Plugins/Online/OnlineSubsystem/Source/Public/OnlineSubsystem.h`

```cpp
class IOnlineSubsystem
{
public:
    static IOnlineSubsystem* Get(const FName& SubsystemName = NAME_None);
    virtual IOnlineSessionPtr GetSessionInterface() = 0;
    virtual IOnlineFriendsPtr GetFriendsInterface() = 0;
    virtual IOnlineIdentityPtr GetIdentityInterface() = 0;
    virtual IOnlineVoicePtr GetVoiceInterface() = 0;
};
```

> 更详细的 OnlineSubsystem 分析见：[[UE-Online-源码解析：OnlineSubsystem 与后端对接]]

### WebSockets：应用层长连接

`WebSockets` 模块封装了基于 HTTP Upgrade 的全双工长连接，提供跨平台的 `IWebSocket` 接口。

> 文件：`Engine/Source/Runtime/Online/WebSockets/Public/IWebSocket.h`

```cpp
class IWebSocket
{
public:
    virtual void Connect() = 0;
    virtual void Close() = 0;
    virtual void Send(const FString& Data) = 0;
    virtual void Send(const void* Data, SIZE_T Size, bool bIsBinary = false) = 0;
    FOnWebSocketConnected OnConnected;
    FOnWebSocketMessage OnMessage;
    FOnWebSocketRawMessage OnRawMessage;
};
```

平台实现：
- `FLwsWebSocketsManager`：基于 `libwebsockets` + OpenSSL
- `FWinHttpWebSocketsManager`：基于 Windows WinHttp API

---

## 数据结构（第 2 层）

### 全链路对象层级关系

```
IOnlineSubsystem (平台服务聚合)
  └── IOnlineSession (房间/匹配)
  └── IOnlineIdentity (登录/账号)
  └── IOnlineFriends (好友)
  └── IOnlineVoice (语音)

UNetDriver (Engine 模块)
  ├── UNetConnection[]
  │     ├── PacketHandler (管道)
  │     │     ├── HandlerComponent[] (加密/压缩/可靠性)
  │     ├── UChannel[]
  │     │     └── UActorChannel (Replication)
  │     └── FSocket (Sockets 模块)
  └── FConnectionHandle (NetCore)
```

### PacketHandler 的缓冲区管理

在初始化期间，`PacketHandler` 使用 `BufferedPacket` 结构缓存未发送的包：

> 文件：`Engine/Source/Runtime/PacketHandlers/PacketHandler/Public/PacketHandler.h`，第 155~200 行

```cpp
struct BufferedPacket
{
    uint8* Data;
    uint32 CountBits;
    FOutPacketTraits Traits;
    double ResendTime;          // 重传时间戳
    uint32 Id;                  // 包序号
    TSharedPtr<const FInternetAddr> Address;
    HandlerComponent* FromComponent;
};
```

握手完成后，所有缓冲包按序通过 `Outgoing` 管道发送。

### UNetConnection 的连接状态机

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/NetConnection.h`，第 88~94 行

```cpp
enum EConnectionState
{
    USOCK_Invalid   = 0,
    USOCK_Closed    = 1,
    USOCK_Pending   = 2,
    USOCK_Open      = 3,
    USOCK_Closing   = 4,
};
```

连接从 `Pending` 开始，完成 PacketHandler 握手后进入 `Open`，此时 Replication 正式开始。

---

## 行为分析（第 3 层）

### 数据包收发全链路

**出站流程**：

```
UActorChannel::SendBunch(FOutBunch* Bunch)
  └── UNetConnection::SendRawBunch(...)
        └── UNetConnection::WriteBitsToSendBuffer(...)
              └── PacketHandler::Outgoing(Buffer, CountBits, Traits)
                    ├── HandlerComponent[0]::Outgoing(...)  // 加密/压缩
                    ├── HandlerComponent[1]::Outgoing(...)  // 可靠性
                    └── LowLevelSend(Buffer, CountBits)
                          └── FSocket::Send(...)
```

**入站流程**：

```
FSocket::Recv(Buffer)
  └── UNetConnection::ReceivedRawPacket(Buffer)
        └── PacketHandler::Incoming(Buffer, CountBits)
              ├── HandlerComponent[1]::Incoming(...)  // 可靠性解包
              ├── HandlerComponent[0]::Incoming(...)  // 解密/解压
              └── UChannel::ReceivedNextBunch(...)
                    └── UActorChannel::ProcessBunch(...)
```

### 连接建立与握手流程

```
Client                                          Server
  │                                               │
  ├─────── 原始握手包 (无 Handler) ───────────────►│
  │                                               │
  │◄────── 响应 + Handler 组件列表 ────────────────┤
  │                                               │
  ├─────── 初始化本地 HandlerComponent[0] ───────►│
  │◄────── 初始化远程 HandlerComponent[0] ─────────┤
  │                                               │
  ├─────── 初始化本地 HandlerComponent[1] ───────►│
  │◄────── 初始化远程 HandlerComponent[1] ─────────┤
  │                                               │
  ├─────── 握手完成，发送缓冲包队列 ──────────────►│
  │                                               │
  │◄══════ 进入 USOCK_Open，开始 Replication ══════│
```

> 更详细的 Replication 分析见：[[UE-Net-源码解析：网络同步与 Replication]]

### WebSocket 与 UNetConnection 的差异

| 维度 | UNetConnection (UDP/TCP) | WebSocket |
|------|--------------------------|-----------|
| 底层协议 | UDP（游戏流量）/ TCP（控制流量） | TCP + HTTP Upgrade |
| 包模型 | 自定义 Bunch/Packet，支持分片 | 帧级文本/二进制消息 |
| 处理管道 | PacketHandler 责任链 | 无（直接由应用层消费） |
| 使用场景 | 实时游戏同步、RPC | Web 后端、聊天、推送、HTTP API |
| 平台服务 | 通过 OnlineSubsystem 匹配 | 通常直接连接后端服务器 |

---

## 与上下层的关系

### 上层调用者

| 上层模块 | 使用方式 |
|---------|---------|
| `Gameplay` | 通过 `AActor::ReplicateSubobjects`、RPC (`UFUNCTION(Server/Client/NetMulticast)`) 使用网络层 |
| `OnlineSubsystemUtils` | 封装 `CreateSession`、`FindSessions`、`JoinSession` 等高阶 API |
| `Voice` | 通过 `IOnlineVoice` 使用语音聊天，底层复用 `UNetConnection` 或独立 Socket |
| `WebBrowser` / `HTTP` | 使用 `WebSockets` 进行实时通信，与游戏网络并行存在 |

### 下层依赖

| 下层模块 | 依赖方式 |
|---------|---------|
| `Core` | 日志、内存、字符串、容器 |
| `NetCommon` | 包视图 `PacketView`、公共错误码、协议常量 |
| `SSL` | WebSockets 的 TLS 加密（可选） |
| `libWebSockets` / `WinHttp` | WebSockets 的第三方实现 |

---

## 设计亮点与可迁移经验

1. **分层解耦，接口先行**：Sockets → PacketHandlers → NetDriver → OnlineSubsystem，每一层只依赖下层接口，不穿透实现。这种设计允许替换任意一层（如用 QUIC 替换 UDP Socket、用自定义加密替换 PacketHandler）。

2. **责任链模式处理管道**：`PacketHandler` 的 `HandlerComponent` 数组是可插拔的，项目可以通过 `UHandlerComponentFactory` 注册自定义处理组件（如自定义协议头、流量整形）。

3. **连接状态与初始化分离**：`USOCK_Pending` 状态下 PacketHandler 完成握手，保证所有加密/可靠性机制就绪后才允许 Replication 流量通过，避免明文泄漏和包序错乱。

4. **平台抽象到接口级别**：`IOnlineSubsystem` 不是简单的函数转发，而是完整的平台服务抽象。同一套游戏代码可以在 Steam/Epic/主机平台间无缝切换。

5. **结果链替代 bool 返回值**：`FNetResult` / `TNetResult<T>` 的设计让网络错误可以在调用链中逐层传递和累积，避免丢失上下文。

---

## 关联阅读

- [[UE-Sockets-源码解析：Socket 子系统]] — Sockets 模块的详细三层分析
- [[UE-Net-源码解析：网络同步与 Replication]] — NetDriver/Connection/Channel 的详细分析
- [[UE-Online-源码解析：OnlineSubsystem 与后端对接]] — OnlineSubsystem 的架构解析
- [[UE-HTTP-源码解析：HTTP 与 JSON 后端通信]] — HTTP/REST 通信（计划中）

---

## 索引状态

- **所属阶段**：第八阶段-跨领域专题
- **对应笔记名称**：UE-专题：网络通信基础设施全链路
- **本轮完成度**：✅ 三层分析完成
- **更新日期**：2026-04-18
