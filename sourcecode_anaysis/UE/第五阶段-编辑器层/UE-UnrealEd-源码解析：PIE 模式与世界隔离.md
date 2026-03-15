---
title: UE-UnrealEd-源码解析：PIE 模式与世界隔离
date: 2026-04-18
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE UnrealEd PIE 模式与世界隔离
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-UnrealEd-源码解析：PIE 模式与世界隔离

## 模块定位

- **UE 模块路径**：`Engine/Source/Editor/UnrealEd/`
- **Build.cs 文件**：`UnrealEd.Build.cs`
- **核心类文件**：`Classes/Editor/EditorEngine.h`、`Classes/Editor/UnrealEdEngine.h`
- **核心依赖**：`Core`、`CoreUObject`、`Engine`、`Slate`、`SlateCore`、`EditorFramework`
- **被依赖方**：`LevelEditor`、`BlueprintGraph`、`Kismet` 等

PIE（Play In Editor）是 UE 编辑器最核心的功能之一：在编辑器内部启动一个隔离的游戏世界实例，同时保持编辑器世界不受影响。其底层支撑是 `UEditorEngine` 对多 `FWorldContext` 的管理。

---

## 接口梳理（第 1 层）

### 核心头文件

| 头文件 | 核心类/结构 | 职责 |
|--------|------------|------|
| `Classes/Editor/EditorEngine.h` | `UEditorEngine` | 编辑器引擎基类，管理 PIE、WorldList、视口 |
| `Classes/Editor/UnrealEdEngine.h` | `UUnrealEdEngine` | 具体编辑器引擎实现，绑定 PIE 相关命令 |
| `Classes/Editor/PlayWorld.h` | `FWorldContext` | 世界上下文，区分 EditorWorld 和 PIEWorld |

### 关键 UCLASS/USTRUCT

```cpp
// 文件：Engine/Source/Editor/UnrealEd/Classes/Editor/EditorEngine.h
UCLASS()
class UNREALED_API UEditorEngine : public UEngine
{
    GENERATED_BODY()
public:
    // 当前活动的所有世界上下文（Editor + PIE + SIE 等）
    UPROPERTY()
    TArray<FWorldContext> WorldList;

    // 编辑器世界（非 PIE 的原始世界）
    UPROPERTY()
    TObjectPtr<UWorld> EditorWorld;

    // PIE 世界的上下文索引
    int32 PlayWorldIndex;

    // PIE 的 GameInstance
    UPROPERTY()
    TObjectPtr<UGameInstance> PlayGameInstance;
};
```

```cpp
// 文件：Engine/Source/Runtime/Engine/Classes/Engine/Engine.h（FWorldContext 定义）
USTRUCT()
struct FWorldContext
{
    GENERATED_BODY()
    
    UPROPERTY()
    TEnumAsByte<EWorldType::Type> WorldType;
    
    UPROPERTY()
    TObjectPtr<UWorld> ThisCurrentWorld;
    
    UPROPERTY()
    TObjectPtr<UGameInstance> OwningGameInstance;
    
    UPROPERTY()
    FName ContextHandle;
};
```

---

## 数据结构（第 2 层）

### 核心类内存布局与状态流转

#### FWorldContext
- **类型**：`USTRUCT()`，非 UObject，值类型存储在 `TArray<FWorldContext>` 中
- **关键字段**：
  - `WorldType`：`Editor`、`PIE`、`Game`、`Preview`、`Inactive` 等
  - `ThisCurrentWorld`：当前关联的 `UWorld` 指针
  - `OwningGameInstance`：该世界所属的 `UGameInstance`
  - `ContextHandle`：唯一标识符，如 "Game_0"、"Editor"
- **内存来源**：`TArray` 内联存储，随 `UEditorEngine` 的 `WorldList` 分配在 UObject GC Heap 上

#### UEditorEngine 的多世界管理
- **EditorWorld**：编辑器打开关卡时的原始世界，`WorldType == Editor`
- **PIE World**：点击 Play 后从 `EditorWorld` **复制**得到的新世界，`WorldType == PIE`
- **SIE World**：Simulate In Editor 模式下的世界，与 PIE 类似但无玩家控制器
- **WorldList**：维护所有活跃世界的上下文，支持同时运行多个 PIE 实例（Multi-PIE）

### UObject 生命周期：PIE 世界的创建与销毁

```
用户点击 Play
    │
    ▼
UEditorEngine::StartQueuedPlayMapRequest()
    │
    ▼
UEditorEngine::CreatePIEWorldByDuplication(UWorld* EditorWorld)
    │
    ├──► 创建新的 UWorld 对象（NewObject<UWorld>）
    ├──► 调用 EditorWorld->DuplicateWorldForPIE()
    │       ├──► 序列化 EditorWorld 到内存 Archive
    │       ├──► 反序列化到新 UWorld（修改对象名、Outer 等）
    │       └──► 修复交叉引用（如 LevelScriptActor）
    ├──► 创建 FWorldContext，加入 WorldList
    └──► 初始化 GameInstance、GameMode、PlayerController
    │
    ▼
PIE 运行中（Tick 分派到 PIE World）
    │
    ▼
用户点击 Stop / Escape
    │
    ▼
UEditorEngine::EndPlayMap()
    ├──► 通知 PIE World EndPlay
    ├──► 销毁 PIE GameInstance
    ├──► 从 WorldList 移除 FWorldContext
    ├──► 标记 PIE World 为 PendingKill
    └──► GC 回收 PIE World 及其所有对象
    │
    ▼
恢复 EditorWorld 的视口和选中状态
```

### 内存分配来源

| 子系统 | 分配来源 | 说明 |
|--------|---------|------|
| FWorldContext | UObject GC Heap（TArray 内联） | 随 UEditorEngine 生命周期管理 |
| PIE UWorld | UObject GC Heap | `NewObject<UWorld>` 创建，Stop PIE 后 MarkPendingKill |
| PIE GameInstance | UObject GC Heap | 独立创建，与 Editor GameInstance 隔离 |
| PIE Level/Actor | UObject GC Heap | 通过 Duplicate 创建，Outer 指向 PIE World |

---

## 行为分析（第 3 层）

### 关键函数调用链：启动 PIE

> 文件：`Engine/Source/Editor/UnrealEd/Private/PlayLevel.cpp`，第 100~300 行（近似范围）

```cpp
void UEditorEngine::StartQueuedPlayMapRequest()
{
    // 1. 保存编辑器世界的当前状态（选中、相机位置等）
    SaveEditorState();
    
    // 2. 根据 PIE 设置选择创建方式（Duplicate / Streaming / None）
    if (PlayWorldSettings->PlayNetMode == PIE_Standalone)
    {
        // 单机模式：直接复制 EditorWorld
        UWorld* PIEWorld = CreatePIEWorldByDuplication(EditorWorld);
        
        // 3. 创建新的 WorldContext
        FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
        PIEContext.ThisCurrentWorld = PIEWorld;
        PIEContext.OwningGameInstance = NewObject<UGameInstance>(...);
        
        // 4. 初始化 GameInstance
        PIEContext.OwningGameInstance->InitializeForPlayInEditor(PIEWorld);
        
        // 5. 初始化 World（BeginPlay、Tick 启动）
        PIEWorld->InitializeActorsForPlay(...);
    }
    else
    {
        // 多人模式：创建多个 Client/Server WorldContext
        CreatePIEWorldForNetworkPlay(...);
    }
}
```

### 关键函数调用链：停止 PIE

> 文件：`Engine/Source/Editor/UnrealEd/Private/PlayLevel.cpp`，第 400~550 行（近似范围）

```cpp
void UEditorEngine::EndPlayMap()
{
    // 1. 结束所有 PIE World 的游戏逻辑
    for (FWorldContext& WorldContext : WorldList)
    {
        if (WorldContext.WorldType == EWorldType::PIE)
        {
            UWorld* PIEWorld = WorldContext.ThisCurrentWorld;
            if (PIEWorld)
            {
                PIEWorld->CleanupWorld();
            }
            
            // 2. 销毁 GameInstance
            if (WorldContext.OwningGameInstance)
            {
                WorldContext.OwningGameInstance->Shutdown();
            }
        }
    }
    
    // 3. 从 WorldList 移除 PIE Context
    WorldList.RemoveAll([](const FWorldContext& Context) {
        return Context.WorldType == EWorldType::PIE;
    });
    
    // 4. 触发 GC，回收 PIE 对象
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    
    // 5. 恢复编辑器状态
    RestoreEditorState();
}
```

### 多线程与同步

- **Game Thread**：PIE 世界的 `Tick` 与 Editor 世界的更新在**同一个 Game Thread** 上串行执行，但 UE 通过 `FWorldContext` 的隔离保证二者不互相干扰
- **Render Thread**：Editor 视口和 PIE 视口各自提交渲染命令到 Render Thread，RHI 层区分不同的 ViewFamily
- **Async Loading Thread**：PIE 世界可能触发异步资源加载，加载完成后通过回调设置 `ThisCurrentWorld` 的引用
- **同步原语**：`UEditorEngine` 对 `WorldList` 的访问在单线程中完成，无需显式锁；PIE 与 Editor 的隔离通过对象复制（而非共享）实现

### 性能优化手段

- **世界复制（Duplicate）**：PIE 世界通过内存序列化/反序列化快速复制，而非重新加载关卡文件
- **增量更新**：PIE 运行时只 Tick PIE World，Editor World 暂停 Tick（除非 Simulate）
- **对象隔离**：PIE 对象与 Editor 对象通过不同的 `Outer`（PIE World vs Editor World）和命名空间隔离，避免引用混乱
- **GC 分代**：PIE 结束后一次性 `CollectGarbage`，快速回收整个 PIE 世界的对象

---

## 与上下层的关系

### 上层调用者

| 上层模块 | 调用方式 | 用途 |
|----------|---------|------|
| `LevelEditor` | 工具栏 Play/Simulate 按钮 | 触发 `UEditorEngine::StartQueuedPlayMapRequest()` |
| `BlueprintGraph` | PIE 调试图 | 在 PIE 中暂停并检查蓝图变量 |
| `Kismet` | 蓝图编译后自动 PIE | 验证蓝图逻辑 |

### 下层依赖

| 下层模块 | 依赖方式 | 用途 |
|----------|---------|------|
| `Engine` | Public | `UEngine`、`UWorld`、`UGameInstance`、`FWorldContext` |
| `CoreUObject` | Public | UObject 序列化（DuplicateWorldForPIE）、GC |
| `Slate` | Public | PIE 视口的 Slate 渲染 |
| `RenderCore` / `RHI` | Private | 多视口渲染命令分派 |

---

## 设计亮点与可迁移经验

1. **WorldContext 隔离模型**：通过 `FWorldContext` 结构体封装世界类型、GameInstance、World 指针，实现同一进程内多世界共存，是编辑器嵌入游戏运行的核心设计。
2. **序列化复制实现世界隔离**：PIE 世界不是重新加载关卡，而是通过 `DuplicateWorldForPIE` 将 EditorWorld 序列化到内存再反序列化，既保持数据一致性又实现完全隔离。
3. **统一 Tick 分派**：`UEngine::Tick` 遍历所有 `FWorldContext`，根据 `WorldType` 分派到 Editor Tick 或 PIE Tick，保证单线程安全。
4. **完整生命周期管理**：PIE 世界从创建、初始化、运行到销毁有完整的闭环，`CleanupWorld` + `CollectGarbage` 保证资源不泄漏。

---

## 关键源码片段

> 文件：`Engine/Source/Editor/UnrealEd/Classes/Editor/EditorEngine.h`，第 200~260 行（近似范围）

```cpp
UCLASS()
class UNREALED_API UEditorEngine : public UEngine
{
    GENERATED_BODY()
public:
    // 所有世界上下文（Editor + PIE + SIE）
    UPROPERTY()
    TArray<FWorldContext> WorldList;

    // 编辑器原始世界
    UPROPERTY()
    TObjectPtr<UWorld> EditorWorld;

    // PIE 世界的 GameInstance
    UPROPERTY()
    TObjectPtr<UGameInstance> PlayGameInstance;

    // 启动 PIE
    virtual void StartQueuedPlayMapRequest();
    
    // 停止 PIE
    virtual void EndPlayMap();
    
    // 通过复制 EditorWorld 创建 PIE 世界
    virtual UWorld* CreatePIEWorldByDuplication(UWorld* InWorld);
};
```

> 文件：`Engine/Source/Runtime/Engine/Classes/Engine/Engine.h`，第 80~130 行（近似范围）

```cpp
USTRUCT()
struct FWorldContext
{
    GENERATED_BODY()
    
    // 世界类型：Editor / PIE / Game / Preview / Inactive
    UPROPERTY()
    TEnumAsByte<EWorldType::Type> WorldType;
    
    // 当前关联的 UWorld
    UPROPERTY()
    TObjectPtr<UWorld> ThisCurrentWorld;
    
    // 所属的 GameInstance
    UPROPERTY()
    TObjectPtr<UGameInstance> OwningGameInstance;
    
    // 唯一上下文句柄
    UPROPERTY()
    FName ContextHandle;
};
```

---

## 关联阅读

- [[UE-UnrealEd-源码解析：编辑器框架总览]] — UEditorEngine 的完整架构与职责
- [[UE-Engine-源码解析：World 与 Level 架构]] — UWorld 的内存结构与 Level 管理
- [[UE-Engine-源码解析：GameFramework 与规则体系]] — PIE 中 GameMode、PlayerController 的初始化
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]] — 多世界共存时的 Tick 分派机制
- [[UE-CoreUObject-源码解析：UObject 体系总览]] — DuplicateWorldForPIE 依赖 UObject 序列化体系

---

## 索引状态

- **所属 UE 阶段**：第五阶段 — 编辑器层
- **对应 UE 笔记**：`UE-UnrealEd-源码解析：PIE 模式与世界隔离`
- **本轮完成度**：✅ 第三轮（骨架扫描 + 数据结构/行为分析 + 关联辐射）
- **更新日期**：2026-04-18
