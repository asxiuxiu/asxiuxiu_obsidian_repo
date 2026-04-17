---
title: UE-Engine-源码解析：GameFramework 与规则体系
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Engine GameFramework 规则体系
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Engine-源码解析：GameFramework 与规则体系

## Why：为什么要深入理解 GameFramework？

GameFramework 是 UE 网络游戏的"骨架"。从玩家登录、规则仲裁、Pawn 生成到状态同步，所有多人游戏的底层逻辑都围绕 `AGameModeBase`、`APlayerController`、`APawn` 等类展开。理解这套框架的生命周期、网络复制策略和权威关系，是开发任何联网游戏的基础。

## What：GameFramework 与规则体系是什么？

- **`AGameModeBase`**：游戏规则仲裁者，**仅存在于 Server**，不复制到任何客户端。
- **`AGameStateBase`**：全局游戏状态容器，**复制到所有客户端**。
- **`APlayerController`**：人类玩家的代理，**仅复制到 owning Client**。
- **`APlayerState`**：玩家网络数据容器（名称、分数、Ping），**复制到所有客户端**。
- **`APawn`** / **`ACharacter`**：玩家/生物在关卡中的物理表示，可被 Controller Possess。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Engine/`
- **Build.cs 文件**：`Engine.Build.cs`
- **关键目录**：
  - `Classes/GameFramework/GameModeBase.h`、`GameStateBase.h`、`PlayerController.h`、`PlayerState.h`、`Pawn.h`、`Character.h`
  - `Private/GameModeBase.cpp`、`GameStateBase.cpp`、`PlayerController.cpp`、`Controller.cpp`

---

## 接口梳理（第 1 层）

### 核心类职责

| 类 | 继承 | 存在位置 | 核心职责 |
|---|---|---|---|
| `AGameModeBase` | `AInfo` → `AActor` | 仅 Server | 游戏规则、玩家登录审批、Pawn 生成 |
| `AGameStateBase` | `AInfo` → `AActor` | Server + 所有 Client | 全局状态、PlayerArray、服务器时间 |
| `APlayerController` | `AController` → `AActor` | Server + owning Client | 相机、输入、HUD、关卡流送同步 |
| `APlayerState` | `AInfo` → `AActor` | Server + 所有 Client | 玩家身份数据（Name、Score、Ping） |
| `APawn` | `AActor` | Server + Client | 可被控制的物理表示 |
| `ACharacter` | `APawn` | Server + Client | 带骨架网格、胶囊碰撞、移动组件的 Pawn |

### GameFramework 类关系图

```
[Server Only]
AGameModeBase
    ├── AGameSession
    └── AGameStateBase ─────────┐
        └── PlayerArray[]       │
            └── APlayerState────┼──┐
                                │  │
    Spawn / Manage ─────────────┘  │
        APlayerController         │
              │                   │
              │ Possess           │
              ▼                   │
            APawn ────────────────┘
              │
        ACharacter
              │
    ├─ USkeletalMeshComponent (Mesh)
    ├─ UCapsuleComponent (Collision)
    └─ UCharacterMovementComponent
```

---

## 数据结构（第 2 层）

### AGameModeBase 核心字段

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h`

```cpp
class ENGINE_API AGameModeBase : public AInfo
{
    TSubclassOf<AGameSession> GameSessionClass;
    TSubclassOf<AGameStateBase> GameStateClass;
    TSubclassOf<APawn> DefaultPawnClass;
    TSubclassOf<APlayerController> PlayerControllerClass;
    TSubclassOf<APlayerState> PlayerStateClass;
    TSubclassOf<AHUD> HUDClass;
    int32 NumSpectators;
    int32 NumPlayers;
    int32 NumBots;
};
```

### APlayerController 核心字段

```cpp
class ENGINE_API APlayerController : public AController
{
    TObjectPtr<APlayerCameraManager> PlayerCameraManager;
    TObjectPtr<UPlayerInput> PlayerInput;
    TObjectPtr<AHUD> MyHUD;
    TObjectPtr<UCheatManager> CheatManager;
};
```

### 网络复制策略

| 对象 | 权威位置 | 复制策略 | 关键说明 |
|---|---|---|---|
| GameMode | 仅 Server | **不复制** | 绝对的规则仲裁者 |
| GameState | Server | 复制到所有 Client (`bAlwaysRelevant=true`) | 客户端通过 `OnRep` 同步游戏阶段 |
| PlayerState | Server | 复制到所有 Client (`bAlwaysRelevant=true`) | 跨机玩家信息的唯一来源 |
| PlayerController | Server + owning Client | 仅 owning Client | 远程客户端看不到其他玩家的 PC |
| Pawn | Server | 按相关性复制 | Controller 和 PlayerState 是 Replicated 属性 |

---

## 行为分析（第 3 层）

### 游戏框架生命周期

```
UGameEngine::LoadMap()
    └── 生成 AGameModeBase
        └── AGameModeBase::InitGame()
            └── 生成 AGameSession

AGameModeBase::PreInitializeComponents()
    └── 生成 AGameStateBase → World->SetGameState(GameState)
    └── InitGameState()

[客户端连接]
    └── PreLoginAsync / PreLogin → GameSession->ApproveLogin()
    └── Login → SpawnPlayerController() → InitNewPlayer()
    └── PostLogin → GenericPlayerInitialization() → HandleStartingNewPlayer()
        └── RestartPlayer() → SpawnDefaultPawnFor() → FinishRestartPlayer()
            └── Controller->Possess(Pawn)

[游戏开始]
    └── AGameModeBase::StartPlay()
        └── GameState->HandleBeginPlay()
            └── bReplicatedHasBegunPlay = true (复制到客户端)
```

### 关键函数 1：AGameModeBase::Login

> 文件：`Engine/Source/Runtime/Engine/Private/GameModeBase.cpp`

```cpp
APlayerController* AGameModeBase::Login(UPlayer* NewPlayer, const FString& Portal, ...)
{
    APlayerController* NewPlayerController = SpawnPlayerController(NewPlayer->GetNetConnection()->GetRemoteRole(), Options);
    InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    return NewPlayerController;
}
```

### 关键函数 2：AGameModeBase::RestartPlayer

> 文件：`Engine/Source/Runtime/Engine/Private/GameModeBase.cpp`

```cpp
void AGameModeBase::RestartPlayer(AController* NewPlayer)
{
    AActor* StartSpot = FindPlayerStart(NewPlayer);
    APawn* NewPawn = SpawnDefaultPawnFor(NewPlayer, StartSpot);
    NewPlayer->SetPawn(NewPawn);
    FinishRestartPlayer(NewPlayer, StartSpot->GetActorRotation());
}
```

### 关键函数 3：FinishRestartPlayer → Possess

> 文件：`Engine/Source/Runtime/Engine/Private/GameModeBase.cpp` / `Controller.cpp`

```cpp
void AGameModeBase::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
    NewPlayer->Possess(NewPlayer->GetPawn());
    NewPlayer->ClientSetRotation(StartRotation);
    SetPlayerDefaults(NewPlayer->GetPawn());
}

void AController::OnPossess(APawn* InPawn)
{
    InPawn->PossessedBy(this);   // Pawn 侧设置 Controller/PlayerState
    SetPawn(InPawn);
    Pawn->DispatchRestart();
}
```

### 关键同步机制

- `AController::OnRep_Pawn()` — 客户端收到 Pawn 引用后绑定。
- `APawn::OnRep_Controller()` / `APawn::OnRep_PlayerState()` — 远程 Pawn 建立与本地 Controller/PlayerState 关联。
- `AGameStateBase::OnRep_ReplicatedHasBegunPlay()` — 保证全图统一开始游戏。

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `Gameplay` | 子类化 `AGameModeBase`、`APlayerController` 实现具体游戏规则 |
| `OnlineSubsystem` | 处理登录验证、会话匹配、玩家 ID |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `CoreUObject` | UObject 生命周期、网络复制属性系统 |
| `Net` / `NetCore` | `Replicated` 属性的序列化与同步 |

---

## 设计亮点与可迁移经验

1. **GameMode 的 Server-Only 设计**：将规则仲裁者完全隔离在服务器，从根本上防止了客户端作弊修改游戏规则。
2. **PlayerState 的全局可见性**：因为 `Controller` 不复制到远程客户端，`PlayerState` 成为跨机玩家信息的唯一来源。这是网络游戏中"身份信息与控制器分离"的经典模式。
3. **Possess 作为权威切换点**：`Controller::OnPossess` 是服务器声明"谁控制哪个 Pawn"的唯一权威入口，所有客户端通过 `OnRep_Pawn` 被动同步。
4. **通过 GameState 统一阶段同步**：`bReplicatedHasBegunPlay` 保证了所有客户端在收到通知后才统一进入 BeginPlay，避免了时序不一致。

---

## 关键源码片段

### GameStateBase 的复制设置

> 文件：`Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h`

```cpp
UCLASS(config=Game, BlueprintType, MinimalAPI)
class AGameStateBase : public AInfo
{
    UPROPERTY(Replicated, Transient, BlueprintReadOnly, Category="GameState")
    double ReplicatedWorldTimeSecondsDouble;

    UPROPERTY(ReplicatedUsing=OnRep_ReplicatedHasBegunPlay, BlueprintReadOnly, Category="GameState")
    uint8 bReplicatedHasBegunPlay:1;

    UPROPERTY(Replicated, BlueprintReadOnly, Category="GameState")
    TArray<TObjectPtr<APlayerState>> PlayerArray;
};
```

### AController::OnRep_Pawn

> 文件：`Engine/Source/Runtime/Engine/Private/Controller.cpp`

```cpp
void AController::OnRep_Pawn()
{
    if (Pawn)
    {
        Pawn->RecalculateBaseEyeHeight();
        SetControlRotation(Pawn->GetActorRotation());
        AttachToPawn(Pawn);
    }
}
```

---

## 关联阅读

- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-Engine-源码解析：Actor 与 Component 模型]]
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]]
- [[UE-Net-源码解析：网络同步与 Replication]]
- [[UE-Online-源码解析：OnlineSubsystem 与后端对接]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-Engine-源码解析：GameFramework 与规则体系
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17
